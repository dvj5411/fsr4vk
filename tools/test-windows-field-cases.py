#!/usr/bin/env python3
"""Bounded W6400 synthetic cases matching the supplied Windows logs.

Usage: script isolated-test-root native|windows result-directory-name
The root must contain assets, preset-pr1161 and/or preset-dispatch-smoke.exe.
No game files are modified. Windows mode requires the existing GE-Proton11-5.
"""
import hashlib,json,os,subprocess,sys,time
from pathlib import Path
root=Path(sys.argv[1]).resolve(); mode=sys.argv[2]
assets=Path(os.environ.get('FSR4_TEST_ASSETS',str(root/'assets')))
out=root/sys.argv[3]; out.mkdir(exist_ok=False)
sdk=Path(os.environ['VULKAN_SDK']); deadline=time.monotonic()+480
cases=[('nms-auto',1712,960,None),('ef-one',1704,958,'1'),
       ('ef-zero',1704,958,'0'),('ef-two',1704,958,'2'),
       ('ef-half',1704,958,'0.5')]
results=[]
for name,rw,rh,exposure in cases:
 log=out/(name+'.log'); image=out/(name+'.rgba16f'); bundle=assets/'general/2160/quality'
 env=os.environ.copy(); env.update(FSR4_TEST_RENDER_SIZE=f'{rw}x{rh}',FSR4_TEST_OUTPUT_SIZE='2560x1440',
   FSR4_TEST_PRESET='auto',FSR4_TEST_VULKAN_1_1='1',
   VK_LOADER_LAYERS_DISABLE='~implicit~',VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation',
   VK_LOADER_DRIVERS_SELECT='*radeon*',WINEDEBUG='-all')
 env.pop('FSR4_TEST_EXTERNAL_EXPOSURE',None)
 if exposure is not None:env['FSR4_TEST_EXTERNAL_EXPOSURE']=exposure
 if mode=='native':
  env.update(FSR4_VK_ASSET_ROOT=str(assets),VK_LAYER_PATH=str(sdk/'share/vulkan/explicit_layer.d'),
   LD_LIBRARY_PATH=str(sdk/'lib')+':'+str(sdk/'lib/VulkanLoader/lib'),FSR4_VK_LOG_PATH=str(out/(name+'.provider.log')))
  command=[str(root/'preset-pr1161'),str(bundle),str(bundle/'initializers.bin'),str(bundle/'weights.bin'),str(image),'--provider-temporal']
  streamlog=log
 else:
  env.pop('FSR4_VK_ASSET_ROOT',None)
  env.update(WINEPREFIX=str(root/'umu-prefix'),GAMEID='umu-default',STORE='none',
   PROTONPATH='/home/admin/.local/share/Steam/compatibilitytools.d/GE-Proton11-5',PROTONFIXES_DISABLE='1',UMU_RUNTIME_UPDATE='0',
   FSR4_VK_LOG_PATH='Z:'+str(out/(name+'.provider.log')))
  win=lambda p:'Z:'+str(p).replace('/','\\')
  args=[win(root/'preset-dispatch-smoke.exe'),win(bundle),win(bundle/'initializers.bin'),win(bundle/'weights.bin'),win(image),'--provider-temporal']
  command=['umu-run','cmd.exe','/d','/c',subprocess.list2cmdline(args)+' > '+subprocess.list2cmdline([win(log)])+' 2>&1']
  streamlog=out/(name+'.umu.log')
 with streamlog.open('w') as f:
  p=subprocess.run(command,cwd=root,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=min(100,max(1,deadline-time.monotonic())))
 text=log.read_text(errors='replace') if log.exists() else ''
 passed=p.returncode==0 and text.count('preset_frame=')==2 and 'cleanup=complete' in text and 'nonfinite=0' in text and 'VUID-' not in text and 'Validation Error' not in text
 entry=dict(case=name,exit=p.returncode,passed=passed,reports=[s for s in text.splitlines() if s.startswith(('preset_frame=','output_','cleanup='))])
 if image.exists():entry['sha256']=hashlib.sha256(image.read_bytes()).hexdigest()
 results.append(entry); (out/'results.json').write_text(json.dumps(results,indent=2)+'\n'); print(json.dumps(entry),flush=True)
 if not passed:raise SystemExit(1)
