#pragma once
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace fsr4 {
// The 4.1.1 prepass reduces each 2x2 quad into one packed tensor element.
// Its postpass already leaves an unmatched odd output row/column untouched.
// Do not enter the reduction with inactive quad partners: those shuffle values
// are undefined. Complete quads retain exactly the original arithmetic;
// incomplete edge tensors retain the zero from the per-frame scratch clear.
inline void guard_fsr411_prepass_quads(std::vector<uint32_t>& w) {
    const auto fail=[] {throw std::runtime_error("unsupported 4.1.1 prepass quad-bounds contract");};
    if(w.size()<5 || w[0]!=0x07230203 || w[3]>0xffffff00u)fail();
    bool local=false,x=false,y=false,quad=false;uint32_t u=0;
    size_t function=0,xc=0,yc=0;
    for(size_t i=5;i<w.size();) {
        auto n=w[i]>>16,op=w[i]&65535;
        if(!n || n>w.size()-i)fail();
        if(op==16 && n==6 && w[i+2]==17 && w[i+3]==256 && w[i+4]==1 && w[i+5]==1)local=true;
        if(op==81 && n==5 && w[i+2]==81 && w[i+3]==80 && w[i+4]==0){x=true;u=w[i+1];}
        if(op==81 && n==5 && w[i+2]==84 && w[i+3]==80 && w[i+4]==1)y=true;
        if(op==176 && n==5 && w[i+2]==83 && w[i+3]==71 && w[i+4]==81)xc=i;
        if(op==176 && n==5 && w[i+2]==85 && w[i+3]==72 && w[i+4]==84)yc=i;
        if(op==366)quad=true;
        if(op==54 && !function)function=i;
        i+=n;
    }
    if(!local || !x || !y || !quad || !function || !xc || !yc || !u)fail();
    const auto mask=w[3]++,width=w[3]++,height=w[3]++;
    std::vector<uint32_t> out(w.begin(),w.begin()+5);
    for(size_t i=5;i<w.size();) {
        auto n=w[i]>>16;
        if(i==function)out.insert(out.end(),{(4u<<16)|43,u,mask,0xfffffffeu});
        if(i==xc || i==yc)out.insert(out.end(),{(5u<<16)|199,u,i==xc?width:height,i==xc?81u:84u,mask});
        const auto start=out.size();out.insert(out.end(),w.begin()+i,w.begin()+i+n);
        if(i==xc || i==yc)out[start+4]=i==xc?width:height;
        i+=n;
    }
    w=std::move(out);
}
}
