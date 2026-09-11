"""SPIR-V checks shared by portable bundle import and verification."""
import re
import struct


def instructions(raw):
    if len(raw) < 20 or len(raw) % 4:
        raise ValueError('invalid SPIR-V size')
    words = struct.unpack(f'<{len(raw)//4}I', raw)
    if words[0] != 0x07230203:
        raise ValueError('invalid SPIR-V magic')
    offset = 5
    while offset < len(words):
        size, op = words[offset] >> 16, words[offset] & 65535
        if not size or offset + size > len(words):
            raise ValueError('invalid SPIR-V instruction')
        yield op, words[offset+1:offset+size]
        offset += size


def identity(raw):
    names = re.findall(rb'([0-9a-f]{16})\.dxil\0', raw)
    if len(names) != 1:
        raise ValueError('expected one DXIL identity')
    return names[0].decode()


def resource_abi(raw):
    types, constants, decorations, members, variables, modes = {}, {}, {}, {}, [], []
    for op, a in instructions(raw):
        if 19 <= op <= 39:
            types[a[0]] = (op, a[1:])
        elif op == 43:
            constants[a[1]] = a[2:]
        elif op == 71:
            decorations.setdefault(a[0], {})[a[1]] = a[2:]
        elif op == 72 and a[2] in (4, 5, 7, 35):
            members.setdefault(a[0], []).append(a[1:])
        elif op == 59:
            variables.append(a[:3])
        elif op == 16 and a[1] in (17, 5289, 5290):
            modes.append(a[1:])

    def signature(t):
        op, a = types[t]
        if op in (23, 24, 25, 27, 29):
            args = (signature(a[0]), *a[1:])
        elif op == 28:
            args = (signature(a[0]), constants[a[1]])
        elif op == 30:
            args = tuple(signature(x) for x in a)
        elif op == 32:
            args = (a[0], signature(a[1]))
        else:
            args = a
        return (op, args, tuple(sorted(members.get(t, []))), decorations.get(t, {}).get(6))

    resources = []
    for t, v, storage in variables:
        d = decorations.get(v, {})
        if 33 in d or storage == 9:
            resources.append((d.get(34), d.get(33), storage, signature(t)))
    return sorted(resources, key=repr), sorted(modes)


def validate_pair(original, portable, index):
    if identity(original) != identity(portable):
        raise ValueError('DXIL identity changed')
    if resource_abi(original) != resource_abi(portable):
        raise ValueError('descriptor, push-constant or workgroup ABI changed')
    ops = list(instructions(portable))
    if any(op in (6916, 6917, 6918) or (op == 17 and a[0] in (6912, 6913, 6914, 6915, 6029))
           for op, a in ops):
        raise ValueError('non-portable mixed-dot/float-controls2 instruction or capability')
    if any(x in portable for x in (b'SPV_VALVE_', b'SPV_NV_', b'SPV_KHR_float_controls2')):
        raise ValueError('unexpected vendor/float-controls2 extension')
    if index >= 2 and not any(op == 4450 for op, _ in ops):
        raise ValueError('INT8 dot-product path missing')


def verify(general, portable):
    import hashlib
    import json
    count = 0
    for tier in ('1080', '2160'):
        for preset in ('native', 'quality', 'balanced', 'performance', 'ultraperf'):
            base = general/tier/preset
            leaf = portable/tier/preset
            source = json.loads((base/'manifest.json').read_text())
            manifest = json.loads((leaf/'manifest.json').read_text())
            if manifest['preset'] != preset or manifest['capacity'] != source['capacity']:
                raise ValueError(f'bundle identity mismatch: {leaf}')
            if [e['file'] for e in manifest['shaders']] != [f'pass-{i:02}.spv' for i in range(15)]:
                raise ValueError(f'incomplete portable bundle: {leaf}')
            for i, (entry, original_entry) in enumerate(zip(manifest['shaders'], source['shaders'])):
                original = (base/entry['file']).read_bytes()
                raw = (leaf/entry['file']).read_bytes()
                if hashlib.sha256(raw).hexdigest() != entry['sha256']:
                    raise ValueError(f'portable hash mismatch: {leaf/entry["file"]}')
                if (hashlib.sha256(original).hexdigest() != entry['source_sha256']
                        or entry['source_sha256'] != original_entry['sha256']):
                    raise ValueError(f'original hash mismatch: {base/entry["file"]}')
                validate_pair(original, raw, i)
                count += 1
    return count
