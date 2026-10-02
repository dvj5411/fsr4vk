import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from captured_stats import convert


class CapturedStatsTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.log = self.root/'probe.jsonl'
        self.manifest = self.root/'manifest.json'
        self.rows = [{'kind': 'device', 'name': 'test GPU', 'driver': 'test', 'driver_info': '1'}]
        shaders = []
        for i in range(15):
            payload = bytes([i])*8
            name = f'pass-{i}-input.spv'
            (self.root/name).write_bytes(payload)
            shaders.append({'file': f'pass-{i:02}.spv', 'sha256': hashlib.sha256(payload).hexdigest()})
            self.rows.extend([
                {'kind': 'input', 'pass': i, 'file': 'remote/path/'+name},
                {'kind': 'compile', 'pass': i, 'requested_wave': 0},
                {'kind': 'executable', 'pass': i, 'index': 0, 'actual_wave': 64},
                {'kind': 'stat', 'pass': i, 'index': 0, 'name': 'fractional metric',
                 'value': 1.5, 'format': 3, 'description': 'driver-defined units'},
            ])
        self.rows.append({'kind': 'complete', 'pipelines': 15, 'gpu_dispatches': 0})
        self.manifest.write_text(json.dumps({'shaders': shaders}))

    def run_capture(self):
        self.log.write_text(''.join(json.dumps(r)+'\n' for r in self.rows))
        return convert(self.log, self.root, self.manifest)

    def test_valid_preserves_typed_statistics(self):
        result = self.run_capture()
        self.assertEqual(len(result['shaders']), 15)
        self.assertEqual(result['shaders'][0]['stats']['fractional metric'], 1.5)
        self.assertEqual(result['shaders'][0]['stats']['actual_wave'], 64)

    def test_rejects_input_mismatch(self):
        (self.root/'pass-1-input.spv').write_bytes(b'wrong shader')
        with self.assertRaisesRegex(ValueError, 'differs'):
            self.run_capture()

    def test_rejects_incomplete_capture(self):
        self.rows.pop()
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            self.run_capture()

    def test_rejects_missing_statistics(self):
        self.rows = [r for r in self.rows if not (r['kind'] == 'stat' and r['pass'] == 2)]
        with self.assertRaisesRegex(ValueError, 'missing driver'):
            self.run_capture()

    def test_rejects_ambiguous_executable(self):
        self.rows.insert(1, {'kind': 'executable', 'pass': 1, 'index': 1, 'actual_wave': 32})
        with self.assertRaisesRegex(ValueError, 'one compiled executable'):
            self.run_capture()


if __name__ == '__main__':
    unittest.main()
