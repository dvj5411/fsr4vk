#!/usr/bin/env python3
"""Compare the C++ recorder's dispatch plan against original-DLL captures."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('assets', type=Path)
p.add_argument('--vulkan-include', type=Path, required=True)
p.add_argument('--cxx', default='c++')
a = p.parse_args()
repo = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    source = tmp/'plan.cpp'
    source.write_text('''#include "native/fsr411-recorder.hpp"
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=3)return 1;
    unsigned w=std::stoul(argv[1]),h=std::stoul(argv[2]);
    for(unsigned i=0;i<27;++i) {
        auto g=fsr4::fsr411_dispatch(i,w,h,w,h);
        std::cout << g[0] << ' ' << g[1] << '\\n';
    }
}
''')
    subprocess.run([a.cxx,'-std=c++20','-I'+str(repo),'-I'+str(a.vulkan_include),str(source),'-o',str(tmp/'plan')],check=True)
    count = 0
    for manifest in sorted(a.assets.glob('general/*/*/manifest.json')):
        data = json.loads(manifest.read_text())
        width, height = data['provenance']['capacity']
        actual = subprocess.check_output([str(tmp/'plan'),str(width),str(height)],text=True)
        groups = [list(map(int,line.split()))+[1] for line in actual.splitlines()]
        expected = [s['groups'] for s in data['shaders'][1:]]
        if groups != expected:
            raise AssertionError(f'{manifest}: dispatch plan differs from capture: {[(i,x,y) for i,(x,y) in enumerate(zip(groups,expected)) if x!=y]}')
        count += 1
    if not count: raise ValueError('no captured bundles')
    print(f'Matched {count} original-DLL dispatch plans.')
