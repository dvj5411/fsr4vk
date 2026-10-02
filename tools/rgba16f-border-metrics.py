#!/usr/bin/env python3
"""Compare final row/column separately; a whole-image RMSE can hide stale edges."""
import argparse
import json
import math
from pathlib import Path
import struct


def compare(left, right, width, height):
    if width <= 0 or height <= 0 or left.stat().st_size != width*height*8 or right.stat().st_size != width*height*8:
        raise ValueError('border images must match explicit RGBA16F dimensions')
    coordinates = [(x, height-1) for x in range(width)] + [(width-1, y) for y in range(height-1)]
    count = nonfinite = above = 0
    total = maximum = 0.0
    with left.open('rb') as a, right.open('rb') as b:
        for x, y in coordinates:
            offset = (y*width+x)*8
            a.seek(offset); b.seek(offset)
            for p, q in zip(struct.unpack('<4e', a.read(8)), struct.unpack('<4e', b.read(8))):
                count += 1
                if not math.isfinite(p) or not math.isfinite(q):
                    nonfinite += 1
                    continue
                error = abs(p-q)
                total += error*error
                maximum = max(maximum, error)
                above += error > .001
    finite = count-nonfinite
    return dict(border_values=count, nonfinite_pairs=nonfinite, finite_pairs=finite,
                rmse=math.sqrt(total/finite) if finite else None,
                max_abs_error=maximum if finite else None,
                above_tolerance_fraction=above/finite if finite else None,
                valid=not nonfinite)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--width', type=int, required=True)
    parser.add_argument('--height', type=int, required=True)
    args = parser.parse_args()
    result = compare(args.reference, args.candidate, args.width, args.height)
    print(json.dumps(result, allow_nan=False))
    raise SystemExit(0 if result['valid'] else 5)


if __name__ == '__main__':
    main()
