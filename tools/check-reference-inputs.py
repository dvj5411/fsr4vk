#!/usr/bin/env python3
"""Byte-compare both synthetic CPU generators, without Vulkan or Windows.

Compiles the actual function bodies, not a third implementation of the scene.
This checks generator parity on one compiler; it does not validate GPU uploads
or establish cross-compiler floating-point equivalence.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile


def section(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='g++')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    original = (root / 'probes/provider-probe.cpp').read_text()
    native = (root / 'native/dispatch-smoke.cpp').read_text()
    prefix = '''
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <stdexcept>
using UINT = unsigned;
constexpr unsigned kRenderWidth=1280, kRenderHeight=720;
bool g_nmsInputs=true;
struct FrameInputs {
    std::vector<uint16_t> color, motion;
    std::vector<float> depth;
    std::vector<uint8_t> reactive, transparency;
    float jitterX, jitterY;
};
template<class T> void dump(const std::vector<T>& v) {
    if (fwrite(v.data(), sizeof(T), v.size(), stdout)!=v.size()) std::abort();
}
'''
    functions = {
        'original': section(original, 'uint16_t FloatToHalf(', 'float HalfToFloat(')
        + section(original, 'float Halton(', 'bool InitializeTransferBuffer('),
        'native': section(native, 'std::uint16_t float_to_half(', 'float half_to_float(')
        + section(native, 'float reference_jitter(', 'VkDeviceAddress device_address('),
    }
    with tempfile.TemporaryDirectory(prefix='fsr4-input-parity-') as directory:
        outputs = {}
        for name, body in functions.items():
            generate = ('GenerateFrameInputs(i, frame);' if name == 'original' else
                        'generate_reference_frame(frame, i); for(auto& z:frame.depth) z=1.0f-z;')
            jitter = ('frame.jitterX, frame.jitterY' if name == 'original' else
                      'reference_jitter(i,2), reference_jitter(i,3)')
            source = prefix + body + '''
int main() { for(unsigned i=0;i<2;++i) { FrameInputs frame;
''' + generate + '''
dump(frame.color); dump(frame.depth); dump(frame.motion);
std::vector<float> jitter={''' + jitter + '''}; dump(jitter);
} }
'''
            binary = str(Path(directory) / name)
            subprocess.run([args.cxx, '-std=c++20', '-O2', '-x', 'c++', '-', '-o', binary],
                           input=source.encode(), check=True, timeout=60)
            outputs[name] = subprocess.check_output([binary], timeout=30)
            print(name, len(outputs[name]), hashlib.sha256(outputs[name]).hexdigest())
        a, b = outputs.values()
        if a != b:
            differences = sum(x != y for x, y in zip(a, b)) + abs(len(a)-len(b))
            print(f'FAIL: {differences} differing bytes')
            return 1
        print('PASS: both frames have identical color, inverted depth, motion, and jitter bytes')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
