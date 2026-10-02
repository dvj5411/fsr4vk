"""Bounded AMD64 PE/DLL reader for embedded-payload acceptance gates.

All RVA ranges must be file-backed. No assert checks or unchecked recursion.
"""
import struct


class PEImage:
    def __init__(self, data):
        self.data = data
        self.sections = []
        if self.bytes(0, 2) != b'MZ':
            raise ValueError('missing DOS signature')
        pe = self.u32(60)
        if pe < 64 or self.bytes(pe, 4) != b'PE\0\0':
            raise ValueError('invalid PE signature/offset')
        machine, count = self.unpack('<HH', pe + 4)
        optional_size, characteristics = self.unpack('<HH', pe + 20)
        if machine != 0x8664 or not characteristics & 0x2000:
            raise ValueError('expected an AMD64 DLL')
        opt = pe + 24
        self.bytes(opt, optional_size)
        if optional_size < 112 or self.u16(opt) != 0x20b:
            raise ValueError('expected a PE32+ optional header')
        directories = self.u32(opt + 108)
        if directories > 16 or optional_size < 112 + directories * 8:
            raise ValueError('invalid PE data-directory count')
        self.directories = [self.unpack('<II', opt + 112 + i * 8) for i in range(directories)]
        self.header_size = self.u32(opt + 60)
        table = opt + optional_size
        if not 1 <= count <= 96 or self.header_size < table + count * 40:
            raise ValueError('invalid PE section table/header size')
        self.bytes(0, self.header_size)
        for i in range(count):
            pos = table + i * 40
            _, rva, size, offset = self.unpack('<IIII', pos + 8)
            if size:
                self.bytes(offset, size)
                if offset < self.header_size or rva < self.header_size or rva + size > 0x100000000:
                    raise ValueError('invalid section placement')
                for old_rva, old_size, _ in self.sections:
                    if rva < old_rva + old_size and old_rva < rva + size:
                        raise ValueError('overlapping section RVA ranges')
            self.sections.append((rva, size, offset))
        self.machine = machine

    def bytes(self, offset, size):
        if offset < 0 or size < 0 or offset > len(self.data) or size > len(self.data) - offset:
            raise ValueError('PE range outside file bytes')
        return self.data[offset:offset + size]

    def unpack(self, fmt, offset):
        return struct.unpack(fmt, self.bytes(offset, struct.calcsize(fmt)))

    def u16(self, offset):
        return self.unpack('<H', offset)[0]

    def u32(self, offset):
        return self.unpack('<I', offset)[0]

    def rva_offset(self, rva, size):
        if rva < self.header_size and size <= self.header_size - rva:
            self.bytes(rva, size)
            return rva
        for start, length, offset in self.sections:
            if start <= rva and rva - start <= length and size <= length - (rva - start):
                pos = offset + rva - start
                self.bytes(pos, size)
                return pos
        raise ValueError('RVA range is not file-backed')

    def rcdata(self):
        if len(self.directories) <= 2 or not self.directories[2][0]:
            raise ValueError('missing PE resource directory')
        rva, resource_size = self.directories[2]
        base = self.rva_offset(rva, resource_size)
        resources, visited = {}, set()

        def resource_bytes(relative, size):
            if relative < 0 or relative > resource_size or size > resource_size - relative:
                raise ValueError('resource metadata outside directory range')
            return self.bytes(base + relative, size)

        def walk(relative, path):
            if len(path) > 2 or relative in visited or len(visited) >= 4096:
                raise ValueError('cyclic/deep/oversized PE resource tree')
            visited.add(relative)
            header = resource_bytes(relative, 16)
            named, ids = struct.unpack_from('<HH', header, 12)
            entries = resource_bytes(relative + 16, (named + ids) * 8)
            names = set()
            for i in range(named + ids):
                name, value = struct.unpack_from('<II', entries, i * 8)
                if name in names:
                    raise ValueError('duplicate resource directory name')
                names.add(name)
                if name & 0x80000000:
                    string_pos = name & 0x7fffffff
                    length = struct.unpack('<H', resource_bytes(string_pos, 2))[0]
                    resource_bytes(string_pos + 2, length * 2)
                current = path + [name]
                if value & 0x80000000:
                    walk(value & 0x7fffffff, current)
                    continue
                if len(current) != 3:
                    raise ValueError('resource leaf must have type/name/language')
                data_rva, size, _, _ = struct.unpack('<IIII', resource_bytes(value, 16))
                pos = self.rva_offset(data_rva, size)
                if current[0] == 10:
                    rid = current[1]
                    if rid & 0x80000000 or rid in resources or not 0 < size <= 64 * 1024 * 1024:
                        raise ValueError('invalid/duplicate/multilanguage RCDATA payload')
                    resources[rid] = self.bytes(pos, size)
        walk(0, [])
        if not resources:
            raise ValueError('no RCDATA payloads')
        return resources

    def exports(self):
        if not self.directories or not self.directories[0][0]:
            return []
        rva, size = self.directories[0]
        if size < 40:
            raise ValueError('short export directory')
        directory = self.rva_offset(rva, 40)
        functions, names, func_rva, names_rva, ordinals_rva = self.unpack('<IIIII', directory + 20)
        if names > functions or functions > 65536:
            raise ValueError('invalid export counts')
        name_table = self.rva_offset(names_rva, names * 4)
        ordinal_table = self.rva_offset(ordinals_rva, names * 2)
        function_table = self.rva_offset(func_rva, functions * 4)
        exports = []
        for i in range(names):
            ordinal = self.u16(ordinal_table + i * 2)
            if ordinal >= functions or not self.u32(function_table + ordinal * 4):
                raise ValueError('invalid exported function')
            string_rva = self.u32(name_table + i * 4)
            name = bytearray()
            for j in range(1024):
                byte = self.bytes(self.rva_offset(string_rva + j, 1), 1)[0]
                if not byte:
                    break
                name.append(byte)
            else:
                raise ValueError('unterminated export name')
            exports.append(name.decode('ascii'))
        if len(set(exports)) != len(exports):
            raise ValueError('duplicate export names')
        return sorted(exports)
