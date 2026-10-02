#!/usr/bin/env python3
"""Isolated Quality experiment: specialize immutable prepass weights, not math."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

SHADERS={
    'fbeeca312c5fea442d03004412fc33d5ea6bbde7e38199a69dac7326d072ba7b',
    '63021dc68725a664db47f243ffd6d3820010d065f680e79e271605a8a69db62a',
}
WEIGHTS='f125f216cbe7ea37ab621edf9b0c9a5ccc0e969f6a4d1d483c20637bac1fb046'


def specialize(payload,weights):
    if hashlib.sha256(payload).hexdigest() not in SHADERS:
        raise ValueError('not a reviewed native Quality prepass')
    if len(weights)!=1024 or hashlib.sha256(weights).hexdigest()!=WEIGHTS:
        raise ValueError('not the reviewed immutable weight table')
    words=list(struct.unpack('<%dI'%(len(payload)//4),payload))
    instructions=[];definitions={};i=5
    while i<len(words):
        count,op=words[i]>>16,words[i]&65535
        if count==0 or i+count>len(words):raise ValueError('malformed SPIR-V')
        args=words[i+1:i+count]
        instructions.append((i,op,args))
        if op in (43,61,66,124,196,197,199):definitions[args[1]]=(op,args)
        i+=count
    # %522 is lane mod 4, from the reviewed local-index swizzle.
    required={522:(197,[6,522,521,55]),521:(196,[6,521,56,33]),
              55:(199,[6,55,43,33]),56:(199,[6,56,49,33])}
    if any(definitions.get(k)!=v for k,v in required.items()):
        raise ValueError('unexpected lane mapping')
    constants={a[1]:a[2] for _,op,a in instructions if op==43 and a[0]==6}
    sites=[]
    for offset,op,args in instructions:
        if op!=61 or len(args)!=5 or args[0]!=64 or args[3:]!=[2,16]:continue
        chain=definitions.get(args[2])
        if not chain or chain[0]!=66:continue
        a=chain[1]
        if len(a)!=5 or a[0]!=70 or a[3]!=36:continue
        base=definitions.get(a[2])
        if not base or base!=(124,[68,a[2],37]):continue
        index=a[4]
        if index==522:block=0
        else:
            expr=definitions.get(index)
            if not expr or expr[0]!=197 or expr[1][0]!=6 or expr[1][2]!=522:
                raise ValueError('unexpected weight offset')
            block=constants[expr[1][3]]
        sites.append((offset,args[1],block))
    if [b for _,_,b in sites]!=list(range(0,64,4)):
        raise ValueError('expected sixteen ordered weight loads')
    next_id=words[3]
    def fresh():
        nonlocal next_id
        value=next_id;next_id+=1;return value
    def emit(op,*args):return [((len(args)+1)<<16)|op,*args]
    declarations=[];prefix=[]
    bool4=fresh();declarations+=emit(23,bool4,74,4)
    predicates=[]
    for existing in (36,33,313):
        if constants.get(existing)!=len(predicates):raise ValueError('lane constants changed')
        scalar=fresh();vector=fresh()
        prefix+=emit(170,74,scalar,522,existing)
        prefix+=emit(80,bool4,vector,scalar,scalar,scalar,scalar)
        predicates.append(vector)
    data=struct.unpack('<256I',weights)
    values={v:k for k,v in constants.items()}
    replacements={}
    for site,result,block in sites:
        vectors=[]
        for lane in range(4):
            parts=[]
            for value in data[4*(block+lane):4*(block+lane+1)]:
                if value not in values:
                    values[value]=fresh();declarations+=emit(43,6,values[value],value)
                parts.append(values[value])
            vector=fresh();declarations+=emit(44,64,vector,*parts);vectors.append(vector)
        s2,s1=fresh(),fresh()
        replacement=emit(169,64,s2,predicates[2],vectors[2],vectors[3])
        replacement+=emit(169,64,s1,predicates[1],vectors[1],s2)
        replacement+=emit(169,64,result,predicates[0],vectors[0],s1)
        replacements[site]=(prefix if site==sites[0][0] else [])+replacement
    first_function=next(i for i,op,_ in instructions if op==54)
    output=words[:5];output[3]=next_id
    for offset,op,args in instructions:
        if offset==first_function:output+=declarations
        output+=replacements.get(offset,emit(op,*args))
    return struct.pack('<%dI'%len(output),*output)


def build(source,target):
    manifest=json.loads((source/'manifest.json').read_text())
    if manifest['preset']!='quality':raise ValueError('Quality only')
    weights=(source/'weights.bin').read_bytes()
    files={}
    for row in manifest['shaders']:
        name=row['file']
        if Path(name).name!=name:raise ValueError('unexpected filename')
        data=(source/name).read_bytes()
        if hashlib.sha256(data).hexdigest()!=row['sha256']:raise ValueError('shader hash mismatch')
        if name=='pass-01.spv':
            data=specialize(data,weights);row['sha256']=hashlib.sha256(data).hexdigest()
        files[name]=data
    for name,key in [('weights.bin','weights_sha256'),('initializers.bin','initializer_sha256')]:
        data=(source/name).read_bytes()
        if hashlib.sha256(data).hexdigest()!=manifest[key]:raise ValueError('coefficient mismatch')
        files[name]=data
    target.mkdir(parents=True,exist_ok=False)
    for name,data in files.items():(target/name).write_bytes(data)
    (target/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path);p.add_argument('target',type=Path)
    a=p.parse_args();build(a.source,a.target)
