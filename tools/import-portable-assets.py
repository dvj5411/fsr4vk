#!/usr/bin/env python3
"""Import a complete, verified set of target-translated shader bundles."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

spec=importlib.util.spec_from_file_location('portable_assets',Path(__file__).with_name('portable-assets.py'))
checks=importlib.util.module_from_spec(spec);spec.loader.exec_module(checks)
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('captures',type=Path)
p.add_argument('general',type=Path)
p.add_argument('output',type=Path)
p.add_argument('--spirv-val',required=True)
a=p.parse_args()
if a.output.exists():p.error('output exists; refusing to overwrite verified assets')
validated=[]
sha=lambda b:hashlib.sha256(b).hexdigest()
for tier in ('1080','2160'):
 for preset in ('native','quality','balanced','performance','ultraperf'):
  base=a.general/tier/preset;source=json.loads((base/'manifest.json').read_text());entries=[];payloads=[]
  for i,entry in enumerate(source['shaders']):
   original=(base/entry['file']).read_bytes();assert sha(original)==entry['sha256']
   name=checks.identity(original);dump=a.captures/tier/preset/'shaders'
   path=dump/(name+'.spv');raw=path.read_bytes();checks.validate_pair(original,raw,i)
   for target in ('vulkan1.1','vulkan1.3'):
    subprocess.run([a.spirv_val,'--target-env',target,str(path)],check=True)
   entries.append(dict(file=entry['file'],sha256=sha(raw),source_sha256=entry['sha256'],dxil_sha256=sha((dump/(name+'.dxil')).read_bytes()),dxil=name))
   payloads.append(raw)
  validated.append((tier,preset,dict(schema=1,capacity=source['capacity'],preset=preset,shaders=entries),payloads))
for tier,preset,manifest,payloads in validated:
 leaf=a.output/tier/preset;leaf.mkdir(parents=True)
 for entry,raw in zip(manifest['shaders'],payloads):(leaf/entry['file']).write_bytes(raw)
 (leaf/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(a.output/'provenance.json').write_bytes((a.captures/'provenance.json').read_bytes())
print('Imported 150 portable shaders; ABI and Vulkan 1.1/1.3 validation passed.')
