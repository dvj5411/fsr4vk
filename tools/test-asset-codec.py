#!/usr/bin/env python3
"""Exercise the actual C++ resource decoder without requiring Vulkan/Windows."""
import argparse,os,subprocess,tempfile
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[1]
zstd=os.environ.get('ZSTD','zstd')
obj=a.output/'zstd.o';probe=a.output/'asset-codec-probe';raw=a.output/'asset-codec-raw-probe'
subprocess.run([os.environ.get('CC','cc'),'-O2','-c',str(root/'third_party/zstd/zstddeclib.c'),'-o',str(obj)],check=True)
for exe,flags in [(probe,['-DFSR4_COMPRESSED_ASSETS',str(obj)]),(raw,[])]:
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O2',str(root/'probes/asset-codec-probe.cpp'),*flags,'-o',str(exe)],check=True)
with tempfile.TemporaryDirectory(prefix='fsr4-codec-') as temporary:
    temp=Path(temporary);original=temp/'input';encoded=temp/'encoded'
    def run(exe,compressed,size,data,expected=None):
        encoded.write_bytes(data)
        result=subprocess.run([str(exe.resolve()),str(int(compressed)),str(size),str(encoded)],capture_output=True)
        if expected is None:assert result.returncode!=0,'corruption was accepted'
        else:assert result.returncode==0 and result.stdout==expected,result.stderr
    for data in (b'x',b'abc'*8192,bytes(range(256))*512):
        original.write_bytes(data)
        compressed=subprocess.check_output([zstd,'-q','-19','--check','--stdout',str(original)])
        for exe in (probe,raw):run(exe,False,len(data),data,data)
        run(probe,True,len(data),compressed,data)
        run(raw,True,len(data),compressed)
        run(probe,True,len(data)+1,compressed)
        run(probe,True,len(data),compressed[:-1])
        run(probe,True,len(data),compressed+b'extra')
        corrupt=bytearray(compressed);corrupt[-1]^=128
        run(probe,True,len(data),corrupt)
        run(probe,True,64*1024*1024+1,compressed)
        run(probe,False,len(data)+1,data)
    run(probe,False,0,b'')
    run(probe,True,16,b'not zstd')
print('CPU resource decoder round-trip, corruption, bounds and raw-debug rejection passed')
