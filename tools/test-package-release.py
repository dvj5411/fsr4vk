#!/usr/bin/env python3
"""Check normal-only packaging and rejection gates, including Python -O."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PROVIDER_EXPORTS = ['ffxCreateContext', 'ffxDestroyContext', 'ffxConfigure', 'ffxQuery', 'ffxDispatch']
HOST_EXPORTS = ['CreateDXGIFactory', 'CreateDXGIFactory1', 'CreateDXGIFactory2']


def dll(exports):
    data = bytearray(0x1200)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 60, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HH', data, 0x84, 0x8664, 1)
    struct.pack_into('<HH', data, 0x94, 240, 0x2000)
    optional = 0x98
    struct.pack_into('<H', data, optional, 0x20b)
    struct.pack_into('<I', data, optional + 60, 0x200)
    struct.pack_into('<I', data, optional + 108, 16)
    struct.pack_into('<II', data, optional + 112, 0x1000, 0x300)
    section = optional + 240
    data[section:section + 8] = b'.rdata\0\0'
    struct.pack_into('<IIII', data, section + 8, 0x1000, 0x1000, 0x1000, 0x200)
    struct.pack_into('<IIIIII', data, 0x200 + 16, 1, len(exports), len(exports), 0x1100, 0x1140, 0x1180)
    position = 0x1200
    for index, name in enumerate(exports):
        struct.pack_into('<I', data, 0x300 + index * 4, 0x1900 + index)
        struct.pack_into('<I', data, 0x340 + index * 4, position)
        struct.pack_into('<H', data, 0x380 + index * 2, index)
        text = name.encode() + b'\0'
        offset = position - 0x1000 + 0x200
        data[offset:offset + len(text)] = text
        position += len(text)
    return bytes(data)


class PackageTests(unittest.TestCase):
    def command(self, root, output, optimized=False):
        return [sys.executable, *(['-O'] if optimized else []), str(ROOT / 'tools/package-release.py'),
                '--provider', str(root / 'provider.dll'), '--optiscaler', str(root / 'primary.dll'),
                '--output', str(output)]

    def inputs(self, root):
        payloads = {'provider.dll': dll(PROVIDER_EXPORTS), 'primary.dll': dll(HOST_EXPORTS),
                    'amd_fidelityfx_upscaler_vk_debug.dll': dll(PROVIDER_EXPORTS)}
        for name, data in payloads.items():
            (root / name).write_bytes(data)
        return payloads

    def test_normal_only_even_when_debug_exists(self):
        for optimized in (False, True):
            with self.subTest(optimized=optimized), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                payloads = self.inputs(root)
                output = root / 'release.zip'
                subprocess.run(self.command(root, output, optimized), check=True, capture_output=True)
                with zipfile.ZipFile(output) as archive:
                    self.assertEqual(set(archive.namelist()), {
                        'OptiScaler/amd_fidelityfx_upscaler_vk.dll', 'OptiScaler.dll', 'readme.txt',
                        'LICENSES/AMD-FidelityFX-SDK-MIT.md', 'LICENSES/GPL-3.0.txt',
                        'LICENSES/Zstandard-BSD.txt'})
                    self.assertEqual(archive.read('OptiScaler.dll'), payloads['primary.dll'])
                    self.assertEqual(archive.read('OptiScaler/amd_fidelityfx_upscaler_vk.dll'), payloads['provider.dll'])
                    self.assertEqual(archive.read('readme.txt'), (ROOT / 'readme.txt').read_bytes())
                    self.assertFalse(any('debug' in name.lower() for name in archive.namelist()))

    def test_reject_bad_dll_or_wrong_exports(self):
        for optimized in (False, True):
            for name, data in [('provider.dll', b'invalid'), ('provider.dll', b'MZnot-a-PE'),
                               ('provider.dll', dll(HOST_EXPORTS)), ('primary.dll', dll(PROVIDER_EXPORTS))]:
                with self.subTest(optimized=optimized, name=name), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    self.inputs(root)
                    (root / name).write_bytes(data)
                    output = root / 'invalid.zip'
                    self.assertNotEqual(subprocess.run(self.command(root, output, optimized), capture_output=True).returncode, 0)
                    self.assertFalse(output.exists())

    def test_reject_old_debug_argument(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.inputs(root)
            output = root / 'invalid.zip'
            command = self.command(root, output) + ['--provider-debug', str(root / 'amd_fidelityfx_upscaler_vk_debug.dll')]
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertFalse(output.exists())

    def test_existing_output_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.inputs(root)
            output = root / 'existing.zip'
            output.write_bytes(b'keep-existing')
            self.assertNotEqual(subprocess.run(self.command(root, output, True), capture_output=True).returncode, 0)
            self.assertEqual(output.read_bytes(), b'keep-existing')


if __name__ == '__main__':
    unittest.main()
