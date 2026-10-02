#!/usr/bin/env python3
"""Final isolated DLL/temporal checks, never modifies a game installation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('root',type=Path)
p.add_argument('--proton',type=Path,required=True)
p.add_argument('--prefix',type=Path,required=True)
p.add_argument('--sdk',type=Path,required=True)
a=p.parse_args();root=a.root.resolve();out=root/'deployment-checks';out.mkdir()
env={k:v for k,v in os.environ.items() if not k.startswith(('FSR4_','VK_','RADV_','ACO_'))}
env.pop('LD_PRELOAD',None)
env.update(WINEPREFIX=str(a.prefix.resolve()),GAMEID='umu-default',STORE='none',
           PROTONPATH=str(a.proton.resolve()),PROTONFIXES_DISABLE='1',UMU_RUNTIME_UPDATE='0',
           VK_LOADER_LAYERS_DISABLE='~implicit~',WINEDEBUG='-all',
           FSR4_VK_LOG_PATH='Z:'+str(out/'nms.provider.log'))
env.update(VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation',
           VK_LAYER_PATH=str(a.sdk/'share/vulkan/explicit_layer.d'),
           LD_LIBRARY_PATH=str(a.sdk/'lib'))
w=lambda path:'Z:'+str(path).replace('/','\\')
def windows(name,args):
    log=out/(name+'.log')
    cmd=subprocess.list2cmdline([w(x) if isinstance(x,Path) else x for x in args])
    cmd+=' > '+subprocess.list2cmdline([w(log)])+' 2>&1'
    with (out/(name+'.umu.log')).open('x') as stream:
        subprocess.run(['umu-run','cmd.exe','/d','/c',cmd],cwd=root,env=env,
                       stdout=stream,stderr=subprocess.STDOUT,check=True,timeout=120)
    text=log.read_text(errors='replace')
    if 'Validation Error' in text or 'VUID-' in text:raise RuntimeError(text)
    return text

windows('abi',[root/'vulkan-provider-abi-probe.exe',root/'amd_fidelityfx_upscaler_vk.dll'])
bundle=root/'assets/general/1080/quality';image=out/'nms.rgba16f'
text=windows('nms',[root/'provider-dispatch-smoke.exe',bundle,bundle/'initializers.bin',
                    bundle/'weights.bin',image,'--provider-nms-formats'])
assert 'context_frames=19' in text and 'overflow=rejected slot_reuse=passed' in text
assert 'nonfinite=0' in text and image.stat().st_size==1920*1080*8
assert 'shader_backend=native-mixed-dot' in (out/'nms.provider.log').read_text()

# Verify the packaged DLL agrees exactly with the native implementation,
# including the case that fails the unmodified-DLL reference threshold.
cases=json.loads((root/'case-sets/windows.json').read_text())['cases']
for case in cases:
    for suffix in ('','.frame0.rgba16f'):
        name=case['case']+'.rgba16f'+suffix
        assert (root/'windows-matrix'/name).read_bytes()==(root/'native-matrix'/name).read_bytes()

# An independent Ultra Performance repeat verifies stability, not oracle accuracy.
bundle=root/'assets/general/1080/ultraperf';image=out/'ultraperf.rgba16f'
native_env=env.copy()
native_env.update(FSR4_VK_ASSET_ROOT=str(root/'assets'),FSR4_TEST_RENDER_SIZE='640x360',
                  FSR4_TEST_OUTPUT_SIZE='1920x1080',FSR4_VK_LOG_PATH=str(out/'ultraperf.provider.log'))
with (out/'ultraperf.log').open('x') as log:
    subprocess.run([str(root/'baseline'),str(bundle),str(bundle/'initializers.bin'),str(bundle/'weights.bin'),
                    str(image),'--provider-temporal'],env=native_env,stdout=log,stderr=subprocess.STDOUT,
                   timeout=90,check=True)
for suffix in ('','.frame0.rgba16f'):
    assert Path(str(image)+suffix).read_bytes()==(root/'native-matrix'/('1080-ultraperf.rgba16f'+suffix)).read_bytes()

series=[]
for i in range(2):
    rows=[json.loads(x) for x in (root/'runtime-guard-runs'/f'1080-{i}.rgba16f.timings.jsonl').read_text().splitlines()]
    assert [r['frame'] for r in rows]==list(range(48))
    series.append([r['output_fnv1a64'] for r in rows])
assert series[0]==series[1]
for suffix in ('','.frame0.rgba16f'):
    assert (root/'runtime-guard-runs'/('1080-0.rgba16f'+suffix)).read_bytes()==(root/'runtime-guard-runs'/('1080-1.rgba16f'+suffix)).read_bytes()
summary=dict(dll_sha256=hashlib.sha256((root/'amd_fidelityfx_upscaler_vk.dll').read_bytes()).hexdigest(),
    abi='passed',nms_format_frames=19,nms_stress='passed',native_windows_identical_images=len(cases)*2,
    quality_repeat_frames=48,quality_repeat_runs=2,quality_repeat_hashes_equal=True,
    ultraperf_reset_and_temporal_repeat_byte_equal=True,
    note='Repeatability and ABI checks; the Ultra Performance original-oracle RMSE failure remains open.')
(out/'results.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary),flush=True)
