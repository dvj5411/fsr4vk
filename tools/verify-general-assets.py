#!/usr/bin/env python3
"""Verify all ten private shader/model bundles before using a candidate package."""
import argparse
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('general', type=Path)
a = p.parse_args()
count = 0
for tier in ('1080', '2160'):
    for preset in ('native', 'quality', 'balanced', 'performance', 'ultraperf'):
        leaf = a.general/tier/preset
        manifest = json.loads((leaf/'manifest.json').read_text())
        assert manifest['preset'] == preset
        assert manifest['capacity'] == ([1920,1080] if tier == '1080' else [3840,2160])
        assert [s['file'] for s in manifest['shaders']] == [f'pass-{i:02}.spv' for i in range(15)]
        entries = [(s['file'],s['sha256']) for s in manifest['shaders']]
        entries += [('initializers.bin',manifest['initializer_sha256']),('weights.bin',manifest['weights_sha256'])]
        for name, digest in entries:
            if hashlib.sha256((leaf/name).read_bytes()).hexdigest() != digest:
                raise RuntimeError(f'hash mismatch: {leaf/name}')
        count += 1
print(f'Verified {count} complete bundles (170 payload files).')
