"""Verify captured color-only pre/post shader overlays before embedding."""
import hashlib
import importlib.util
import json
from pathlib import Path

spec=importlib.util.spec_from_file_location('portable_assets',Path(__file__).with_name('portable-assets.py'))
checks=importlib.util.module_from_spec(spec);spec.loader.exec_module(checks)
PRESETS=('native','quality','balanced','performance','ultraperf','drs')
MODES=('nonlinear','srgb','pq')
sha=lambda raw:hashlib.sha256(raw).hexdigest()

def verify(general, colors):
    result=[]
    for tier in ('1080','2160'):
        for preset in PRESETS:
            for mode in MODES:
                leaf=colors/tier/preset/mode
                manifest=json.loads((leaf/'manifest.json').read_text())
                if (manifest['tier'],manifest['preset'],manifest['color_space'])!=(tier,preset,mode):
                    raise ValueError(f'color identity mismatch: {leaf}')
                if [x['file'] for x in manifest['shaders']]!=['pass-01.spv','pass-14.spv']:
                    raise ValueError(f'incomplete color pair: {leaf}')
                for entry in manifest['shaders']:
                    name=entry['file'];base=(general/tier/preset/name).read_bytes()
                    native=(leaf/name).read_bytes()
                    portable_name=name[:-4]+'.portable.spv'
                    portable=(leaf/portable_name).read_bytes()
                    if sha(base)!=entry['linear_sha256'] or sha(native)!=entry['sha256'] or sha(portable)!=entry['portable_sha256']:
                        raise ValueError(f'color hash mismatch: {leaf/name}')
                    if checks.resource_abi(base)!=checks.resource_abi(native):
                        raise ValueError(f'color resource ABI mismatch: {leaf/name}')
                    checks.validate_pair(native,portable,int(name[5:7]))
                    result.extend([leaf/name,leaf/portable_name])
    return result
