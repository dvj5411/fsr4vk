#!/usr/bin/env python3
"""Validate retained W6400 timing/placement evidence for the production policy."""
import argparse
from collections import Counter
import json
from pathlib import Path
from statistics import median


def summarize(root):
    report = {}
    for case in ('1080', '1440'):
        reference = None
        runs = {'control': [], 'production': []}
        manifests = set()
        for mode in runs:
            for trial in 'abc':
                directory = root/f'production-check-{mode}-{trial}-runs'
                key = case+'-0'
                frames = [json.loads(line) for line in
                          (directory/(key+'.rgba16f.timings.jsonl')).read_text().splitlines()]
                assert [row['frame'] for row in frames] == list(range(48))
                hashes = [row['output_fnv1a64'] for row in frames]
                if reference is None:
                    reference = hashes
                assert hashes == reference, 'timed output mismatch'
                assert 'nonfinite=0' in (directory/(key+'.stdout')).read_text()
                allocations = [json.loads(line) for line in (directory/(key+'.stderr')).read_text().splitlines()
                               if line.startswith('{"kind":"memory_placement"')]
                assert Counter(row['role'] for row in allocations) == {
                    'descriptors': 27, 'frame_constants': 8, 'initializers': 1}
                for allocation in allocations:
                    local = mode=='production' and allocation['role']=='initializers'
                    expected_type = 4 if allocation['role']=='descriptors' else 3 if local else 2
                    assert allocation['selected_type'] == expected_type
                    assert allocation['property_flags'] == (6 if expected_type==2 else 7)
                    assert allocation['memory_type_bits'] & (1<<allocation['selected_type'])
                    assert allocation['result']==0 and not allocation['allocation_fallback']
                    # Research wrappers observe only; production code chooses memory.
                    assert allocation['prefer_local']==0
                proof = (directory/(key+'.provider.log')).read_text()
                assert 'shader_backend=native-mixed-dot' in proof
                if mode=='production':
                    assert 'type=3 flags=7 device_local=1 fallback=0' in proof
                row = next(row for row in json.loads((directory/'summary.json').read_text())
                           if row['case']==key)
                assert row['frames']==48 and row['warmup']==8
                assert row['context_median_ms']==median(frame['context_gpu_ms'] for frame in frames[8:])
                manifests.add((row['asset_root'], row['bundle_manifest_sha256']))
                runs[mode].append(dict(trial=trial, gpu_ms=row['context_median_ms'],
                    model9_ms=row['passes']['model_9'], executable_sha256=row['executable_sha256'],
                    cpu_record_us=median(frame['cpu_record_us'] for frame in frames[8:])))
            assert len({row['executable_sha256'] for row in runs[mode]})==1
        assert len(manifests)==1
        totals = {mode: median(row['gpu_ms'] for row in rows) for mode, rows in runs.items()}
        report[case] = dict(runs=runs, median_ms=totals,
            reduction_percent=100*(1-totals['production']/totals['control']),
            paired_reduction_percent=[100*(1-p['gpu_ms']/c['gpu_ms'])
                                      for c,p in zip(runs['control'],runs['production'])],
            matching_frame_hashes=288)
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args=parser.parse_args()
    report=summarize(args.root)
    with args.output.open('x') as stream:
        json.dump(report, stream, indent=2)
        stream.write('\n')
    print(json.dumps(report, indent=2))
