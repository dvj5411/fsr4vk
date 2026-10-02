#!/usr/bin/env python3
"""Join fresh resource-probe statistics to retained, manifest-verified SPIR-V."""
import argparse
import hashlib
import json
from pathlib import Path


def convert(log, inputs, manifest):
    rows = [json.loads(line) for line in log.read_text().splitlines() if line.strip()]
    if not rows or rows[-1] != {'kind': 'complete', 'pipelines': 15, 'gpu_dispatches': 0}:
        raise ValueError('incomplete resource capture')
    devices = [r for r in rows if r['kind'] == 'device']
    if len(devices) != 1:
        raise ValueError('expected one device identity')
    expected = {r['file']: r['sha256'] for r in json.loads(manifest.read_text())['shaders']}
    shaders = []
    for pass_id in range(15):
        records = [r for r in rows if r.get('pass') == pass_id]
        captured = [r for r in records if r['kind'] == 'input']
        compiled = [r for r in records if r['kind'] == 'compile']
        executable = [r for r in records if r['kind'] == 'executable']
        name = f'pass-{pass_id}-input.spv'
        if len(captured) != 1 or Path(captured[0]['file']).name != name:
            raise ValueError('missing or ambiguous retained input')
        if len(compiled) != 1 or len(executable) != 1 or executable[0]['index'] != 0:
            raise ValueError('expected one compiled executable per pass')
        digest = hashlib.sha256((inputs/name).read_bytes()).hexdigest()
        if digest != expected[f'pass-{pass_id:02}.spv']:
            raise ValueError('captured module differs from selected manifest')
        stats = {}
        descriptions = {}
        for r in records:
            if r['kind'] != 'stat':
                continue
            if r['index'] != 0 or r['name'] in stats:
                raise ValueError('ambiguous statistic')
            stats[r['name']] = r['value']
            descriptions[r['name']] = {'format': r['format'], 'description': r['description']}
        if not stats:
            raise ValueError('missing driver statistics')
        stats['actual_wave'] = executable[0]['actual_wave']
        shaders.append({'sha256': digest, 'pass': pass_id, 'stats': stats,
                        'stat_descriptions': descriptions, 'requested_wave': compiled[0]['requested_wave']})
    d = devices[0]
    return {'gpu': d['name'], 'driver': d['driver']+' / '+d['driver_info'],
            'scope': 'compile-only, statistics and internal representations enabled; not timing',
            'capture_log_sha256': hashlib.sha256(log.read_bytes()).hexdigest(), 'shaders': shaders}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('log', type=Path)
    p.add_argument('inputs', type=Path)
    p.add_argument('manifest', type=Path)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    report = convert(a.log, a.inputs, a.manifest)
    with a.output.open('x') as out:
        json.dump(report, out, indent=2)
        out.write('\n')


if __name__ == '__main__':
    main()
