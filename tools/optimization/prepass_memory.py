#!/usr/bin/env python3
"""Offline audit of the retained RADV pre-pass weight loads, not a traffic profiler.

This deliberately understands only the reviewed NIR scalar/vector argument ABI
and integer address expressions. Unknown layouts fail closed. It does not modify
shaders, execute contributor tools, or infer cache misses from instruction counts.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inspect_nir(text):
    if 'workgroup_size: 256, 1, 1' not in text or 'api_subgroup_size: 64' not in text:
        raise ValueError('unreviewed workgroup/subgroup configuration')
    definitions = {}
    widths = {}
    for line in text.splitlines():
        match = re.match(r'\s*(?:con|div)\s+(\d+(?:x\d+)?)\s+%(\d+) = (.*)', line)
        if match:
            width, ident, expression = match.groups()
            if ident in definitions:
                raise ValueError('duplicate SSA definition')
            definitions[ident] = expression
            widths[ident] = width

    def args(expression):
        return re.findall(r'%(\d+)', expression)

    def scalar_arg(ident, base):
        return definitions[ident].startswith('@load_scalar_arg_amd ') and bool(
            re.search(r'\bbase=' + str(base) + r'[,)]', definitions[ident]))

    bases = []
    for ident, expression in definitions.items():
        refs = args(expression)
        if expression.startswith('pack_64_2x32_split ') and len(refs) == 2:
            if scalar_arg(refs[0], 3) and scalar_arg(refs[1], 4):
                bases.append(ident)
    if len(bases) != 1:
        raise ValueError('missing or ambiguous reviewed weight pointer')

    def evaluate(ident, lane, visiting=None):
        visiting = set() if visiting is None else visiting
        if ident in visiting:
            raise ValueError('cyclic address expression')
        visiting = visiting | {ident}
        expression = definitions[ident]
        constant = re.fullmatch(r'load_const \((0x[0-9a-f]+)(?: = [^)]*)?\)', expression)
        if constant:
            return int(constant[1], 16)
        if expression.startswith('@load_vector_arg_amd (base=12,'):
            return lane
        opcode = expression.split()[0]
        refs = args(expression)
        if opcode not in ('iand', 'ior', 'ishl', 'ushr', 'iadd') or len(refs) != 2:
            raise ValueError('unsupported address expression: ' + expression)
        a, b = (evaluate(ref, lane, visiting) for ref in refs)
        if opcode in ('ishl', 'ushr') and not 0 <= b < 32:
            raise ValueError('invalid shift')
        if opcode == 'iand': result = a & b
        elif opcode == 'ior': result = a | b
        elif opcode == 'ishl': result = a << b
        elif opcode == 'ushr': result = a >> b
        else: result = a + b
        return result & 0xffffffff

    loads = []
    for ident, expression in definitions.items():
        if not expression.startswith('@load_global_amd '):
            continue
        refs = args(expression)
        if not refs or refs[0] != bases[0]:
            continue
        if widths[ident] != '32x4' or len(refs) != 2 or '(base=0,' not in expression:
            raise ValueError('unreviewed weight load width/base')
        offsets = [evaluate(refs[1], lane) for lane in range(256)]
        if any(offset % 16 or offset + 16 > 1024 for offset in offsets):
            raise ValueError('weight load outside the reviewed 1 KiB table')
        loads.append(dict(ssa=ident, offsets=offsets))
    if len(loads) != 16:
        raise ValueError('expected 16 reviewed weight vector loads')
    for lane in range(256):
        expected = [block * 64 + (lane % 4) * 16 for block in range(16)]
        if [load['offsets'][lane] for load in loads] != expected:
            raise ValueError('unreviewed lane weight mapping')
    return dict(workgroup_size=256, subgroup_size=64,
                weight_load_instructions=16, bytes_per_vector=16,
                logical_weight_bytes_per_active_lane=256,
                unique_weight_table_bytes=1024,
                unique_addresses_per_instruction_per_full_wave=4,
                lanes_per_address_per_full_wave=16,
                address_formula='64 * block + 16 * (local_lane % 4); block=0..15',
                loads=[dict(ssa=load['ssa'], distinct_offsets=sorted(set(load['offsets'])))
                       for load in loads],
                warning='Static logical loads, not measured VRAM/PCIe traffic or cache misses. '
                        'Applies to active lanes reaching this block only.')


def audit(capture, bundle):
    manifest = json.loads((bundle / 'manifest.json').read_text())
    matches = [s for s in manifest['shaders'] if s['file'] == 'pass-01.spv']
    if len(matches) != 1:
        raise ValueError('ambiguous pre-pass manifest entry')
    module = capture / 'pass-1-input.spv'
    if sha(module) != matches[0]['sha256'] or sha(bundle / 'pass-01.spv') != sha(module):
        raise ValueError('captured module/asset/manifest mismatch')
    nir = capture / 'pass-1-exec-0-ir-0.txt'
    isa = capture / 'pass-1-exec-0-ir-2.txt'
    result = inspect_nir(nir.read_text().rstrip('\0'))
    result.update(shader_sha256=sha(module), nir_sha256=sha(nir), isa_sha256=sha(isa),
                  weights_sha256=sha(bundle / 'weights.bin'), preset=manifest['preset'],
                  capacity=manifest['capacity'],
                  isa_global_load_dwordx4=len(re.findall(r'^\s*global_load_dwordx4\b', isa.read_text(), re.M)))
    if (bundle / 'weights.bin').stat().st_size != 1024:
        raise ValueError('unexpected weight table size')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    report = audit(args.capture, args.bundle)
    with args.output.open('x') as out:
        json.dump(report, out, indent=2)
        out.write('\n')


if __name__ == '__main__':
    main()
