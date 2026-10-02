import unittest
from pathlib import Path
from prepare_memory_placement import prepare


class PrepareTests(unittest.TestCase):
    def test_scoped_hooks(self):
        source='#include <vulkan/vulkan.h>\n#include "quality-context.hpp"\nint main() {}\n'
        result=prepare(source)
        self.assertEqual(result.count('#define vkAllocateMemory '),1)
        self.assertLess(result.index('#undef vkAllocateMemory'),result.index('int main()'))
        with self.assertRaises(ValueError): prepare(result)

    def test_fail_closed(self):
        for source in ('', '#include <vulkan/vulkan.h>\n',
                       '#include <vulkan/vulkan.h>\n#include "quality-context.hpp"\n#include "quality-context.hpp"\n'):
            with self.assertRaises(ValueError): prepare(source)

    def test_cpu_timing_requires_complete_anchors(self):
        source='#include <vulkan/vulkan.h>\n#include "quality-context.hpp"\n'
        with self.assertRaises(ValueError): prepare(source,True)
        source=(Path(__file__).resolve().parents[2]/'native/dispatch-smoke.cpp').read_text()
        result=prepare(source,True)
        self.assertIn('std::chrono::steady_clock::now()-research_cpu_start',result)
        self.assertIn('cpu_record_us',result)
        self.assertIn('cpu_readback_us',result)
        self.assertEqual(result.count('<< ",\\"cpu_readback_us\\":"'),2)
        cached=prepare(source,True,True)
        self.assertIn('std::memcpy(research_cached_pixels.data(),readback.mapped,output_size)',cached)
        with self.assertRaises(ValueError): prepare(source,False,True)


if __name__=='__main__': unittest.main()
