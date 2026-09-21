#!/usr/bin/env python3
"""Generate deduplicated PE resources from verified private bundle manifests."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import importlib.util
import os

spec = importlib.util.spec_from_file_location('portable_assets', Path(__file__).with_name('portable-assets.py'))
portable_assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(portable_assets)
color_spec = importlib.util.spec_from_file_location('color_assets', Path(__file__).with_name('color-assets.py'))
color_assets = importlib.util.module_from_spec(color_spec)
color_spec.loader.exec_module(color_assets)

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('general', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--compression', choices=('none','zstd'), default='none')
p.add_argument('--zstd', default=os.environ.get('ZSTD','zstd'))
a = p.parse_args()
subprocess.run([sys.executable, str(Path(__file__).with_name('verify-general-assets.py')), str(a.general)], check=True)
portable_root = a.general.parent/'portable'
print(f'Verified {portable_assets.verify(a.general, portable_root)} portable shaders.')
a.output.mkdir(parents=True, exist_ok=True)
resources, index, entries = {}, [], []
def add_resource(path, key, payload):
    if not payload or len(payload)>64*1024*1024:
        raise ValueError('asset outside runtime size limit: '+key)
    digest = hashlib.sha256(payload).hexdigest()
    if digest not in resources:
        stored=payload; compressed=False; stored_path=path.resolve()
        if a.compression=='zstd':
            candidate=subprocess.run([a.zstd,'-q','-19','--check','--stdout',str(path)],
                check=True,capture_output=True).stdout
            restored=subprocess.run([a.zstd,'-q','-d','--stdout'],input=candidate,
                check=True,capture_output=True).stdout
            if restored!=payload: raise ValueError('compressed asset round-trip failed: '+key)
            if len(candidate)<len(payload):
                stored=candidate;compressed=True
                directory=a.output/'payloads';directory.mkdir(exist_ok=True)
                stored_path=(directory/(digest+'.zst')).resolve()
                stored_path.write_bytes(stored)
        resources[digest] = (len(resources)+100,stored_path,len(stored),compressed,
                             hashlib.sha256(stored).hexdigest())
    rid,_,stored_size,compressed,stored_hash=resources[digest]
    index.append(f'    {{{json.dumps(key)}, {rid}, {len(payload)}, {stored_size}, {str(compressed).lower()}}},')
    entries.append(dict(path=key, resource_id=rid, size=len(payload), sha256=digest,
                        stored_size=stored_size, compressed=compressed, stored_sha256=stored_hash))

for manifest in sorted(a.general.glob('*/*/manifest.json')):
    data = json.loads(manifest.read_text())
    for name in [s['file'] for s in data['shaders']] + ['initializers.bin', 'weights.bin']:
        path = manifest.parent/name
        payload = path.read_bytes()
        key = 'general/'+path.relative_to(a.general).as_posix()
        add_resource(path, key, payload)
        if name.endswith('.spv'):
            portable_path = portable_root/path.relative_to(a.general)
            portable = portable_path.read_bytes()
            add_resource(portable_path, key[:-4]+'.portable.spv', portable)
color_root = a.general.parent/'colors'
for path in color_assets.verify(a.general,color_root):
    add_resource(path,'colors/'+path.relative_to(color_root).as_posix(),path.read_bytes())
fsr411_spec = importlib.util.spec_from_file_location('fsr411_assets', Path(__file__).with_name('fsr411-assets.py'))
fsr411_assets = importlib.util.module_from_spec(fsr411_spec)
fsr411_spec.loader.exec_module(fsr411_assets)
fsr411_root = a.general.parent/'fsr411'
for path in fsr411_assets.verify(fsr411_root):
    add_resource(path, 'fsr411/'+path.relative_to(fsr411_root).as_posix(), path.read_bytes())
(a.output/'embedded-assets.rc').write_text('\n'.join(f'{rid} RCDATA {json.dumps(str(path))}' for rid,path,*_ in resources.values())+'\n')
(a.output/'embedded-asset-index.hpp').write_text(
    '#pragma once\nnamespace fsr4assets {\nstruct EmbeddedEntry { const char* path; unsigned id; unsigned size; unsigned stored_size; bool compressed; };\n'
    'inline constexpr EmbeddedEntry embedded_index[] = {\n'+'\n'.join(index)+'\n};\n}\n')
(a.output/'embedded-assets.json').write_text(json.dumps(dict(schema=2, compression=a.compression, entries=entries), indent=2)+'\n')
print(f'Embedded {len(entries)} paths using {len(resources)} unique payloads.')
