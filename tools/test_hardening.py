"""Acceptance-gate regressions, also run by test-codebase.py under Python -O."""
import json
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from pe_resources import PEImage
from asset_compression import frame_content_size

ROOT = Path(__file__).resolve().parents[1]


def fixture():
    data = bytearray(1024)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 60, 64)
    data[64:68] = b'PE\0\0'
    struct.pack_into('<HH', data, 68, 0x8664, 1)
    struct.pack_into('<HH', data, 84, 240, 0x2000)
    struct.pack_into('<H', data, 88, 0x20b)
    struct.pack_into('<I', data, 148, 512)
    struct.pack_into('<I', data, 196, 16)
    struct.pack_into('<II', data, 216, 4096, 512)
    struct.pack_into('<IIII', data, 336, 512, 4096, 512, 512)
    # Type 10 -> resource 100 -> language 0 -> data entry -> payload.
    for relative, name, value in ((0, 10, 0x80000020), (32, 100, 0x80000040), (64, 0, 96)):
        struct.pack_into('<HH', data, 512 + relative + 12, 0, 1)
        struct.pack_into('<II', data, 512 + relative + 16, name, value)
    struct.pack_into('<IIII', data, 608, 4224, 4, 0, 0)
    data[640:644] = b'test'
    return data


class PEGates(unittest.TestCase):
    def test_valid_payload(self):
        self.assertEqual(PEImage(fixture()).rcdata(), {100: b'test'})

    def test_truncations(self):
        original = fixture()
        for size in (0, 60, 64, 90, 200, 400, 512, 650, 1023):
            with self.subTest(size=size), self.assertRaises(ValueError):
                PEImage(original[:size]).rcdata()

    def test_machine_and_magic(self):
        for offset, fmt, value in ((68, '<H', 0x14c), (88, '<H', 0x10b), (60, '<I', 0xffffffff)):
            data = fixture()
            struct.pack_into(fmt, data, offset, value)
            with self.assertRaises(ValueError):
                PEImage(data)

    def test_tree_cycle(self):
        data = fixture()
        struct.pack_into('<I', data, 532, 0x80000000)
        with self.assertRaisesRegex(ValueError, 'cyclic'):
            PEImage(data).rcdata()

    def test_directory_bounds(self):
        data = fixture()
        struct.pack_into('<I', data, 532, 0x800001ff)
        with self.assertRaises(ValueError):
            PEImage(data).rcdata()

    def test_payload_range(self):
        for rva, size in ((4606, 4), (0xffffffff, 4), (4224, 0x80000000)):
            data = fixture()
            struct.pack_into('<II', data, 608, rva, size)
            with self.assertRaises(ValueError):
                PEImage(data).rcdata()

    def test_export_bounds(self):
        data = fixture()
        struct.pack_into('<II', data, 200, 4096 + 200, 40)
        struct.pack_into('<IIIII', data, 512 + 220, 1, 2, 4396, 4400, 4408)
        with self.assertRaises(ValueError):
            PEImage(data).exports()


class ScriptGates(unittest.TestCase):
    def test_freeze_owns_sdk_headers_and_records_tools(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            include = root/'live-sdk/vulkan'
            include.mkdir(parents=True)
            header = include/'vulkan.h'
            header.write_bytes(b'original SDK header')
            stage = root/'stage'
            stage.mkdir()
            subprocess.run([sys.executable, str(ROOT/'tools/freeze-provider-inputs.py'), str(ROOT), str(stage),
                            '--vulkan-include', str(include.parent), '--tool', 'PYTHON='+sys.executable],
                           check=True, stdout=subprocess.DEVNULL, timeout=60)
            header.write_bytes(b'changed live SDK header')
            self.assertEqual((stage/'sdk-include/vulkan/vulkan.h').read_bytes(), b'original SDK header')
            manifest = json.loads((stage/'frozen-inputs.json').read_text())
            self.assertIn('sdk-include/vulkan/vulkan.h', manifest['files'])
            self.assertIn('tools/build-vulkan-provider-windows.sh', manifest['files'])
            self.assertEqual(len(manifest['toolchain']['PYTHON']['sha256']), 64)

    def test_border_metric_ignores_interior_not_edge(self):
        spec = importlib.util.spec_from_file_location('border_metrics', ROOT/'tools/rgba16f-border-metrics.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            left, right = root/'left', root/'right'
            left.write_bytes(struct.pack('<16e', *([0]*16)))
            right.write_bytes(struct.pack('<16e', *([10]*4+[0]*12)))
            result = module.compare(left, right, 2, 2)
            self.assertEqual(result['border_values'], 12)
            self.assertEqual(result['rmse'], 0)
            right.write_bytes(struct.pack('<16e', *([0]*4+[1]*4+[0]*8)))
            self.assertGreater(module.compare(left, right, 2, 2)['rmse'], 0)
            right.write_bytes(struct.pack('<16e', *([0]*4+[float('nan')]*4+[0]*8)))
            self.assertFalse(module.compare(left, right, 2, 2)['valid'])

    def test_zstd_frame_size_fields(self):
        magic = b'\x28\xb5\x2f\xfd'
        for flag, raw, expected in ((32, b'\x01', 1), (96, b'\x01\x00', 257),
                                    (160, (65536).to_bytes(4, 'little'), 65536),
                                    (224, (1 << 32).to_bytes(8, 'little'), 1 << 32)):
            self.assertEqual(frame_content_size(magic + bytes([flag]) + raw), expected)
        self.assertIsNone(frame_content_size(magic + b'\x04\x00'))
        for data in (b'', magic, magic + b'\xa0\x00', magic + b'\x28\x00'):
            with self.assertRaises(ValueError):
                frame_content_size(data)

    def test_profile_stream_selection(self):
        spec = importlib.util.spec_from_file_location('audit_regressions', ROOT/'tools/test-audit-regressions.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            key = '4.0.2-profile-series-new'
            names = (key+'.123456789.0.jsonl', key+'.rgba16f.timings.jsonl',
                     '4x0x2-profile-series-new.123456789.0.jsonl', key+'.csv')
            for name in names:
                (root/name).touch()
            self.assertEqual([path.name for path in module.profile_streams(root, key)], [names[0]])

    def test_manifest_preset_survives_optimization(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            leaf = root / '1080/native'
            leaf.mkdir(parents=True)
            (leaf/'manifest.json').write_text(json.dumps(dict(preset='quality')))
            for flags in ([], ['-O']):
                run = subprocess.run([sys.executable, *flags, str(ROOT/'tools/verify-general-assets.py'), str(root)], capture_output=True)
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(b'wrong preset', run.stderr)

    def test_empty_matrix_survives_optimization(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'cases.json').write_text('{"cases": []}')
            for flags in ([], ['-O']):
                run = subprocess.run([sys.executable, *flags, str(ROOT/'tools/compare-existing-matrix.py'),
                                      '--cases', str(root/'cases.json'), '--candidates', str(root),
                                      '--metrics', 'unused', '--output', str(root/'report.json'),
                                      '--expected-backend', 'portable-int8'], capture_output=True)
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(b'empty case set', run.stderr)
                self.assertFalse((root/'report.json').exists())


class MetricGates(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temporary.name)
        cls.binary = cls.root/'metrics'
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', str(ROOT/'tools/rgba16f-metrics.cpp'),
                        '-o', str(cls.binary)], check=True, timeout=120)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def compare(self, a, b):
        (self.root/'a').write_bytes(struct.pack('<' + 'H' * len(a), *a))
        (self.root/'b').write_bytes(struct.pack('<' + 'H' * len(b), *b))
        run = subprocess.run([str(self.binary), str(self.root/'a'), str(self.root/'b')], capture_output=True)
        return run, json.loads(run.stdout) if run.stdout else None

    def test_finite(self):
        run, result = self.compare([0, 0x3c00, 0x3800, 0], [0, 0x3c00, 0x3800, 0])
        self.assertEqual(run.returncode, 0)
        self.assertEqual(result['rmse'], 0)
        self.assertTrue(result['valid'])

    def test_all_invalid(self):
        run, result = self.compare([0x7c00, 0x7e00, 0xfc00, 0x7e00], [0, 0, 0, 0])
        self.assertEqual(run.returncode, 5)
        self.assertEqual(result['nonfinite_pairs'], 4)
        self.assertIsNone(result['rmse'])
        self.assertFalse(result['valid'])

    def test_finite_denominator(self):
        run, result = self.compare([0x3c00, 0, 0, 0x7e00], [0, 0, 0, 0])
        self.assertEqual(run.returncode, 5)
        self.assertAlmostEqual(result['rmse'], (1/3)**0.5)
        self.assertEqual(result['finite_pairs'], 3)

    def test_shape_and_empty(self):
        for a, b in (([], []), ([0], [0]), ([0]*4, [0]*8)):
            run, _ = self.compare(a, b)
            self.assertNotEqual(run.returncode, 0)


if __name__ == '__main__':
    unittest.main()
