import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import build_model11_guard as guard
from compare_pass_captures import compare


def instruction(op, *args):
    return [((len(args)+1) << 16) | op, *args]


class GuardTests(unittest.TestCase):
    def fixture(self, barrier=False):
        # Structural fixture for the injector, not a GPU-valid replacement shader.
        words = [0x07230203, 0x00010300, 0, 110, 0]
        words += instruction(71, 17, 11, 28)
        words += instruction(43, 6, 95, 480)
        words += instruction(16, 4, 17, 64, 1, 1)
        words += instruction(54, 1, 4, 0, 2)
        words += instruction(248, 5)
        words += instruction(249, 99)
        words += instruction(248, 99)
        if barrier:
            words += instruction(224, 1, 1, 1)
        words += instruction(253) + instruction(56)
        return struct.pack('<%dI' % len(words), *words)

    def test_rejects_unreviewed_module(self):
        with self.assertRaisesRegex(ValueError, 'not the reviewed'):
            guard.guarded(self.fixture())

    def test_injection_preserves_original_instructions(self):
        data = self.fixture()
        with patch.object(guard, 'EXPECTED', hashlib.sha256(data).hexdigest()):
            result = guard.guarded(data)
        before = list(struct.unpack('<%dI' % (len(data)//4), data))
        after = list(struct.unpack('<%dI' % (len(result)//4), result))
        at = 5 + 4 + 4 + 6 + 5 + 2  # immediately after the entry label
        self.assertEqual(len(after)-len(before), 26)
        inserted = after[at:at+26]
        self.assertEqual(inserted[:4], instruction(61, 15, 110, 17))
        self.assertIn((5 << 16) | 176, inserted)  # unsigned x < 480
        self.assertIn((1 << 16) | 253, inserted)  # rejected lane returns
        self.assertEqual(after[3], before[3]+5)
        del after[at:at+26]
        after[3] -= 5
        self.assertEqual(after, before)

    def test_rejects_workgroup_barrier(self):
        data = self.fixture(barrier=True)
        with patch.object(guard, 'EXPECTED', hashlib.sha256(data).hexdigest()):
            with self.assertRaisesRegex(ValueError, 'barrier'):
                guard.guarded(data)

    def test_manifest_must_include_every_pass_once(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root/'manifest.json').write_text(json.dumps({
                'preset': 'quality', 'capacity': [1920, 1080], 'shaders': []}))
            with self.assertRaisesRegex(ValueError, '15 canonical'):
                guard.build(root, root/'out')
            self.assertFalse((root/'out').exists())

    def test_excess_lanes_alias_valid_rows(self):
        def address(x, y, subx, suby):
            return 12441600 + (2*y+suby)*15360 + (2*x+subx)*16
        self.assertEqual(address(480, 0, 0, 0), address(0, 0, 0, 1))
        self.assertEqual(address(480, 0, 0, 1), address(0, 1, 0, 0))
        invalid = {address(x, 0, sx, 0) for x in range(480, 512) for sx in (0, 1)}
        valid = {address(x, 0, sx, 1) for x in range(32) for sx in (0, 1)}
        self.assertEqual(invalid, valid)
        self.assertEqual(len(invalid), 64)


class CaptureTests(unittest.TestCase):
    def test_capture_comparison_tracks_first_divergence(self):
        names = ['uploaded-inputs.bin', 'constants.bin', 'exposure.bin', 'cleared.scratch']
        names += [f'pass-{i}.scratch' for i in range(15)]
        names += ['pre-reprojected.rgba16f']
        with tempfile.TemporaryDirectory() as temp:
            a, b = Path(temp)/'a', Path(temp)/'b'
            a.mkdir(); b.mkdir()
            for name in names:
                (a/name).write_bytes(b'abc')
                (b/name).write_bytes(b'abc')
            self.assertTrue(all(r['identical'] for r in compare(a, b)))
            (b/'pass-12.scratch').write_bytes(b'abd')
            changed = [r for r in compare(a, b) if not r['identical']]
            self.assertEqual(len(changed), 1)
            self.assertEqual(changed[0]['file'], 'pass-12.scratch')
            self.assertEqual(changed[0]['first_different_byte'], 2)
            (b/'pass-12.scratch').write_bytes(b'ab')
            with self.assertRaisesRegex(ValueError, 'size mismatch'):
                compare(a, b)


if __name__ == '__main__':
    unittest.main()
