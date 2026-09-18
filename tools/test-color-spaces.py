#!/usr/bin/env python3
"""Compare each provider color mode with matching original-DLL two-frame outputs."""
import argparse
import json
import os
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--captures',type=Path,required=True)
p.add_argument('--assets',type=Path,required=True)
p.add_argument('--probe',type=Path,required=True)
p.add_argument('--portable-probe',type=Path,help='Isolated FSR4_RESEARCH_FORCE_PORTABLE build')
p.add_argument('--metrics',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--sdk',type=Path,required=True)
p.add_argument('--windows',action='store_true')
p.add_argument('--native-drs',type=Path)
p.add_argument('--backends',nargs='+',default=['native','portable'],choices=['native','portable'])
p.add_argument('--presets',nargs='+')
p.add_argument('--tiers',nargs='+',default=['1080','2160'])
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
w=lambda p:'Z:'+str(p.resolve()).replace('/','\\')
results=[]
for backend in a.backends:
 for mode in ('nonlinear','srgb','pq'):
  cases=json.loads((a.captures/backend/mode/'cases.json').read_text())['cases']
  for case in cases:
   if backend=='native' and case['preset']=='drs' and a.native_drs:
    case=next(c for c in json.loads((a.native_drs/mode/'cases.json').read_text())['cases'] if c['case']==case['case'])
   if case['tier'] not in a.tiers or (a.presets and case['preset'] not in a.presets):continue
   key=backend+'-'+mode+'-'+case['case'];out=a.output/(key+'.rgba16f')
   bundle=a.assets/'general'/case['tier']/case['preset'];log=a.output/(key+'.log')
   diagnostic=a.output/(key+'.provider.log')
   env=os.environ.copy()
   for var in ('FSR4_VK_ASSET_ROOT','FSR4_TEST_DYNAMIC_RESOLUTION','FSR4_TEST_COLOR_SWITCH','FSR4_TEST_SECOND_RENDER_SIZE','MESA_EXTENSION_OVERRIDE'):
    env.pop(var,None)
   env.update(FSR4_TEST_RENDER_SIZE='x'.join(map(str,case['input'])),
              FSR4_TEST_OUTPUT_SIZE='x'.join(map(str,case['output'])),FSR4_TEST_COLOR_SPACE=mode,
              FSR4_VK_LOG_PATH=w(diagnostic) if a.windows else str(diagnostic),
              VK_LOADER_LAYERS_DISABLE='~implicit~',VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation',
              VK_LAYER_PATH=str(a.sdk/'share/vulkan/explicit_layer.d'),LD_LIBRARY_PATH=str(a.sdk/'lib'))
   if case.get('dynamic_resolution'):env['FSR4_TEST_DYNAMIC_RESOLUTION']='1'
   probe=a.portable_probe if backend=='portable' else a.probe
   if probe is None:raise RuntimeError('portable validation requires an explicit portable test build')
   args=[str(probe),str(bundle),str(bundle/'initializers.bin'),str(bundle/'weights.bin'),str(out),'--provider-temporal']
   if a.windows:
    assert env.get('WINEPREFIX') and env.get('PROTONPATH')
    env.update(GAMEID='umu-default',STORE='none',PROTONFIXES_DISABLE='1',UMU_RUNTIME_UPDATE='0',WINEDEBUG='-all')
    command=[w(probe),w(bundle),w(bundle/'initializers.bin'),w(bundle/'weights.bin'),w(out),'--provider-temporal']
    batch=a.output/(key+'.cmd')
    batch.write_text('@echo off\n'+subprocess.list2cmdline(command)+' > '+subprocess.list2cmdline([w(log)])+' 2>&1\nexit /b %errorlevel%\n')
    args=['umu-run','cmd.exe','/d','/c',w(batch)]
   else:env['FSR4_VK_ASSET_ROOT']=str(a.assets)
   result=dict(case=key,frames=[])
   try:
    with (a.output/(key+'.umu.log') if a.windows else log).open('w') as stream:
     subprocess.run(args,cwd=probe.parent,env=env,stdout=stream,stderr=subprocess.STDOUT,check=True,timeout=100)
    text=log.read_text(errors='replace');proof=diagnostic.read_text()
    if 'VUID-' in text or 'Validation Error' in text:raise RuntimeError('Vulkan validation error')
    if 'context_frames=2' not in text:raise RuntimeError('temporal completion proof missing')
    expected='native-mixed-dot' if backend=='native' else 'portable-int8'
    if 'shader_backend='+expected not in proof or 'color_space='+mode not in proof:
     raise RuntimeError('wrong shader backend or color selection')
    reference=Path(case['runs'][0])
    for i,image in enumerate([Path(str(out)+'.frame0.rgba16f'),out]):
     metric=json.loads(subprocess.check_output([str(a.metrics),str(reference/f'frame-{i:03}.rgba16f'),str(image)],timeout=30))
     metric.update(frame=i,passed=metric['nonfinite_pairs']==0 and metric['rmse']<=.002 and metric['above_tolerance_fraction']<=.05)
     result['frames'].append(metric)
    result['status']='passed' if all(m['passed'] for m in result['frames']) else 'failed_threshold'
   except (OSError,RuntimeError,ValueError,subprocess.SubprocessError) as e:result.update(status='failed',error=str(e))
   results.append(result)
   (a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
   print(json.dumps(result),flush=True)
raise SystemExit(int(any(r['status']!='passed' for r in results)))
