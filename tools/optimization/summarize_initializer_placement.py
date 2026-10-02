#!/usr/bin/env python3
"""Verify three initializer-only placement pairs and their output/timing evidence."""
import argparse
import collections
import json
from pathlib import Path
import statistics
import profiles


def check_allocations(allocations,candidate):
    expected={'descriptors':27,'frame_constants':8,'initializers':1}
    if collections.Counter(a['role'] for a in allocations)!=expected:
        raise ValueError('unexpected allocation roles/counts')
    for a in allocations:
        changed=a['role']=='initializers' and candidate
        if a['prefer_local']!=int(changed) or a['result']!=0 or a['allocation_fallback']:
            raise ValueError('wrong placement mode or failed allocation')
        if a['property_flags'] & 6 !=6:
            raise ValueError('required memory properties lost')
        if not a['memory_type_bits'] & (1<<a['selected_type']):
            raise ValueError('incompatible selected memory type')
        if changed:
            if not a['property_flags'] & 1 or a['selected_type']==a['baseline_type']:
                raise ValueError('initializer did not move to device-local memory')
        elif a['selected_type']!=a['baseline_type']:
            raise ValueError('unexpected non-target placement change')
        if a['role']=='initializers' and a['requested_bytes']!=89216:
            raise ValueError('unexpected Quality initializer size')


def summarize(root,case):
    runs={};reference=None;identities=set()
    for mode in ('control','initializers'):
        runs[mode]=[]
        for trial in 'abc':
            directory=root/f'repeat-{mode}-{trial}-runs';key=case+'-0'
            records=json.loads((directory/'summary.json').read_text())
            record=next(r for r in records if r['case']==key)
            frames=profiles.load(directory/(key+'.rgba16f.timings.jsonl'))
            if len(frames)!=48 or record['frames']!=48 or record['warmup']!=8:
                raise ValueError('incomplete frame sequence')
            allocations=[json.loads(line) for line in (directory/(key+'.stderr')).read_text().splitlines()
                         if line.startswith('{"kind":"memory_placement"')]
            check_allocations(allocations,mode=='initializers')
            if 'shader_backend=native-mixed-dot' not in (directory/(key+'.provider.log')).read_text():
                raise ValueError('wrong backend')
            if 'nonfinite=0' not in (directory/(key+'.stdout')).read_text():
                raise ValueError('finite-output check missing')
            identities.add((record['asset_root'],record['bundle_manifest_sha256']))
            hashes=[f['output_fnv1a64'] for f in frames]
            if reference is None:reference=hashes
            matched=sum(a==b for a,b in zip(reference,hashes))
            kept=frames[8:]
            gpu=statistics.median(f['context_gpu_ms'] for f in kept)
            if gpu!=record['context_median_ms']:raise ValueError('summary mismatch')
            runs[mode].append(dict(trial=trial,record=record,matching_frame_hashes=matched,
                cpu_record_median_us=statistics.median(f['cpu_record_us'] for f in kept),
                initializer=next(a for a in allocations if a['role']=='initializers')))
    if len(identities)!=1:raise ValueError('assets changed')
    for mode,rows in runs.items():
        if len({r['record']['executable_sha256'] for r in rows})!=1:
            raise ValueError('executable changed within variant')
    totals={m:statistics.median(r['record']['context_median_ms'] for r in rows) for m,rows in runs.items()}
    passes={name:{m:statistics.median(r['record']['passes'][name] for r in rows)
                  for m,rows in runs.items()} for name in runs['control'][0]['record']['passes']}
    return dict(case=case,runs=runs,gpu_median_of_run_medians_ms=totals,passes=passes,
        change_percent=(totals['initializers']/totals['control']-1)*100,
        paired_change_percent=[(b['record']['context_median_ms']/a['record']['context_median_ms']-1)*100
                               for a,b in zip(runs['control'],runs['initializers'])],
        all_frame_hashes_match=all(r['matching_frame_hashes']==48 for rows in runs.values() for r in rows),
        warning='Synthetic native-Linux W6400 experiment, not sustained game throughput or production validation.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('root',type=Path);p.add_argument('--case',required=True,choices=['1080','1440'])
    p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    report=summarize(a.root,a.case)
    with a.output.open('x') as f:json.dump(report,f,indent=2);f.write('\n')
    print(json.dumps({k:v for k,v in report.items() if k!='runs'},indent=2))
