#!/usr/bin/env python3
"""Validate all portable shaders, original identities and embedded resources."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--spirv-val',required=True)
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('portable_assets',root/'tools/portable-assets.py')
checks=importlib.util.module_from_spec(spec);spec.loader.exec_module(checks)
assert checks.verify(root/'assets/general',root/'assets/portable')==150
for path in sorted((root/'assets/portable').glob('*/*/*.spv')):
 for target in ('vulkan1.1','vulkan1.3'):
  subprocess.run([a.spirv_val,'--target-env',target,str(path)],check=True)
with tempfile.TemporaryDirectory(prefix='fsr4-portable-test-') as directory:
 out=Path(directory)
 subprocess.run(['python3',str(root/'tools/embed-general-assets.py'),str(root/'assets/general'),str(out)],check=True)
 entries=json.loads((out/'embedded-assets.json').read_text())['entries']
 assert len(entries)==320 and len({e['path'] for e in entries})==320
 for e in entries:
  path=root/'assets'/e['path']
  if '.portable.spv' in e['path']:
   path=root/'assets/portable'/e['path'].removeprefix('general/').replace('.portable.spv','.spv')
  assert hashlib.sha256(path.read_bytes()).hexdigest()==e['sha256']
for raw in (b'',b'bad',b'\x03\x02\x23\x07'+bytes(20)):
 try:list(checks.instructions(raw))
 except ValueError:pass
 else:raise AssertionError('Malformed SPIR-V accepted')
print('150 portable shaders validated for Vulkan 1.1/1.3; all 170 original payloads retained.')
