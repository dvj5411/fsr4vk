#!/usr/bin/env python3
"""Read bounded AMD64 PE RCDATA and verify logical assets through our decoder."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from pe_resources import PEImage


def verify(dll, manifest_path, decoder):
    data = dll.read_bytes()
    pe = PEImage(data)
    resources = pe.rcdata()
    exports = pe.exports()
    required = {'ffxCreateContext', 'ffxDestroyContext', 'ffxConfigure', 'ffxQuery', 'ffxDispatch'}
    if not required <= set(exports):
        raise ValueError(f'missing provider exports: {sorted(required - set(exports))}')
    manifest = json.loads(manifest_path.read_text())
    seen, verified, paths = {}, [], set()
    if not manifest['entries']:
        raise ValueError('empty embedding manifest')
    with tempfile.TemporaryDirectory(prefix='fsr4-pe-check-') as temporary:
        payload = Path(temporary) / 'payload'
        for entry in manifest['entries']:
            if entry['path'] in paths:
                raise ValueError(f"duplicate logical asset: {entry['path']}")
            paths.add(entry['path'])
            rid = entry['resource_id']
            stored = resources[rid]
            if len(stored) != entry['stored_size'] or hashlib.sha256(stored).hexdigest() != entry['stored_sha256']:
                raise ValueError(f"stored payload mismatch: {entry['path']}")
            if not isinstance(entry['compressed'], bool) or not 0 < entry['size'] <= 64 * 1024 * 1024:
                raise ValueError('invalid payload mode/size')
            if rid not in seen:
                payload.write_bytes(stored)
                decoded = subprocess.check_output([str(decoder.resolve()), str(int(entry['compressed'])),
                                                   str(entry['size']), str(payload)], timeout=120)
                seen[rid] = (len(decoded), hashlib.sha256(decoded).hexdigest(), entry['compressed'])
            if seen[rid] != (entry['size'], entry['sha256'], entry['compressed']):
                raise ValueError(f"decoded payload mismatch: {entry['path']}")
            verified.append(dict(path=entry['path'], sha256=entry['sha256']))
    if set(resources) != set(seen):
        raise ValueError('unreferenced or missing RCDATA payloads')
    return dict(dll_sha256=hashlib.sha256(data).hexdigest(), dll_bytes=len(data),
                machine='AMD64', exports=exports, logical_assets=len(verified), unique_resources=len(seen),
                compression=manifest['compression'], unique_stored_bytes=sum(map(len, resources.values())), verified=verified)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('dll', 'manifest', 'decoder', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    report = verify(args.dll, args.manifest, args.decoder)
    with args.output.open('x') as stream:
        stream.write(json.dumps(report, indent=2, allow_nan=False) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'verified'}))


if __name__ == '__main__':
    main()
