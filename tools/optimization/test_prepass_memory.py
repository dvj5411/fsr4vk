import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import prepass_memory


def fixture():
    lines = ['workgroup_size: 256, 1, 1', 'api_subgroup_size: 64',
             'con 32 %0 = @load_scalar_arg_amd (base=3, other=0)',
             'con 32 %1 = @load_scalar_arg_amd (base=4, other=0)',
             'div 32 %2 = @load_vector_arg_amd (base=12, other=0)',
             'con 32 %3 = load_const (0x00000003)',
             'con 32 %4 = load_const (0x00000004)',
             'div 32 %5 = iand %2, %3',
             'div 32 %6 = ishl %5, %4',
             'con 64 %7 = pack_64_2x32_split %0, %1']
    for block in range(16):
        ident = 10 + block * 3
        lines.extend([f'con 32 %{ident} = load_const (0x{block * 64:08x})',
                      f'div 32 %{ident+1} = ior %6, %{ident}',
                      f'div 32x4 %{ident+2} = @load_global_amd (%7, %{ident+1}) (base=0, access=readonly)'])
    return '\n'.join(lines)


class PrepassMemoryTests(unittest.TestCase):
    def test_address_mapping(self):
        result = prepass_memory.inspect_nir(fixture())
        self.assertEqual(result['weight_load_instructions'], 16)
        self.assertEqual(result['loads'][0]['distinct_offsets'], [0, 16, 32, 48])
        self.assertEqual(result['loads'][-1]['distinct_offsets'], [960, 976, 992, 1008])
        self.assertNotIn('pcie_bytes', result)

    def test_rejects_unreviewed_layouts(self):
        for old, new in [('256, 1, 1', '128, 1, 1'), ('subgroup_size: 64', 'subgroup_size: 32'),
                         ('base=3,', 'base=5,'), ('32x4', '32x2'),
                         ('base=0, access=', 'base=16, access='),
                         ('0x000003c0', '0x00000400'), ('ior %6,', 'imul %6,')]:
            with self.subTest(change=new), self.assertRaises(ValueError):
                prepass_memory.inspect_nir(fixture().replace(old, new))

    def test_rejects_missing_load(self):
        with self.assertRaises(ValueError):
            prepass_memory.inspect_nir('\n'.join(fixture().splitlines()[:-1]))

    def test_rejects_duplicate_definition(self):
        with self.assertRaises(ValueError):
            prepass_memory.inspect_nir(fixture() + '\ncon 32 %0 = load_const (0x0)')

    def test_rejects_cycle(self):
        with self.assertRaises(ValueError):
            prepass_memory.inspect_nir(fixture().replace('iand %2, %3', 'iand %5, %3'))

    def test_rejects_wrong_lane_mapping(self):
        with self.assertRaises(ValueError):
            prepass_memory.inspect_nir(fixture().replace('0x00000003', '0x00000001'))

    def test_capture_manifest_identity_and_weight_size(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            capture, bundle = root/'capture', root/'bundle'
            capture.mkdir()
            bundle.mkdir()
            blob = b'identity-test-module-not-executed'
            module = capture/'pass-1-input.spv'
            module.write_bytes(blob)
            (capture/'pass-1-exec-0-ir-0.txt').write_text(fixture())
            (capture/'pass-1-exec-0-ir-2.txt').write_text('global_load_dwordx4 v[0:3], v4, s[4:5]\n')
            (bundle/'pass-01.spv').write_bytes(blob)
            (bundle/'weights.bin').write_bytes(bytes(1024))
            manifest = {'preset': 'quality', 'capacity': [1920, 1080], 'shaders': [
                {'file': 'pass-01.spv', 'sha256': hashlib.sha256(blob).hexdigest()}]}
            (bundle/'manifest.json').write_text(json.dumps(manifest))
            report = prepass_memory.audit(capture, bundle)
            self.assertEqual(report['capacity'], [1920, 1080])
            module.write_bytes(blob + b'changed')
            with self.assertRaisesRegex(ValueError, 'mismatch'):
                prepass_memory.audit(capture, bundle)
            module.write_bytes(blob)
            (bundle/'weights.bin').write_bytes(bytes(1023))
            with self.assertRaisesRegex(ValueError, 'table size'):
                prepass_memory.audit(capture, bundle)


if __name__ == '__main__':
    unittest.main()
