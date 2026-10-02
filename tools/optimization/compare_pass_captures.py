#!/usr/bin/env python3
"""Compare exact frame-zero pass captures; do not interpret hashes as RMSE."""
import argparse
import hashlib
import json
from pathlib import Path


def compare(a, b):
    names = ['uploaded-inputs.bin', 'constants.bin', 'exposure.bin', 'cleared.scratch']
    names += [f'pass-{i}.scratch' for i in range(15)]
    names += ['pre-reprojected.rgba16f']
    results = []
    for name in names:
        x, y = (a/name).read_bytes(), (b/name).read_bytes()
        if len(x) != len(y):
            raise ValueError('capture size mismatch: '+name)
        row = {'file': name, 'bytes': len(x), 'identical': x == y,
               'sha256_a': hashlib.sha256(x).hexdigest(), 'sha256_b': hashlib.sha256(y).hexdigest()}
        if x != y:
            row['first_different_byte'] = next(i for i, (v, w) in enumerate(zip(x, y)) if v != w)
        results.append(row)
    return results


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('a', type=Path)
    p.add_argument('b', type=Path)
    p.add_argument('--output', required=True, type=Path)
    args = p.parse_args()
    result = compare(args.a, args.b)
    with args.output.open('x') as out:
        json.dump(result, out, indent=2)
        out.write('\n')
    for row in result:
        print(row['file'], 'identical' if row['identical'] else 'DIFF @ '+str(row['first_different_byte']))
