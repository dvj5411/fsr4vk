#!/usr/bin/env python3
"""Exercise 19 queued NMS-format frames across color changes without host resets."""
import argparse
import json
import os
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('root',type=Path);p.add_argument('--sdk',type=Path,required=True)
p.add_argument('--backends',nargs='+',default=['native'],choices=['native','portable'])
a=p.parse_args();r=a.root.resolve();out=r/'color-switching';out.mkdir(exist_ok=False)
w=lambda p:'Z:'+str(p).replace('/','\\')
results=[]
for backend in a.backends:
 for mode in ('linear','nonlinear'):
  key=backend+'-'+mode;image=out/(key+'.rgba16f');diagnostic=out/(key+'.provider.log')
  env=os.environ.copy()
  for name in ('FSR4_VK_ASSET_ROOT','MESA_EXTENSION_OVERRIDE','FSR4_TEST_RENDER_SIZE','FSR4_TEST_OUTPUT_SIZE','FSR4_TEST_DYNAMIC_RESOLUTION'):
   env.pop(name,None)
  env.update(FSR4_TEST_COLOR_SPACE=mode,FSR4_TEST_COLOR_SWITCH='1',FSR4_TEST_VULKAN_1_1='1',
             FSR4_VK_LOG_PATH=w(diagnostic),GAMEID='umu-default',STORE='none',PROTONFIXES_DISABLE='1',
             UMU_RUNTIME_UPDATE='0',WINEDEBUG='-all',VK_LOADER_LAYERS_DISABLE='~implicit~',
             VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation',VK_LAYER_PATH=str(a.sdk/'share/vulkan/explicit_layer.d'),
             LD_LIBRARY_PATH=str(a.sdk/'lib'))
  probe=r/('portable-test/provider-dispatch-smoke.exe' if backend=='portable' else 'provider-dispatch-smoke.exe')
  bundle=r/'assets/general/1080/quality';log=out/(key+'.log')
  batch=out/(key+'.cmd')
  command=[w(probe),w(bundle),w(bundle/'initializers.bin'),w(bundle/'weights.bin'),w(image),'--provider-nms-formats']
  batch.write_text('@echo off\n'+subprocess.list2cmdline(command)+' > '+subprocess.list2cmdline([w(log)])+' 2>&1\nexit /b %errorlevel%\n')
  with (out/(key+'.umu.log')).open('w') as f:
   proc=subprocess.run(['umu-run','cmd.exe','/d','/c',w(batch)],cwd=probe.parent,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=100)
  text=log.read_text(errors='replace');proof=diagnostic.read_text() if diagnostic.exists() else ''
  first=Path(str(image)+'.frame0.rgba16f')
  expected='native-mixed-dot' if backend=='native' else 'portable-int8'
  passed=(proc.returncode==0 and 'VUID-' not in text and 'Validation Error' not in text and
          'shader_backend='+expected in proof and 'api=1.1' in proof and
          'context destroyed cached_models=1' in proof and
          'context_frames=19' in text and
          proof.count('creating model=')==1 and proof.count('color_space=')>=19 and
          all('color_space='+x in proof for x in (mode,'srgb','pq')) and
          image.exists() and first.exists() and image.read_bytes()==first.read_bytes())
  results.append(dict(case=key,passed=passed,returncode=proc.returncode))
  (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
  print(json.dumps(results[-1]),flush=True)
raise SystemExit(int(not all(r['passed'] for r in results)))
