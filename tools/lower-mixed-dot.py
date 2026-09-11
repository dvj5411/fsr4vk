#!/usr/bin/env python3
"""Lower FP16 dot2/FP32 accumulation to core SPIR-V, preserving resource ABI."""
import argparse
from pathlib import Path
import struct


def instruction(opcode, *operands):
    return [((len(operands) + 1) << 16) | opcode, *operands]


def lower(raw):
    if len(raw) < 20 or len(raw) % 4:
        raise ValueError("invalid SPIR-V byte length")
    words = list(struct.unpack(f"<{len(raw)//4}I", raw))
    if words[0] != 0x07230203:
        raise ValueError("invalid SPIR-V magic")
    instructions = []
    offset = 5
    while offset < len(words):
        count = words[offset] >> 16
        if not count or offset + count > len(words):
            raise ValueError("malformed SPIR-V instruction")
        instructions.append(words[offset:offset + count])
        offset += count
    types = {}
    value_types = {}
    decorated = set()
    for ins in instructions:
        op, a = ins[0] & 0xffff, ins[1:]
        if op in (21, 22, 23):
            types[a[0]] = (op, a[1:])
        if op == 71 and len(a) == 3 and a[1] == 40:
            decorated.add(a[0])
        # Typed results used as dot operands are identified by their known type.
        if len(a) >= 2 and a[0] in types and op not in (21, 22, 23):
            value_types[a[1]] = a[0]
        if op in (6917, 6918) or (op == 17 and a[0] in (6913, 6914, 6915)):
            raise ValueError("unsupported mixed-dot variant")
    if not any((i[0] & 0xffff) == 6916 for i in instructions):
        return raw, 0
    next_id = words[3]

    def fresh():
        nonlocal next_id
        result = next_id
        next_id += 1
        return result

    annotations, new_types, body = [], [], []
    vectors = {a[0]: result for result, (op, a) in types.items() if op == 23 and a[1] == 2}
    count = 0
    for ins in instructions:
        op, a = ins[0] & 0xffff, ins[1:]
        if op == 17 and a[0] == 6912:
            continue
        if op == 10:
            extension = struct.pack(f"<{len(a)}I", *a).split(b"\0", 1)[0]
            if extension == b"SPV_VALVE_mixed_float_dot_product":
                continue
        if op != 6916:
            body.append(ins)
            continue
        if len(a) != 5:
            raise ValueError("invalid mixed-dot operand count")
        f32, result, lhs, rhs, acc = a
        if types.get(f32) != (22, [32]) or value_types.get(acc) != f32:
            raise ValueError("mixed-dot result/accumulator must be FP32")
        input_type = value_types.get(lhs)
        if input_type != value_types.get(rhs) or input_type not in types:
            raise ValueError("mixed-dot input types must match")
        vector = types[input_type]
        if vector[0] != 23 or vector[1][1] != 2 or types.get(vector[1][0]) != (22, [16]):
            raise ValueError("mixed-dot inputs must be FP16x2")
        if f32 not in vectors:
            vectors[f32] = fresh()
            new_types.append(instruction(23, vectors[f32], f32, 2))
        wide_type = vectors[f32]
        wide_lhs, wide_rhs, products, x, y, total = [fresh() for _ in range(6)]
        body.extend([
            instruction(115, wide_type, wide_lhs, lhs),
            instruction(115, wide_type, wide_rhs, rhs),
            instruction(133, wide_type, products, wide_lhs, wide_rhs),
            instruction(81, f32, x, products, 0),
            instruction(81, f32, y, products, 1),
            instruction(129, f32, total, x, y),
            instruction(129, f32, result, total, acc),
        ])
        for target in (products, total, result):
            if target not in decorated:
                # These modules use FloatControls2/FPFastMathDefault, which
                # forbids NoContraction. An explicit empty fast-math mask
                # prevents contraction and reassociation for this arithmetic.
                annotations.append(instruction(71, target, 40, 0))
                decorated.add(target)
        count += 1
    # Annotations precede types; added vector types precede the first function.
    output = words[:5]
    output[3] = next_id
    added_annotations = added_types = False
    for ins in body:
        op = ins[0] & 0xffff
        if not added_annotations and 19 <= op <= 39:
            output.extend(v for item in annotations for v in item)
            added_annotations = True
        if not added_types and op == 54:
            output.extend(v for item in new_types for v in item)
            added_types = True
        output.extend(ins)
    if not added_annotations or not added_types:
        raise ValueError("SPIR-V lacks type/function sections")
    return struct.pack(f"<{len(output)}I", *output), count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data, count = lower(args.source.read_bytes())
    args.output.write_bytes(data)
    print(f"lowered {count} mixed-dot instructions: {args.output}")


if __name__ == "__main__":
    main()
