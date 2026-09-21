#!/usr/bin/env python3
"""Check the two-OptiScaler package layout and input validation."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[1]

class PackageTests(unittest.TestCase):
    def test_both_binaries_and_reject_invalid_dll(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            payloads={'provider.dll':b'MZprovider','primary.dll':b'MZprimary','fallback.dll':b'MZfallback'}
            for name,data in payloads.items():(root/name).write_bytes(data)
            archive=root/'release.zip'
            args=[sys.executable,str(ROOT/'tools/package-release.py'),
                  '--provider',str(root/'provider.dll'),'--optiscaler',str(root/'primary.dll'),
                  '--optiscaler-fallback',str(root/'fallback.dll'),'--output',str(archive)]
            subprocess.run(args,check=True,capture_output=True)
            with zipfile.ZipFile(archive) as z:
                self.assertEqual(set(z.namelist()),{'OptiScaler/amd_fidelityfx_upscaler_vk.dll',
                    'OptiScaler.dll','OptiScaler_fallback.dll','readme.txt',
                    'LICENSES/AMD-FidelityFX-SDK-MIT.md','LICENSES/OptiScaler-GPL-3.0.txt'})
                self.assertEqual(z.read('OptiScaler.dll'),payloads['primary.dll'])
                self.assertEqual(z.read('OptiScaler_fallback.dll'),payloads['fallback.dll'])
                self.assertEqual(z.read('readme.txt'),(ROOT/'release/readme.txt').read_bytes())
            (root/'fallback.dll').write_bytes(b'invalid')
            args[-1]=str(root/'invalid.zip')
            self.assertNotEqual(subprocess.run(args,capture_output=True).returncode,0)
            self.assertFalse((root/'invalid.zip').exists())

if __name__=='__main__':unittest.main()
