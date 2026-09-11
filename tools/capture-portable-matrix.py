#!/usr/bin/env python3
import os,subprocess,json,hashlib,time
from pathlib import Path
import argparse
p=argparse.ArgumentParser(description="Capture portable shaders and two-frame reference images on the target GPU.")
p.add_argument('--output',type=Path,required=True)
p.add_argument('--proton',type=Path,required=True)
p.add_argument('--probe',type=Path,required=True)
p.add_argument('--dll',type=Path,required=True)
p.add_argument('--vkd3d',type=Path,required=True)
p.add_argument('--compat-data',type=Path,required=True)
p.add_argument('--steam-root',type=Path,required=True)
p.add_argument('--gpu',required=True)
p.add_argument('--driver',required=True)
p.add_argument('--driver-filter',required=True)
a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
proton=a.proton.resolve();probe=a.probe.resolve();dll=a.dll.resolve();vkd3d=a.vkd3d.resolve()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
provenance={'dll_sha256':sha(dll),'probe_sha256':sha(probe),'vkd3d_sha256':sha(vkd3d),'VKD3D_CONFIG':'force_raw_va_cbv','VKD3D_DISABLE_EXTENSIONS':'VK_NV_raw_access_chains','gpu':a.gpu,'driver':a.driver}
if (out/'provenance.json').exists():
 assert json.loads((out/'provenance.json').read_text())==provenance, 'capture provenance changed'
(out/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
cases=[]
for tier,ow,oh,inputs in [('1080',1920,1080,[(1920,1080),(1280,720),(1129,635),(960,540),(640,360)]),('2160',3840,2160,[(3840,2160),(2560,1440),(2258,1270),(1920,1080),(1280,720)])]:
 for preset,(rw,rh) in zip(('native','quality','balanced','performance','ultraperf'),inputs):
  d=out/tier/preset;d.mkdir(parents=True,exist_ok=True);dump=d/'shaders';dump.mkdir(exist_ok=True)
  ref=d/'reference'/dll.stem
  case={'case':tier+'-'+preset,'input':[rw,rh],'output':[ow,oh],'tier':tier,'preset':preset,'runs':[str(ref)],'shader_dump':str(dump)}
  if not (ref/'frame-001.rgba16f').exists():
   e=os.environ.copy();e.update(STEAM_COMPAT_DATA_PATH=str(a.compat_data.resolve()),STEAM_COMPAT_CLIENT_INSTALL_PATH=str(a.steam_root.resolve()),SteamAppId='0',SteamGameId='0',VK_LOADER_DRIVERS_SELECT=a.driver_filter,WINEDEBUG='-all',VKD3D_CONFIG='force_raw_va_cbv',VKD3D_DISABLE_EXTENSIONS='VK_NV_raw_access_chains',VKD3D_SHADER_DUMP_PATH=str(dump),VKD3D_SHADER_CACHE_PATH='0')
   w=lambda p:'Z:'+str(p).replace('/','\\')
   cmd=[str(proton),'run',str(probe),'--frames','2','--render-size',f'{rw}x{rh}','--output-size',f'{ow}x{oh}','--output-dir',w(d/'reference'),'--nms-inputs',w(dll)]
   with (d/'capture.log').open('w') as f:
    result=subprocess.run(cmd,cwd=probe.parent,env=e,stdout=f,stderr=subprocess.STDOUT,timeout=100)
   assert result.returncode==0, (case,result.returncode)
  assert (ref/'frame-001.rgba16f').stat().st_size==ow*oh*8
  cases.append(case);(out/'cases.json').write_text(json.dumps({'cases':cases},indent=2)+'\n')
  print('captured',case['case'],len(list(dump.glob('*.spv'))),'shaders',flush=True)
