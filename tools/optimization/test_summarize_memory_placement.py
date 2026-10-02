import json
from pathlib import Path
import tempfile
import unittest

from summarize_memory_placement import summarize


def fixture(root):
    for mode in (0,1):
        for trial in ('a','b','c'):
            directory=root/f'mode-{mode}-{trial}-runs'
            directory.mkdir()
            timing=2.0 if mode==0 else 1.8
            record=dict(case='1080-0',context_median_ms=timing,asset_root='assets',
                        bundle_manifest_sha256='a'*64,passes={'pre':1,'model_11':0.5})
            (directory/'summary.json').write_text(json.dumps([record]))
            frames=[dict(frame=i,context_gpu_ms=timing,cpu_record_us=10+mode,
                         passes=record['passes'],output_fnv1a64=str(i)) for i in range(48)]
            (directory/'1080-0.rgba16f.timings.jsonl').write_text('\n'.join(map(json.dumps,frames)))
            allocation=dict(kind='memory_placement',role='frame_constants',prefer_local=mode,
                result=0,requested_bytes=2048,property_flags=6+mode,allocation_fallback=False,
                selected_type=2+mode,baseline_type=2)
            (directory/'1080-0.stderr').write_text('\n'.join([json.dumps(allocation,separators=(',',':'))]*8))
            (directory/'1080-0.provider.log').write_text('shader_backend=native-mixed-dot\n')


class SummaryTests(unittest.TestCase):
    def test_complete_comparison(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); fixture(root)
            result=summarize(root)
            self.assertAlmostEqual(result['gpu_change_percent'],-10)
            self.assertTrue(result['all_frame_hashes_match'])
            self.assertEqual(result['cpu_median_of_run_medians_ms'][1],0.011)

    def test_rejects_missing_actual_placement(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); fixture(root)
            path=root/'mode-1-a-runs/1080-0.stderr'
            path.write_text(path.read_text().replace('"property_flags":7','"property_flags":6'))
            with self.assertRaisesRegex(ValueError,'actually use'): summarize(root)

    def test_rejects_incomplete_series(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); fixture(root)
            path=root/'mode-1-a-runs/1080-0.rgba16f.timings.jsonl'
            path.write_text('\n'.join(path.read_text().splitlines()[:-1]))
            with self.assertRaisesRegex(ValueError,'incomplete'): summarize(root)

    def test_reports_output_difference(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); fixture(root)
            path=root/'mode-1-a-runs/1080-0.rgba16f.timings.jsonl'
            path.write_text(path.read_text().replace('"output_fnv1a64": "0"','"output_fnv1a64": "bad"'))
            self.assertFalse(summarize(root)['all_frame_hashes_match'])


if __name__=='__main__': unittest.main()
