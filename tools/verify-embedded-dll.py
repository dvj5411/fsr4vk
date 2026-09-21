#!/usr/bin/env python3
"""Read actual PE RCDATA and verify every logical asset through our C++ decoder."""
import argparse,hashlib,json,struct,subprocess,tempfile
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--dll',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True)
p.add_argument('--decoder',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();data=a.dll.read_bytes();pe=struct.unpack_from('<I',data,60)[0];opt=pe+24
assert data[pe:pe+4]==b'PE\0\0' and struct.unpack_from('<H',data,opt)[0]==0x20b
table=opt+struct.unpack_from('<H',data,pe+20)[0];sections=[]
for i in range(struct.unpack_from('<H',data,pe+6)[0]):
    pos=table+i*40;vsize,rva,size,offset=struct.unpack_from('<IIII',data,pos+8)
    sections.append((rva,size,offset))
def offset(rva):
    for start,size,pos in sections:
        if start<=rva<start+size:return pos+rva-start
    raise ValueError('invalid PE resource RVA')
base=offset(struct.unpack_from('<I',data,opt+112+2*8)[0]);resources={}
def walk(relative,path):
    directory=base+relative;named,ids=struct.unpack_from('<HH',data,directory+12)
    for i in range(named+ids):
        name,value=struct.unpack_from('<II',data,directory+16+i*8)
        current=path+[name]
        if value&0x80000000:walk(value&0x7fffffff,current)
        elif current[0]==10:
            assert len(current)==3 and current[1] not in resources
            rva,size=struct.unpack_from('<II',data,base+value)
            pos=offset(rva);resources[current[1]]=data[pos:pos+size]
walk(0,[])
manifest=json.loads(a.manifest.read_text());seen={};verified=[]
with tempfile.TemporaryDirectory(prefix='fsr4-pe-check-') as temporary:
    payload=Path(temporary)/'payload'
    for entry in manifest['entries']:
        rid=entry['resource_id'];stored=resources[rid]
        assert len(stored)==entry['stored_size']
        assert hashlib.sha256(stored).hexdigest()==entry['stored_sha256']
        if rid not in seen:
            payload.write_bytes(stored)
            decoded=subprocess.check_output([str(a.decoder.resolve()),str(int(entry['compressed'])),str(entry['size']),str(payload)])
            seen[rid]=(len(decoded),hashlib.sha256(decoded).hexdigest())
        assert seen[rid]==(entry['size'],entry['sha256'])
        verified.append(dict(path=entry['path'],sha256=entry['sha256']))
assert set(resources)==set(seen)
report=dict(dll_sha256=hashlib.sha256(data).hexdigest(),dll_bytes=len(data),
            logical_assets=len(verified),unique_resources=len(seen),compression=manifest['compression'],
            unique_stored_bytes=sum(len(x) for x in resources.values()),verified=verified)
a.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='verified'}))
