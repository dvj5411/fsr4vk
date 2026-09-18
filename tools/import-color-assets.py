#!/usr/bin/env python3
"""Import SDK-selected color pre/post variants from matched reference captures.

Each capture must contain every unchanged model shader, while each new pre/post
shader must uniquely match its linear counterpart's descriptor/workgroup ABI.
Captured payloads belong to the private mixed-content project, not a clean
vendor-neutral source distribution.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess

spec=importlib.util.spec_from_file_location('colors',Path(__file__).with_name('color-assets.py'))
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('captures',type=Path);p.add_argument('general',type=Path);p.add_argument('output',type=Path)
p.add_argument('--spirv-val',required=True)
p.add_argument('--native-drs',type=Path,help='DRS captures using its baseline float-controls2-disabled translation')
a=p.parse_args()
if a.output.exists():p.error('output exists; refusing to overwrite assets')
for tier in ('1080','2160'):
 for preset in c.PRESETS:
  base=a.general/tier/preset
  linear={i:(base/f'pass-{i:02}.spv').read_bytes() for i in range(15)}
  for mode in c.MODES:
   captures={backend:a.captures/backend/mode/tier/preset/'shaders' for backend in ('native','portable')}
   if preset=='drs' and a.native_drs:captures['native']=a.native_drs/mode/tier/preset/'shaders'
   native={c.checks.identity(f.read_bytes()):f for f in captures['native'].glob('*.spv')}
   # Only pre/post may differ. Verify the exact native model bytes too.
   for i,raw in linear.items():
    if i not in (1,14) and native[c.checks.identity(raw)].read_bytes()!=raw:
     raise ValueError(f'unchanged model shader differs: {tier}/{preset}/{mode}/{i}')
   entries=[];payloads={}
   for i in (1,14):
    matches=[f for f in native.values() if c.checks.resource_abi(f.read_bytes())==c.checks.resource_abi(linear[i])]
    if len(matches)!=1:raise ValueError(f'ambiguous color shader: {tier}/{preset}/{mode}/{i}')
    original=matches[0];raw=original.read_bytes();identity=c.checks.identity(raw)
    portable=captures['portable']/(identity+'.spv');portable_raw=portable.read_bytes()
    c.checks.validate_pair(raw,portable_raw,i)
    for f in (original,portable):
     for target in ('vulkan1.1','vulkan1.3'):
      subprocess.run([a.spirv_val,'--target-env',target,str(f)],check=True,capture_output=True)
    name=f'pass-{i:02}.spv'
    entries.append(dict(file=name,sha256=c.sha(raw),portable_sha256=c.sha(portable_raw),
                        linear_sha256=c.sha(linear[i]),dxil=identity))
    payloads[name]=raw;payloads[name[:-4]+'.portable.spv']=portable_raw
   leaf=a.output/tier/preset/mode;leaf.mkdir(parents=True)
   for name,raw in payloads.items():(leaf/name).write_bytes(raw)
   provenance={b:json.loads((captures[b].parents[2]/'provenance.json').read_text()) for b in captures}
   (leaf/'manifest.json').write_text(json.dumps(dict(schema=1,tier=tier,preset=preset,color_space=mode,
                         shaders=entries,provenance=provenance),indent=2)+'\n')
print(f'Verified {len(c.verify(a.general,a.output))} color shader payloads')
