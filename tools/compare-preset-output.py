#!/usr/bin/env python3
"""Compare RGBA16F outputs without third-party dependencies."""
import array
import json
import math
from pathlib import Path
import struct
import sys


def compare(left, right):
    a, b = array.array('H'), array.array('H')
    a.frombytes(Path(left).read_bytes())
    b.frombytes(Path(right).read_bytes())
    if sys.byteorder != 'little':
        a.byteswap()
        b.byteswap()
    assert len(a) == len(b) and len(a) % 4 == 0
    changed = [0] * 4
    squared = [0.0] * 4
    maximum = [0.0] * 4
    nonfinite = 0
    values = [struct.unpack('<e', struct.pack('<H', i))[0] for i in range(65536)]
    for i, (x, y) in enumerate(zip(a, b)):
        if not math.isfinite(values[x]) or not math.isfinite(values[y]):
            nonfinite += 1
            continue
        if x == y:
            continue
        channel = i % 4
        changed[channel] += 1
        error = abs(values[x] - values[y])
        if not math.isfinite(error):
            nonfinite += 1
        else:
            squared[channel] += error * error
            maximum[channel] = max(maximum[channel], error)
    return dict(left=str(left), right=str(right), changed_rgba=changed,
                rmse_rgba=[math.sqrt(x / (len(a) // 4)) for x in squared],
                max_abs_rgba=maximum, nonfinite_differences=nonfinite)


if __name__ == '__main__':
    print(json.dumps(compare(sys.argv[1], sys.argv[2]), indent=2))
