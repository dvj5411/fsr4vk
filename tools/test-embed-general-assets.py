#!/usr/bin/env python3
"""CPU checks for deterministic resource generation and fail-closed packaging."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import shutil

class PackagingTests(unittest.TestCase):
    def test_verified_dedup_and_corruption(self):
        with tempfile.TemporaryDirectory(prefix='fsr4-embed-test-') as temp:
            root = Path(temp)
            general = root/'general'
            # Portable packaging also validates SPIR-V identities and ABI;
            # arbitrary fixture bytes no longer represent valid build inputs.
            assets = Path(__file__).resolve().parents[1]/'assets'
            shutil.copytree(assets/'general', general)
            shutil.copytree(assets/'portable', root/'portable')
            command = [sys.executable,str(Path(__file__).with_name('embed-general-assets.py')),str(general),str(root/'generated')]
            subprocess.run(command,check=True,capture_output=True)
            report = json.loads((root/'generated/embedded-assets.json').read_text())
            self.assertEqual(len(report['entries']),320)
            self.assertEqual(len({e['path'] for e in report['entries']}),320)
            self.assertEqual(len({e['resource_id'] for e in report['entries']}),
                             len({e['sha256'] for e in report['entries']}))
            before = (root/'generated/embedded-asset-index.hpp').read_bytes()
            subprocess.run(command,check=True,capture_output=True)
            self.assertEqual(before,(root/'generated/embedded-asset-index.hpp').read_bytes())
            (general/'1080/native/weights.bin').write_bytes(b'corrupt')
            self.assertNotEqual(subprocess.run(command,capture_output=True).returncode,0)

if __name__ == '__main__': unittest.main()
