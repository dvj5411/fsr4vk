#!/usr/bin/env python3
"""Verify the complete official 4.1.1 INT8 model and color asset matrix."""
import hashlib
import json
from pathlib import Path

PRESETS = ('native', 'quality', 'balanced', 'performance', 'ultraperf', 'drs')
OFFICIAL_DLL_SHA256 = 'd0dcccc74a43c44ba435b7a369b456e0970d8a4464e4bd683119b374f2c9fb46'


def verify(root):
    paths = []
    for tier in ('1080', '2160'):
        for preset in PRESETS:
            general = root/'general'/tier/preset
            base = None
            for color in ('linear', 'nonlinear', 'srgb', 'pq'):
                bundle = general if color == 'linear' else root/'colors'/tier/preset/color
                manifest = json.loads((bundle/'manifest.json').read_text())
                provenance = manifest['provenance']
                if (manifest['version'] != '4.1.1' or manifest['arithmetic'] != 'int8' or
                    provenance['dll_sha256'] != OFFICIAL_DLL_SHA256 or provenance['backend'] != 'portable' or
                    provenance['preset'] != preset or provenance['capacity'][1] != int(tier) or
                    provenance.get('color', 'linear') != color):
                    raise ValueError(f'{bundle}: unexpected capture provenance')
                shaders = manifest['shaders']
                if [s['file'] for s in shaders] != [f'pass-{i:02}.spv' for i in range(28)]:
                    raise ValueError(f'{bundle}: incomplete shader sequence')
                for shader in shaders:
                    path = bundle/shader['file']
                    if hashlib.sha256(path.read_bytes()).hexdigest() != shader['sha256']:
                        raise ValueError(f'{path}: digest mismatch')
                initializer = bundle/'initializers.bin'
                if initializer.stat().st_size != 131072 or hashlib.sha256(initializer.read_bytes()).hexdigest() != manifest['initializers_sha256']:
                    raise ValueError(f'{initializer}: initializer mismatch')
                if base is None:
                    base = manifest
                    paths += [general/s['file'] for s in shaders] + [initializer]
                else:
                    if manifest['initializers_sha256'] != base['initializers_sha256'] or any(
                        shaders[i]['sha256'] != base['shaders'][i]['sha256'] for i in range(28) if i not in (1,27)):
                        raise ValueError(f'{bundle}: color variant changes the model')
                    paths += [bundle/'pass-01.spv', bundle/'pass-27.spv']
    return paths


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('root', type=Path)
    a = p.parse_args()
    print(f'Verified {len(verify(a.root))} FSR 4.1.1 runtime assets.')
