#!/usr/bin/env python3
"""Hash a supplied tool tree without importing scripts or executing binaries."""
import argparse
import hashlib
import json
from pathlib import Path


def catalog(root):
    rows = []
    for path in sorted(root.rglob('*')):
        if path.name == '.DS_Store' or path.name.startswith('._'):
            continue  # Finder metadata is neither supplied source nor stable content.
        if path.is_symlink():
            raise ValueError(f'symlink not permitted: {path}')
        if path.is_file():
            data = path.read_bytes()
            rows.append({'path': path.relative_to(root).as_posix(), 'bytes': len(data),
                         'sha256': hashlib.sha256(data).hexdigest()})
    if not rows:
        raise ValueError('empty tool tree')
    return {'schema': 1, 'files': rows}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    data = catalog(args.root)
    with args.output.open('x') as stream:
        json.dump(data, stream, indent=2)
        stream.write('\n')
