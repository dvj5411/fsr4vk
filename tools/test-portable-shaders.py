#!/usr/bin/env python3
"""Validate every portable variant and check that original payloads are retained."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--spirv-val', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('lowering', root/'tools/lower-mixed-dot.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def instructions(raw):
    words = struct.unpack(f'<{len(raw)//4}I', raw)
    offset = 5
    while offset < len(words):
        length = words[offset] >> 16
        yield words[offset] & 0xffff, words[offset+1:offset+length]
        offset += length


with tempfile.TemporaryDirectory(prefix='fsr4-portable-test-') as directory:
    output = Path(directory)
    subprocess.run(['python3', str(root/'tools/embed-general-assets.py'),
                    str(root/'assets/general'), str(output)], check=True)
    entries = json.loads((output/'embedded-assets.json').read_text())['entries']
    assert len(entries) == 180
    assert len({e['path'] for e in entries}) == 180
    for entry in entries:
        if '.portable.spv' not in entry['path']:
            assert hashlib.sha256((root/'assets'/entry['path']).read_bytes()).hexdigest() == entry['sha256']
    for source in sorted((root/'assets/general').glob('*/*/*.spv')):
        original = source.read_bytes()
        converted, count = module.lower(original)
        if source.name != 'pass-01.spv':
            assert converted == original and count == 0
            continue
        assert count == 64
        assert module.lower(converted) == (converted, 0)
        assert not any(op == 6916 or (op == 17 and a[0] == 6912)
                       for op, a in instructions(converted))
        assert b'SPV_VALVE_mixed_float_dot_product' not in converted
        # Descriptor bindings, layouts, entry interfaces, execution modes and
        # physical pointer types remain byte-for-byte identical instructions.
        def abi(raw):
            return [(op, a) for op, a in instructions(raw)
                    if op in (14, 15, 16, 331, 30, 32, 59, 72)
                    or (op == 71 and a[1] in (2, 3, 6, 11, 33, 34))]
        assert abi(original) == abi(converted)
        candidate = output/'candidate.spv'
        candidate.write_bytes(converted)
        for target in ('vulkan1.1', 'vulkan1.3'):
            subprocess.run([args.spirv_val, '--target-env', target, str(candidate)], check=True)
    for invalid in (b'', b'bad', struct.pack('<6I', 0x07230203, 0x10300, 0, 1, 0, 0)):
        try:
            module.lower(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError('Malformed shader accepted')
print('10 portable variants validated; 140 other shaders and all 170 original payloads unchanged.')
