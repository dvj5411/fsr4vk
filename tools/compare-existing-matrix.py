#!/usr/bin/env python3
"""Compare retained two-frame candidates to an explicitly identified oracle set."""
import argparse
import json
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cases',type=Path,required=True)
p.add_argument('--candidates',type=Path,required=True)
p.add_argument('--metrics',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--expected-backend',required=True,choices=['native-mixed-dot','portable-int8'])
a=p.parse_args()
if a.output.exists():p.error('report exists; refusing overwrite')
results=[]
cases=json.loads(a.cases.read_text())['cases']
if not cases:raise ValueError('empty case set')
for c in cases:
    output=a.candidates/(c['case']+'.rgba16f')
    log=(a.candidates/(c['case']+'.log')).read_text()
    diag=(a.candidates/(c['case']+'.provider.log')).read_text()
    if 'Validation Error' in log or 'VUID-' in log:
        raise ValueError(f"Vulkan validation failed: {c['case']}")
    if 'context_frames=2' not in log or 'shader_backend='+a.expected_backend not in diag:
        raise ValueError(f"missing two-frame/backend proof: {c['case']}")
    if c.get('dynamic_resolution') and 'preset=drs' not in diag:
        raise ValueError(f"missing DRS proof: {c['case']}")
    if c.get('backend',a.expected_backend)!=a.expected_backend:
        raise ValueError(f"oracle backend mismatch: {c['case']}")
    result=dict(c,frames=[],candidate=str(output),backend=a.expected_backend)
    for i,image in enumerate([Path(str(output)+'.frame0.rgba16f'),output]):
        reference=Path(c['runs'][0])/f'frame-{i:03}.rgba16f'
        if image.stat().st_size!=reference.stat().st_size or image.stat().st_size!=c['output'][0]*c['output'][1]*8:
            raise ValueError(f"wrong image byte count: {c['case']} frame {i}")
        metric=json.loads(subprocess.check_output([str(a.metrics),str(reference),str(image)],timeout=60))
        metric.update(frame=i,passed=metric['nonfinite_pairs']==0 and metric['rmse']<=.002 and metric['above_tolerance_fraction']<=.05)
        result['frames'].append(metric)
    result['status']='passed' if all(f['passed'] for f in result['frames']) else 'failed_threshold'
    results.append(result)
    print(c['case'],result['status'],flush=True)
a.output.parent.mkdir(parents=True,exist_ok=True)
with a.output.open('x') as stream:
    stream.write(json.dumps(dict(cases=results),indent=2,allow_nan=False)+'\n')
raise SystemExit(int(any(c['status']!='passed' for c in results)))
