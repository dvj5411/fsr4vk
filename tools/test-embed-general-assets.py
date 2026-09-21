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
    def test_compressed_identity_and_determinism(self):
        with tempfile.TemporaryDirectory(prefix='fsr4-compressed-test-') as temp:
            root=Path(temp)
            assets=Path(__file__).resolve().parents[1]/'assets/general'
            command=[sys.executable,str(Path(__file__).with_name('embed-general-assets.py')),str(assets)]
            subprocess.run(command+[str(root/'raw')],check=True,capture_output=True)
            subprocess.run(command+[str(root/'compressed'),'--compression','zstd'],check=True,capture_output=True)
            raw=json.loads((root/'raw/embedded-assets.json').read_text())
            compressed=json.loads((root/'compressed/embedded-assets.json').read_text())
            identity=lambda report:[(e['path'],e['resource_id'],e['size'],e['sha256']) for e in report['entries']]
            self.assertEqual(identity(raw),identity(compressed))
            self.assertTrue(all(not e['compressed'] and e['size']==e['stored_size'] for e in raw['entries']))
            self.assertTrue(any(e['compressed'] for e in compressed['entries']))
            self.assertLess(sum(e['stored_size'] for e in compressed['entries']),sum(e['size'] for e in raw['entries'])//2)
            before=(root/'compressed/embedded-assets.json').read_bytes()
            subprocess.run(command+[str(root/'compressed'),'--compression','zstd'],check=True,capture_output=True)
            self.assertEqual(before,(root/'compressed/embedded-assets.json').read_bytes())

    def test_verified_dedup_and_corruption(self):
        with tempfile.TemporaryDirectory(prefix='fsr4-embed-test-') as temp:
            root = Path(temp)
            general = root/'general'
            # Portable packaging also validates SPIR-V identities and ABI;
            # arbitrary fixture bytes no longer represent valid build inputs.
            assets = Path(__file__).resolve().parents[1]/'assets'
            shutil.copytree(assets/'general', general)
            shutil.copytree(assets/'portable', root/'portable')
            shutil.copytree(assets/'colors', root/'colors')
            shutil.copytree(assets/'fsr411', root/'fsr411')
            command = [sys.executable,str(Path(__file__).with_name('embed-general-assets.py')),str(general),str(root/'generated')]
            subprocess.run(command,check=True,capture_output=True)
            report = json.loads((root/'generated/embedded-assets.json').read_text())
            self.assertEqual(len(report['entries']),948)
            self.assertEqual(len({e['path'] for e in report['entries']}),948)
            self.assertEqual(sum(e['path'].startswith('fsr411/') for e in report['entries']),420)
            self.assertEqual(len({e['resource_id'] for e in report['entries']}),
                             len({e['sha256'] for e in report['entries']}))
            before = (root/'generated/embedded-asset-index.hpp').read_bytes()
            subprocess.run(command,check=True,capture_output=True)
            self.assertEqual(before,(root/'generated/embedded-asset-index.hpp').read_bytes())
            color=root/'colors/1080/quality/srgb/pass-01.spv'
            saved=color.read_bytes();color.write_bytes(b'corrupt')
            self.assertNotEqual(subprocess.run(command,capture_output=True).returncode,0)
            color.write_bytes(saved)
            new_model=root/'fsr411/general/1080/quality/pass-03.spv'
            saved=new_model.read_bytes();new_model.write_bytes(b'corrupt')
            self.assertNotEqual(subprocess.run(command,capture_output=True).returncode,0)
            new_model.write_bytes(saved)
            manifest=general/'2160/drs/manifest.json'
            manifest_bytes=manifest.read_bytes()
            manifest.unlink()
            self.assertNotEqual(subprocess.run(command,capture_output=True).returncode,0)
            manifest.write_bytes(manifest_bytes)
            (general/'1080/drs/weights.bin').write_bytes(b'corrupt')
            self.assertNotEqual(subprocess.run(command,capture_output=True).returncode,0)

if __name__ == '__main__': unittest.main()
