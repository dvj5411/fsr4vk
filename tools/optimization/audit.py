#!/usr/bin/env python3
"""Read-only INT8 bundle audit; SPIR-V declarations are NOT compiler spill data."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import struct


def inspect(blob):
    if len(blob) < 20 or len(blob) % 4:
        raise ValueError('invalid SPIR-V byte length')
    words = struct.unpack(f'<{len(blob)//4}I', blob)
    if words[0] != 0x07230203:
        raise ValueError('invalid SPIR-V magic')
    types, constants, variables, local = {}, {}, [], []
    ops = collections.Counter()
    offset = 5
    while offset < len(words):
        count, op = words[offset] >> 16, words[offset] & 65535
        if not count or offset + count > len(words):
            raise ValueError('invalid SPIR-V instruction length')
        args = words[offset+1:offset+count]
        ops[op] += 1
        if 19 <= op <= 39 and args:
            types[args[0]] = (op, args[1:])
        elif op == 43 and len(args) == 3:  # 32-bit OpConstant only
            constants[args[1]] = args[2]
        elif op == 59 and len(args) >= 3:
            variables.append(args)  # storage class is operand 2, not the last operand
        elif op == 16 and len(args) == 5 and args[1] == 17:
            local.append(list(args[2:]))
        offset += count

    def size(type_id, visiting=frozenset()):
        if type_id in visiting:
            raise ValueError('recursive Workgroup type')
        op, args = types[type_id]
        visiting = visiting | {type_id}
        if op in (21, 22) and args[0] in (8, 16, 32, 64):
            return args[0] // 8
        if op == 23:
            return size(args[0], visiting) * args[1]
        if op == 28:
            return size(args[0], visiting) * constants[args[1]]
        if op == 32:
            return size(args[1], visiting)
        raise ValueError(f'unsupported Workgroup type opcode {op}')

    shared = [v for v in variables if v[2] == 4]
    error = None
    try:
        shared_bytes = sum(size(v[0]) for v in shared)
    except (ValueError, KeyError, IndexError) as exc:
        shared_bytes = None  # unknown is not zero; do not guess struct/spec-constant layout
        error = str(exc)
    return {'sha256': hashlib.sha256(blob).hexdigest(), 'spirv_bytes': len(blob),
            'local_size': local or None, 'workgroup_variables': len(shared),
            'declared_workgroup_bytes_unpadded': shared_bytes,
            'declaration_size_error': error, 'control_barriers': ops[224],
            'memory_barriers': ops[225]}


def audit(root):
    root = root.resolve()
    manifests = sorted(root.rglob('manifest.json'))
    rows = []
    for path in manifests:
        if not path.resolve().is_relative_to(root):
            raise ValueError(f'escaping manifest path: {path}')
        doc = json.loads(path.read_text())
        if 'shaders' not in doc:
            continue
        seen = set()
        for shader in doc['shaders']:
            name = shader['file']
            target = (path.parent / name).resolve()
            if not target.is_relative_to(root) or target in seen:
                raise ValueError(f'escaping or duplicate shader path: {name}')
            seen.add(target)
            row = inspect(target.read_bytes())
            if row['sha256'] != shader['sha256']:
                raise ValueError(f'manifest hash mismatch: {target}')
            rows.append({'shader': target.relative_to(root).as_posix(),
                         'manifest': path.relative_to(root).as_posix(),
                         'preset': doc.get('preset'), 'capacity': doc.get('capacity'), **row})
    if not rows:
        raise ValueError('no manifest shader entries found')
    return {'schema': 1, 'scope': 'static declarations, not final LDS allocation or spills',
            'shader_paths': len(rows), 'unique_shaders': len({r['sha256'] for r in rows}),
            'shaders': rows}


def correlate(report, driver):
    # A filename or pass index is insufficient across presets, tiers and backends.
    if not driver.get('gpu') or not driver.get('driver'):
        raise ValueError('driver report requires gpu and driver identities')
    index = {}
    for row in driver['shaders']:
        key = row['sha256']
        if key in index or len(key) != 64 or any(c not in '0123456789abcdef' for c in key):
            raise ValueError('duplicate or invalid driver shader SHA-256')
        if not isinstance(row['stats'], dict):
            raise ValueError('stats must preserve named driver values')
        index[key] = row['stats']
    unknown = set(index) - {r['sha256'] for r in report['shaders']}
    if unknown:
        raise ValueError('driver report contains shaders outside this audited bundle')
    for row in report['shaders']:
        row['driver_stats'] = index.get(row['sha256'])  # missing remains null, never zero
    report['driver_identity'] = {k: driver[k] for k in ('gpu', 'driver')}
    report['matched_shader_paths'] = sum(r['driver_stats'] is not None for r in report['shaders'])
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assets', type=Path)
    parser.add_argument('--driver-stats', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    report = audit(args.assets)
    if args.driver_stats:
        report = correlate(report, json.loads(args.driver_stats.read_text()))
    with args.output.open('x') as stream:
        json.dump(report, stream, indent=2)
        stream.write('\n')
    print(f"Audited {report['shader_paths']} paths / {report['unique_shaders']} unique shaders")


if __name__ == '__main__':
    main()
