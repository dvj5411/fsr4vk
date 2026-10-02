import unittest
from pathlib import Path
from prepare_wave_size import prepare


class WaveTests(unittest.TestCase):
    def test_actual_source_and_scoping(self):
        source=(Path(__file__).resolve().parents[2]/'native/dispatch-smoke.cpp').read_text()
        result=prepare(source)
        self.assertEqual(result.count('#define vkCreateComputePipelines '),1)
        self.assertLess(result.index('#undef vkCreateComputePipelines'),result.index('int main('))
        self.assertIn('device_info.pNext=&research_subgroups;',result)
        self.assertIn('research_cached_pixels',result)
        with self.assertRaises(ValueError): prepare(result)

    def test_unknown_source(self):
        with self.assertRaises(ValueError): prepare('')


if __name__=='__main__': unittest.main()
