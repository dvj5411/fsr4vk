import copy
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

import audit
import catalog
import profiles


def module(*instructions):
    words = [0x07230203, 0x00010300, 0, 32, 0]
    for op, args in instructions:
        words += [(len(args)+1) << 16 | op, *args]
    return struct.pack(f'<{len(words)}I', *words)


class AuditTests(unittest.TestCase):
    def test_initialized_workgroup_variable(self):
        blob = module((21, [1, 32, 0]), (43, [1, 2, 4]), (28, [3, 1, 2]),
                      (32, [4, 4, 3]), (59, [4, 5, 4, 6]), (224, [2, 2, 2]),
                      (16, [7, 17, 64, 1, 1]))
        row = audit.inspect(blob)
        self.assertEqual(row['declared_workgroup_bytes_unpadded'], 16)
        self.assertEqual(row['local_size'], [[64, 1, 1]])
        self.assertEqual(row['control_barriers'], 1)

    def test_unknown_layout_is_not_zero(self):
        row = audit.inspect(module((30, [1, 2]), (32, [3, 4, 1]), (59, [3, 4, 4])))
        self.assertIsNone(row['declared_workgroup_bytes_unpadded'])
        self.assertIsNotNone(row['declaration_size_error'])

    def test_bad_binary(self):
        for blob in (b'', b'xxxx'*5, module() + b'\0\0\0\0', module() + b'\x01'):
            with self.assertRaises(ValueError):
                audit.inspect(blob)

    def test_no_declared_lds_does_not_report_spills(self):
        row = audit.inspect(module())
        self.assertEqual(row['declared_workgroup_bytes_unpadded'], 0)
        self.assertNotIn('spill_pass_count', row)

    def test_manifest_verification(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            blob = module()
            (root/'pass-00.spv').write_bytes(blob)
            doc = {'shaders': [{'file': 'pass-00.spv', 'sha256': hashlib.sha256(blob).hexdigest()}]}
            (root/'manifest.json').write_text(json.dumps(doc))
            self.assertEqual(audit.audit(root)['shader_paths'], 1)
            doc['shaders'][0]['sha256'] = '0'*64
            (root/'manifest.json').write_text(json.dumps(doc))
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                audit.audit(root)
            doc['shaders'][0]['file'] = '../outside.spv'
            (root/'manifest.json').write_text(json.dumps(doc))
            with self.assertRaisesRegex(ValueError, 'escaping'):
                audit.audit(root)

    def test_driver_stats_identity_and_types(self):
        report = {'shaders': [{'sha256': 'a'*64}, {'sha256': 'b'*64}]}
        driver = {'gpu': 'test', 'driver': 'test', 'shaders': [
            {'sha256': 'a'*64, 'stats': {'ratio': 1.25, 'supported': True, 'Scratch size': 16}}]}
        result = audit.correlate(report, driver)
        self.assertEqual(result['shaders'][0]['driver_stats']['ratio'], 1.25)
        self.assertIsNone(result['shaders'][1]['driver_stats'])
        self.assertNotIn('spill_pass_count', result)
        driver['shaders'][0]['sha256'] = 'c'*64
        with self.assertRaises(ValueError):
            audit.correlate(report, driver)

    def test_catalog_does_not_follow_symlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'test.py').write_text('raise RuntimeError("do not execute")')
            self.assertEqual(len(catalog.catalog(root)['files']), 1)
            (root/'link').symlink_to(root/'test.py')
            with self.assertRaises(ValueError):
                catalog.catalog(root)


class ProfileTests(unittest.TestCase):
    def test_unavailable_native_sample_is_not_zero(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'profile.jsonl'
            path.write_text('{"frame":0,"available":false,"passes":null}\n')
            with self.assertRaisesRegex(ValueError,'unavailable GPU timestamps'):
                profiles.load(path)

    def test_duplicate_json_keys(self):
        with self.assertRaises(ValueError):
            json.loads('{"post":1,"post":2}', object_pairs_hook=profiles.unique_object)

    def setUp(self):
        self.frames = [{'frame': i, 'passes': {'pre': 1.0, 'post': 2.0},
                        'context_gpu_ms': 4.0, 'output_fnv1a64': 'abc'} for i in range(3)]
        identity = dict(gpu='test', driver='test', backend='portable-int8',
                        render_size=[64, 64], output_size=[128, 128], preset='quality',
                        input_sha256='a'*64, exposure_mode='auto', timing_scope='context',
                        build_id='baseline')
        self.meta = {'baseline': identity, 'candidate': dict(identity, build_id='candidate')}

    def test_named_pass_comparison(self):
        candidate = copy.deepcopy(self.frames)
        for frame in candidate:
            frame['passes'] = {'post': 1.5, 'pre': 1.0}
        result = profiles.compare(self.frames, candidate, self.meta)
        self.assertEqual(result['passes'][1]['delta_ms'], -0.5)
        self.assertTrue(result['output_hashes_match'])

    def test_identity_mismatch_and_missing_values(self):
        for change in ('mismatch', 'missing'):
            meta = copy.deepcopy(self.meta)
            if change == 'mismatch':
                meta['candidate']['gpu'] = 'another GPU'
            else:
                del meta['candidate']['gpu']
            with self.assertRaises(ValueError):
                profiles.compare(self.frames, self.frames, meta)

    def test_pass_mismatch(self):
        candidate = copy.deepcopy(self.frames)
        for frame in candidate:
            frame['passes']['other'] = frame['passes'].pop('pre')
        with self.assertRaises(ValueError):
            profiles.compare(self.frames, candidate, self.meta)

    def test_unknown_or_mismatched_pixel_hashes(self):
        candidate = copy.deepcopy(self.frames)
        candidate[0]['output_fnv1a64'] = 'different'
        self.assertFalse(profiles.compare(self.frames, candidate, self.meta)['output_hashes_match'])
        del candidate[0]['output_fnv1a64']
        self.assertIsNone(profiles.compare(self.frames, candidate, self.meta)['output_hashes_match'])

    def test_csv_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'profile.csv'
            path.write_text('pass,gpu_ms\npre,1.25\npost,2\n')
            result = profiles.summarize(profiles.load(path))
            self.assertIsNone(result['context'])
            self.assertEqual(result['passes'][0]['pass'], 'post')
            self.assertEqual(result['passes'][0]['samples'], 1)
            for invalid in ('pre,nan', 'pre,-1', 'pre,0', 'pre,1\npre,2'):
                path.write_text('pass,gpu_ms\n'+invalid+'\n')
                with self.assertRaises(ValueError):
                    profiles.load(path)

    def test_sequence_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'profile.jsonl'
            for change in ('ok', 'order', 'passes', 'interval'):
                frames = copy.deepcopy(self.frames)
                if change == 'order': frames[1]['frame'] = 4
                if change == 'passes': del frames[1]['passes']['post']
                if change == 'interval': frames[1]['context_gpu_ms'] = 1
                path.write_text('\n'.join(json.dumps(f) for f in frames))
                if change == 'ok':
                    self.assertEqual(len(profiles.load(path, 1)), 2)
                else:
                    with self.assertRaises(ValueError): profiles.load(path)


if __name__ == '__main__':
    unittest.main()
