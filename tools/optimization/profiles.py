#!/usr/bin/env python3
"""Rank our named-pass CSV/JSONL timings; never align DX12/FP8 passes by index."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics


def unique_object(pairs):
    obj = {}
    for key, value in pairs:
        if key in obj:
            raise ValueError(f'duplicate JSON key: {key}')
        obj[key] = value
    return obj


def valid_time(value):
    if isinstance(value, bool):
        raise ValueError('boolean timestamp')
    value = float(value)
    if not math.isfinite(value) or value <= 0:
        raise ValueError('timestamp must be positive and finite')
    return value


def load(path, warmup=0):
    if path.suffix.lower() == '.csv':
        with path.open(newline='') as stream:
            rows = list(csv.DictReader(stream))
        passes = {}
        for row in rows:
            name = row['pass']
            if not name or name in passes:
                raise ValueError('empty or duplicate pass name')
            passes[name] = valid_time(row['gpu_ms'])
        frames = [{'passes': passes}]
    else:
        frames = [json.loads(line, object_pairs_hook=unique_object)
                  for line in path.read_text().splitlines() if line.strip()]
        if [f.get('frame') for f in frames] != list(range(len(frames))):
            raise ValueError('frames must be contiguous and ordered from zero')
    if not frames or not 0 <= warmup < len(frames):
        raise ValueError('empty input or invalid warmup count')
    if any(frame.get('available') is False for frame in frames):
        raise ValueError('unavailable GPU timestamps are not a timing sample')
    names = set(frames[0]['passes'])
    if not names:
        raise ValueError('no pass timings')
    for frame in frames:
        if set(frame['passes']) != names:
            raise ValueError('pass set changed within run')
        for name in names:
            frame['passes'][name] = valid_time(frame['passes'][name])
        if 'context_gpu_ms' in frame:
            frame['context_gpu_ms'] = valid_time(frame['context_gpu_ms'])
            if sum(frame['passes'].values()) > frame['context_gpu_ms'] + 0.001:
                raise ValueError('pass times exceed enclosing context interval')
    return frames[warmup:]


def distribution(values):
    values = sorted(values)
    return {'samples': len(values), 'median_ms': statistics.median(values),
            'p95_ms': values[math.ceil(len(values)*0.95)-1],
            'min_ms': values[0], 'max_ms': values[-1]}


def summarize(frames):
    rows = [{'pass': name, **distribution([f['passes'][name] for f in frames])}
            for name in frames[0]['passes']]
    rows.sort(key=lambda row: row['median_ms'], reverse=True)
    context = [f['context_gpu_ms'] for f in frames if 'context_gpu_ms' in f]
    if context and len(context) != len(frames):
        raise ValueError('partial context timing series')
    return {'passes': rows, 'context': distribution(context) if context else None,
            'sum_of_pass_medians_ms': sum(r['median_ms'] for r in rows),
            'warning': 'Pass sums are not end-to-end frame time; one CSV is one final-frame sample.'}


def compare(baseline, candidate, meta):
    # Explicit run identities prevent accidental cross-GPU/resolution/preset comparisons.
    required = ('gpu', 'driver', 'backend', 'render_size', 'output_size', 'preset',
                'input_sha256', 'exposure_mode', 'timing_scope')
    for key in required:
        if key not in meta['baseline'] or key not in meta['candidate']:
            raise ValueError(f'missing comparison identity: {key}')
        if meta['baseline'][key] is None or meta['baseline'][key] == '':
            raise ValueError(f'empty comparison identity: {key}')
        if meta['baseline'][key] != meta['candidate'][key]:
            raise ValueError(f'comparison identity differs: {key}')
    for run in ('baseline', 'candidate'):
        if not meta[run].get('build_id'):
            raise ValueError('each comparison run requires a build_id')
    if len(baseline) != len(candidate) or len(baseline) < 2:
        raise ValueError('comparison requires equal counts of at least two samples')
    if [f.get('frame') for f in baseline] != [f.get('frame') for f in candidate]:
        raise ValueError('comparison frame indices differ')
    if set(baseline[0]['passes']) != set(candidate[0]['passes']):
        raise ValueError('pass sets differ; supply a reviewed semantic mapping outside this tool')
    a, b = summarize(baseline), summarize(candidate)
    candidate_by_name = {r['pass']: r for r in b['passes']}
    rows = []
    for old in a['passes']:
        new = candidate_by_name[old['pass']]
        delta = new['median_ms'] - old['median_ms']
        rows.append({'pass': old['pass'], 'baseline_ms': old['median_ms'],
                     'candidate_ms': new['median_ms'], 'delta_ms': delta,
                     'change_percent': delta / old['median_ms'] * 100})
    hashes = all(f.get('output_fnv1a64') for f in baseline + candidate)
    return {'passes': sorted(rows, key=lambda r: r['delta_ms'], reverse=True),
            'baseline_context': a['context'], 'candidate_context': b['context'],
            'output_hashes_match': all(x['output_fnv1a64'] == y['output_fnv1a64']
                                       for x, y in zip(baseline, candidate)) if hashes else None,
            'warning': 'Timing comparison is not a numerical correctness gate or a speedup claim.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('--candidate', type=Path)
    parser.add_argument('--metadata', type=Path)
    parser.add_argument('--warmup', type=int, default=0)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    baseline = load(args.baseline, args.warmup)
    report = {'schema': 1, 'warmup': args.warmup,
              'baseline_sha256': hashlib.sha256(args.baseline.read_bytes()).hexdigest(),
              'summary': summarize(baseline)}
    if args.candidate:
        if not args.metadata:
            parser.error('--candidate requires --metadata')
        meta = json.loads(args.metadata.read_text(), object_pairs_hook=unique_object)
        report['metadata'] = meta
        report['candidate_sha256'] = hashlib.sha256(args.candidate.read_bytes()).hexdigest()
        report['comparison'] = compare(baseline, load(args.candidate, args.warmup), meta)
    with args.output.open('x') as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write('\n')


if __name__ == '__main__':
    main()
