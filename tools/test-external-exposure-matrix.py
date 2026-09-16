#!/usr/bin/env python3
"""Bounded six-preset/two-capacity external-exposure regression on a test host."""
import json,os,subprocess,sys,time
from pathlib import Path
root=Path(sys.argv[1]).resolve(); out=root/sys.argv[2];out.mkdir(exist_ok=False)
sdk=Path(os.environ['VULKAN_SDK']);results=[];deadline=time.monotonic()+480
for tier,ow,oh in [('1080',1920,1080),('2160',3840,2160)]:
 for preset in range(6):
  name=f'{tier}-{preset}';log=out/(name+'.log');image=out/(name+'.rgba16f');bundle=root/'assets/general'/tier/'quality'
  env=os.environ.copy();env.update(FSR4_VK_ASSET_ROOT=str(root/'assets'),FSR4_TEST_RENDER_SIZE=f'{ow*2//3}x{oh*2//3}',
   FSR4_TEST_OUTPUT_SIZE=f'{ow}x{oh}',FSR4_TEST_PRESET=str(preset),FSR4_TEST_EXTERNAL_EXPOSURE='1',
   VK_LAYER_PATH=str(sdk/'share/vulkan/explicit_layer.d'),LD_LIBRARY_PATH=str(sdk/'lib')+':'+str(sdk/'lib/VulkanLoader/lib'),
   VK_LOADER_LAYERS_DISABLE='~implicit~',VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation',VK_LOADER_DRIVERS_SELECT='*radeon*')
  args=[str(root/'preset-pr1161'),str(bundle),str(bundle/'initializers.bin'),str(bundle/'weights.bin'),str(image),'--provider-temporal']
  with log.open('w') as f:p=subprocess.run(args,env=env,cwd=root,stdout=f,stderr=subprocess.STDOUT,timeout=min(100,max(1,deadline-time.monotonic())))
  text=log.read_text(errors='replace')
  passed=p.returncode==0 and text.count('preset_frame=')==2 and 'cleanup=complete' in text and 'nonfinite=0' in text and 'VUID-' not in text and 'Validation Error' not in text
  row=dict(case=name,passed=passed,exit=p.returncode,reports=[s for s in text.splitlines() if s.startswith(('preset_frame=','output_','cleanup='))]);results.append(row)
  (out/'results.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(row),flush=True)
  if not passed:raise SystemExit(1)
