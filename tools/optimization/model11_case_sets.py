#!/usr/bin/env python3
"""Select retained W6400 oracle cases for the row-bounds regression; no GPU work."""
import argparse
import json
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--project',type=Path,required=True)
p.add_argument('--drs-project',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
stem='amd_fidelityfx_upscaler_dx12-402c-candidate'
native=[]
for ow,oh in [(1280,720),(1920,1080),(2560,1440),(3840,2160)]:
    for preset,num,den in [('quality',3,2),('balanced',17,10),('performance',2,1),('ultraperf',3,1)]:
        label='ultra-performance' if preset=='ultraperf' else preset
        native.append(dict(case=f'{oh}-{preset}',input=[ow*den//num,oh*den//num],output=[ow,oh],
            preset=preset,tier='1080' if oh<=1080 else '2160',
            runs=[str(a.project/'reference-output'/f'resolution-matrix-20260907-{oh}p-{label}-r0'/stem)]))
for index in (0,1,4,5,7,8,9,11,24):
    matches=list((a.project/'reference-output').glob(f'general-edge-{index:02}-*-v1'))
    assert len(matches)==1
    ref=matches[0]/stem;meta=json.loads((ref/'reference.json').read_text())
    rw,rh=meta['render_size'];ow,oh=meta['output_size'];ratio=((ow+7)//8*8)/rw
    preset='ultraperf' if ratio>=2.99 else 'performance' if ratio>=1.99 else 'balanced' if ratio>=1.69 else 'quality' if ratio>=1.49 else 'native'
    native.append(dict(case=f'edge-{index:02}',input=[rw,rh],output=[ow,oh],preset=preset,
                       tier='2160' if ow>1920 or oh>1080 else '1080',runs=[str(ref)]))
drs=json.loads((a.drs_project/'drs-matrix-v1/cases.json').read_text())['cases']
chosen=(5,6,16,20,21,24,28)
native += [drs[i] for i in chosen]
portable_source=json.loads((a.drs_project/'drs-portable-matrix-v1/cases.json').read_text())['cases']
portable=[portable_source[i] for i in chosen]
windows=[c for c in native if c['case'] in ('1080-quality','1080-balanced','1080-performance','1080-ultraperf','edge-01')]
windows += [drs[6],drs[24]]
for name,cases in [('native',native),('portable',portable),('windows',windows)]:
    for case in cases:
        ow,oh=case['output']
        for frame in (0,1):
            assert (Path(case['runs'][0])/f'frame-{frame:03}.rgba16f').stat().st_size==ow*oh*8
    (a.output/(name+'.json')).write_text(json.dumps(dict(cases=cases),indent=2)+'\n')
    print(name,len(cases))
