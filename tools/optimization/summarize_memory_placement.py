#!/usr/bin/env python3
"""Check complete constant-placement A/B evidence and summarize paired timings."""
import argparse
import json
from pathlib import Path
import statistics

import profiles


def summarize(root, case='1080'):
    variants={0:[],1:[]}
    all_frames=[]
    for mode in (0,1):
        for trial in ('a','b','c'):
            directory=root/f'mode-{mode}-{trial}-runs'
            records=json.loads((directory/'summary.json').read_text())
            record=next(r for r in records if r['case']==case+'-0')
            frames=profiles.load(directory/(case+'-0.rgba16f.timings.jsonl'))
            if len(frames)!=48: raise ValueError('incomplete frame sequence')
            allocations=[json.loads(line) for line in (directory/(case+'-0.stderr')).read_text().splitlines()
                         if line.startswith('{"kind":"memory_placement"')]
            # Newer wrappers also log unmodified descriptor/initializer roles.
            for allocation in allocations:
                if allocation['role']!='frame_constants' and (
                    allocation['prefer_local'] or allocation['result']!=0 or
                    allocation['selected_type']!=allocation['baseline_type']):
                    raise ValueError('non-constant allocation changed in constants-only experiment')
            allocations=[a for a in allocations if a['role']=='frame_constants']
            if len(allocations)!=8: raise ValueError('unexpected constant allocation count')
            for allocation in allocations:
                if allocation['prefer_local']!=mode or allocation['result']!=0:
                    raise ValueError('wrong mode or allocation failed')
                if allocation['requested_bytes']!=2048 or allocation['role']!='frame_constants':
                    raise ValueError('wrong allocation role')
                if allocation['property_flags'] & 6 != 6:
                    raise ValueError('lost host visibility/coherence')
                if mode and (allocation['property_flags'] & 1 == 0 or allocation['allocation_fallback']):
                    raise ValueError('candidate did not actually use device-local memory')
                if not mode and allocation['selected_type']!=allocation['baseline_type']:
                    raise ValueError('control memory type changed')
            diagnostic=(directory/(case+'-0.provider.log')).read_text()
            if 'shader_backend=native-mixed-dot' not in diagnostic:
                raise ValueError('unexpected shader backend')
            kept=frames[8:]
            cpu=profiles.distribution([profiles.valid_time(f['cpu_record_us'])/1000 for f in kept])
            stats=profiles.summarize(kept)
            if stats['context']['median_ms']!=record['context_median_ms']:
                raise ValueError('summary/frames disagree')
            variants[mode].append(dict(trial=trial,record=record,timing=stats,
                cpu_record_ms=cpu,allocations=allocations))
            all_frames.append(frames)
    records=[r['record'] for runs in variants.values() for r in runs]
    if len({r['asset_root'] for r in records})!=1 or len({r['bundle_manifest_sha256'] for r in records})!=1:
        raise ValueError('asset identity changed')
    reference=[f['output_fnv1a64'] for f in all_frames[0]]
    matching=[sum(x==f['output_fnv1a64'] for x,f in zip(reference,frames)) for frames in all_frames]
    gpu={mode:statistics.median(r['timing']['context']['median_ms'] for r in runs)
         for mode,runs in variants.items()}
    cpu={mode:statistics.median(r['cpu_record_ms']['median_ms'] for r in runs)
         for mode,runs in variants.items()}
    passes={}
    for name in records[0]['passes']:
        values=[statistics.median(r['record']['passes'][name] for r in variants[m]) for m in (0,1)]
        passes[name]=dict(control_ms=values[0],candidate_ms=values[1],delta_ms=values[1]-values[0])
    return dict(case=case,frames_each=48,warmup=8,runs=variants,
        matching_frame_hashes_against_control_a=matching,
        all_frame_hashes_match=all(n==48 for n in matching),
        gpu_median_of_run_medians_ms=gpu,cpu_median_of_run_medians_ms=cpu,
        gpu_change_percent=(gpu[1]/gpu[0]-1)*100,passes=passes,
        paired_gpu_change_percent=[(b['timing']['context']['median_ms']/a['timing']['context']['median_ms']-1)*100
                                   for a,b in zip(variants[0],variants[1])],
        warning='Native isolated individually submitted frames, not sustained game FPS or Windows DLL validation.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root',type=Path)
    parser.add_argument('--case',default='1080')
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    report=summarize(args.root,args.case)
    with args.output.open('x') as out:
        json.dump(report,out,indent=2)
        out.write('\n')
    print(json.dumps({k:v for k,v in report.items() if k!='runs'},indent=2))


if __name__=='__main__': main()
