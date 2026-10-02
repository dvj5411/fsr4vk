import unittest
from pathlib import Path
from prepare_capacity_experiment import transform


class CapacityTests(unittest.TestCase):
    def test_narrow_change(self):
        root=Path(__file__).resolve().parents[2]
        provider=(root/'provider/ffx_vk_provider.cpp').read_text()
        core=(root/'native/quality-context.hpp').read_text()
        p,c=transform(provider,core)
        self.assertEqual(sum(a!=b for a,b in zip(provider.splitlines(),p.splitlines())),1)
        self.assertEqual(sum(a!=b for a,b in zip(core.splitlines(),c.splitlines())),1)
        with self.assertRaises(ValueError):transform(p,c)
    def test_reject_missing(self):
        with self.assertRaises(ValueError):transform('','')
