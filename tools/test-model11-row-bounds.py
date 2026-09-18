#!/usr/bin/env python3
"""Exercise the actual C++ row guard against every shipped model-11 variant."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--probe',type=Path,required=True)
    p.add_argument('--assets',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--spirv-val',type=Path)
    a=p.parse_args();a.probe=a.probe.resolve();a.output.mkdir(parents=True,exist_ok=False)
    report=[]
    for backend in ('general','portable'):
        for tier in ('1080','2160'):
            for preset in ('native','quality','balanced','performance','ultraperf','drs'):
                src=a.assets/backend/tier/preset/'pass-12.spv'
                raw=src.read_bytes();before=list(struct.unpack('<%dI'%(len(raw)//4),raw))
                manifest=json.loads(src.with_name('manifest.json').read_text())
                expected=next(s['sha256'] for s in manifest['shaders'] if s['file']=='pass-12.spv')
                assert hashlib.sha256(raw).hexdigest()==expected
                dst=a.output/f'{backend}-{tier}-{preset}.spv'
                result=subprocess.check_output([str(a.probe),str(src),str(dst)],text=True).strip()
                data=dst.read_bytes();after=list(struct.unpack('<%dI'%(len(data)//4),data))
                if tier=='2160':
                    assert result=='aligned-unchanged' and data==raw
                else:
                    assert result=='guarded' and len(after)==len(before)+26
                    assert after[3]==before[3]+5
                    i=5
                    while not ((before[i]&65535)==54 and before[i+2]==4):i+=before[i]>>16
                    at=i+(before[i]>>16)+2 # main function followed by entry label
                    assert after[at:at+4]==[(4<<16)|61,15,before[3],17]
                    restored=after[:at]+after[at+26:];restored[3]-=5
                    assert restored==before # no arithmetic/weights/address edits
                    # A second injection or wrong-stage module must fail closed.
                    assert subprocess.run([str(a.probe),str(dst),str(a.output/'rejected.spv')],
                                          capture_output=True).returncode!=0
                if a.spirv_val:
                    for target in ('vulkan1.1','vulkan1.3'):
                        subprocess.run([str(a.spirv_val),'--target-env',target,str(dst)],check=True)
                report.append(dict(backend=backend,tier=tier,preset=preset,status=result,
                    original_sha256=expected,guarded_sha256=hashlib.sha256(data).hexdigest()))
    source=a.assets/'general/1080/quality/pass-12.spv'
    words=list(struct.unpack('<%dI'%(source.stat().st_size//4),source.read_bytes()))
    # Reject corrupt header, unexpected row width/local size, and truncated instruction.
    negatives=[b'',b'bad',struct.pack('<%dI'%len(words),0,*words[1:])]
    for opcode,operands,replacement in [(43,[6,95,480],481),(16,[4,17,64,1,1],32)]:
        copy=words[:];i=5
        while i<len(copy):
            n=copy[i]>>16
            if copy[i]&65535==opcode and copy[i+1:i+n]==operands:
                copy[i+(3 if opcode==16 else n-1)]=replacement;break
            i+=n
        else:raise AssertionError('missing mutation target')
        negatives.append(struct.pack('<%dI'%len(copy),*copy))
    negatives.append(source.read_bytes()+struct.pack('<I',5<<16|43))
    for i,data in enumerate(negatives):
        src=a.output/f'invalid-{i}.bin';src.write_bytes(data)
        assert subprocess.run([str(a.probe),str(src),str(a.output/'rejected.spv')],capture_output=True).returncode!=0
    wrong=a.assets/'general/1080/quality/pass-11.spv'
    assert subprocess.run([str(a.probe),str(wrong),str(a.output/'rejected.spv')],capture_output=True).returncode!=0
    (a.output/'results.json').write_text(json.dumps(dict(variants=report,negative_cases=len(negatives)+1),indent=2)+'\n')
    print('24 variants passed; 12 guarded, 12 byte-identical; malformed/wrong-stage/double-patch rejection passed.')


if __name__=='__main__':main()
