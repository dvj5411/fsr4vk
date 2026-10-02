#!/usr/bin/env python3
"""Diagnostic ONLY: guard excess X lanes in the exact 1080 Quality model-11 module."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

EXPECTED = 'd619ac312d5a43d2f88da0afc5edc9193eb7f5743a56337f263b6f04066fd1cc'


def guarded(payload):
    if hashlib.sha256(payload).hexdigest() != EXPECTED:
        raise ValueError('not the reviewed 1080 Quality model-11 module')
    words = list(struct.unpack('<%dI' % (len(payload)//4), payload))
    ins = []
    i = 5
    while i < len(words):
        count, op = words[i] >> 16, words[i] & 0xffff
        if not count or i+count > len(words):
            raise ValueError('malformed instruction')
        ins.append((i, op, words[i+1:i+count]))
        i += count
    # Narrow contract, validated before injection; no reduction/barrier semantics changed.
    if not (any(op == 71 and a == [17, 11, 28] for _, op, a in ins)
            and any(op == 43 and a == [6, 95, 480] for _, op, a in ins)
            and any(op == 16 and a == [4, 17, 64, 1, 1] for _, op, a in ins)
            and not any(op == 224 for _, op, _ in ins)):
        raise ValueError('unexpected shader contract or workgroup barrier')
    function = next(n for n, (_, op, a) in enumerate(ins) if op == 54 and a[1] == 4)
    label_offset, op, args = ins[function+1]
    if op != 248 or args != [5] or ins[function+2][1] != 249:
        raise ValueError('unexpected entry block; refusing to move instructions')
    bound = words[3]
    load, x, condition, merge, reject = range(bound, bound+5)
    def emit(op, *args):
        return [((len(args)+1) << 16) | op, *args]
    guard = emit(61, 15, load, 17) + emit(81, 6, x, load, 0)
    guard += emit(176, 93, condition, x, 95)  # OpULessThan
    guard += emit(247, merge, 0) + emit(250, condition, merge, reject)
    guard += emit(248, reject) + emit(253) + emit(248, merge)
    words[3] = bound+5
    at = label_offset+2
    words[at:at] = guard
    return struct.pack('<%dI' % len(words), *words)


def build(source, target):
    manifest = json.loads((source/'manifest.json').read_text())
    if manifest['preset'] != 'quality' or manifest['capacity'] != [1920, 1080]:
        raise ValueError('diagnostic supports only 1080 Quality')
    if sorted(row['file'] for row in manifest['shaders']) != [f'pass-{i:02}.spv' for i in range(15)]:
        raise ValueError('expected exactly the 15 canonical shaders')
    files = {}
    for row in manifest['shaders']:
        if Path(row['file']).name != row['file']:
            raise ValueError('unexpected shader path')
        data = (source/row['file']).read_bytes()
        if hashlib.sha256(data).hexdigest() != row['sha256']:
            raise ValueError('source hash mismatch')
        if row['file'] == 'pass-12.spv':
            data = guarded(data)
            row['sha256'] = hashlib.sha256(data).hexdigest()
        files[row['file']] = data
    for name, key in [('initializers.bin', 'initializer_sha256'), ('weights.bin', 'weights_sha256')]:
        data = (source/name).read_bytes()
        if hashlib.sha256(data).hexdigest() != manifest[key]:
            raise ValueError('coefficient hash mismatch')
        files[name] = data
    target.mkdir(parents=True, exist_ok=False)
    for name, data in files.items():
        (target/name).write_bytes(data)
    (target/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('target', type=Path)
    a = p.parse_args()
    build(a.source, a.target)
