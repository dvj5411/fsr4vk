#!/usr/bin/env python3
import os,json,subprocess,hashlib,time
from pathlib import Path
import argparse
p=argparse.ArgumentParser(description="Validate the embedded Windows DLL against a captured two-frame matrix.")
p.add_argument('--output',type=Path,required=True)
p.add_argument('--cases',type=Path,required=True)
p.add_argument('--assets',type=Path,required=True)
p.add_argument('--metrics',type=Path,required=True)
p.add_argument('--probe',type=Path,required=True)
p.add_argument('--proton',type=Path,required=True)
p.add_argument('--compat-data',type=Path,required=True)
p.add_argument('--steam-root',type=Path,required=True)
p.add_argument('--driver-filter',required=True)
a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
proton=a.proton.resolve();exe=a.probe.resolve();dll=exe.parent/'amd_fidelityfx_upscaler_vk.dll'
w=lambda p:'Z:'+str(p).replace('/','\\')
results=[]
for c in json.loads(a.cases.read_text())['cases']:
 rw,rh=c['input'];ow,oh=c['output'];name=c['case'];bundle=a.assets.resolve()/'general'/c['tier']/c['preset'];output=out/(name+'.rgba16f');log=out/(name+'.log');diag=out/(name+'.provider.log')
 assert not output.exists(), 'refusing overwrite'
 e=os.environ.copy();e.pop('FSR4_VK_ASSET_ROOT',None);e.update(STEAM_COMPAT_DATA_PATH=str(a.compat_data.resolve()),STEAM_COMPAT_CLIENT_INSTALL_PATH=str(a.steam_root.resolve()),SteamAppId='0',SteamGameId='0',VK_LOADER_DRIVERS_SELECT=a.driver_filter,VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation',WINEDEBUG='-all',FSR4_TEST_RENDER_SIZE=f'{rw}x{rh}',FSR4_TEST_OUTPUT_SIZE=f'{ow}x{oh}',FSR4_VK_LOG_PATH=w(diag))
 args=[w(exe),w(bundle),w(bundle/'initializers.bin'),w(bundle/'weights.bin'),w(output),'--provider-temporal']
 command=subprocess.list2cmdline(args)+' > '+subprocess.list2cmdline([w(log)])+' 2>&1'
 entry={'case':name,'input':c['input'],'output':c['output'],'frames':[]}
 try:
  with (out/(name+'.proton.log')).open('w') as f:
   result=subprocess.run([str(proton),'run','cmd.exe','/d','/c',command],env=e,cwd=exe.parent,stdout=f,stderr=subprocess.STDOUT,timeout=100)
  assert result.returncode==0, f'exit {result.returncode}'
  assert 'shader_backend=portable-int8' in diag.read_text(), 'missing backend dispatch proof'
  text=log.read_text(errors='replace')
  assert 'Validation Error' not in text and 'VUID-' not in text, 'Vulkan validation failure'
  assert 'context_frames=2' in text, 'missing temporal completion'
  ref=Path(c['runs'][0])
  for i,p in enumerate([Path(str(output)+'.frame0.rgba16f'),output]):
   assert p.stat().st_size==ow*oh*8
   metric=json.loads(subprocess.check_output([str(a.metrics.resolve()),str(ref/f'frame-{i:03}.rgba16f'),str(p)]))
   metric.update(frame=i,passed=metric['nonfinite_pairs']==0 and metric['rmse']<=.002 and metric['above_tolerance_fraction']<=.05);entry['frames'].append(metric)
  entry['status']='passed' if all(f['passed'] for f in entry['frames']) else 'failed_threshold'
 except Exception as error:entry.update(status='failed',error=str(error))
 results.append(entry);(out/'results.json').write_text(json.dumps({'dll_sha256':hashlib.sha256(dll.read_bytes()).hexdigest(),'cases':results},indent=2)+'\n');print(json.dumps(entry),flush=True)
 if entry['status']!='passed':break

raise SystemExit(0 if len(results)==len(json.loads(a.cases.read_text())['cases']) and all(x['status']=='passed' for x in results) else 1)
