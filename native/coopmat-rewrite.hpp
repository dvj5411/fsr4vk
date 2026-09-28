#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>

namespace fsr4coop {
// Narrow runtime rewrite of 16 independent constant-weight signed INT8 dot16
// chains. All other instructions remain intact. No floating-point reassociation.
// Requires a full wave32 and 16x16x16 signed INT8 -> signed INT32 KHR matrices.
// Partial/divergent subgroups execute the original dot instructions instead.
struct Instruction { uint32_t op; std::vector<uint32_t> a; };
using Code=std::vector<Instruction>;
inline void emit(Code& code,uint32_t op,std::initializer_list<uint32_t> a) {
    code.push_back({op,a});
}
inline Instruction string_instruction(uint32_t op,const char* text) {
    Instruction i{op,std::vector<uint32_t>((std::strlen(text)+4)/4,0)};
    std::memcpy(i.a.data(),text,std::strlen(text));return i;
}
#include "coopmat-loop.hpp"
inline unsigned rewrite(std::vector<uint32_t>& words) {
    if(words.size()<5 || words[0]!=0x07230203) throw std::runtime_error("invalid coopmat SPIR-V input");
    // This transformer deliberately supports only the existing SPIR-V 1.3 ABI.
    if(words[1]!=0x00010300) return 0;
    Code input,decl,annotations;
    for(size_t at=5;at<words.size();) {
        const auto count=words[at]>>16;
        if(!count || count>words.size()-at) throw std::runtime_error("malformed coopmat SPIR-V input");
        input.push_back({words[at]&65535,{words.begin()+at+1,words.begin()+at+count}});at+=count;
    }
    bool supported_group=false;
    for(const auto& i:input) {
        if(i.op==16 && i.a.size()==5 && i.a[1]==17)
            supported_group=i.a[2]==64 && i.a[3]==1 && i.a[4]==1;
        // Existing shared-memory synchronization or GLSL coherent decorations
        // need a separate memory-model audit before changing their module.
        if(i.op==224 || (i.op==71 && i.a.size()>1 && i.a[1]==23)) return 0;
    }
    if(!supported_group)return 0;
    uint32_t bound=words[3];
    expose_constant_dot_loops(input,bound);
    const auto id=[&] {return bound++;};
    const auto type=[&](uint32_t op,std::vector<uint32_t> tail) {
        for(const auto* list:{&input,&decl})for(const auto& i:*list)
            if(i.op==op && i.a.size()==tail.size()+1 && std::equal(tail.begin(),tail.end(),i.a.begin()+1))return i.a[0];
        auto n=id();tail.insert(tail.begin(),n);decl.push_back({op,tail});return n;
    };
    const auto u=type(21,{32,0}),s=type(21,{32,1}),s8=type(21,{8,1}),b=type(20,{});
    const auto constant=[&](uint32_t t,uint32_t value) {
        for(const auto* list:{&input,&decl})for(const auto& i:*list)
            if(i.op==43 && i.a.size()==3 && i.a[0]==t && i.a[2]==value)return i.a[1];
        auto n=id();emit(decl,43,{t,n,value});return n;
    };
    std::array<uint32_t,33> c{};for(unsigned n=0;n<c.size();++n)c[n]=constant(u,n);
    auto c64=constant(u,64),c128=constant(u,128),c256=constant(u,256),c384=constant(u,384),c512=constant(u,512);
    const auto ptrWG=type(32,{4,u}),ptrPrivate=type(32,{6,u}),ptrInput=type(32,{1,u});
    const auto v4=type(23,{u,4});
    const auto array=type(28,{u,constant(u,1408)}),ptrArray=type(32,{4,array});
    const auto weightsArray=type(28,{u,c64}),weightsPtr=type(32,{6,weightsArray});
    const auto matA=type(4456,{s8,c[3],c[16],c[16],c[0]});
    const auto matB=type(4456,{s8,c[3],c[16],c[16],c[1]});
    const auto matC=type(4456,{s,c[3],c[16],c[16],c[2]});
    auto zero=id();emit(decl,44,{matC,zero,constant(s,0)});
    auto truth=id();emit(decl,41,{b,truth});
    auto scratch=id();emit(decl,59,{ptrArray,scratch,4});
    const auto builtin=[&](uint32_t kind) {
        for(const auto& i:input)if(i.op==71 && i.a.size()==3 && i.a[1]==11 && i.a[2]==kind)return i.a[0];
        auto n=id();emit(decl,59,{ptrInput,n,1});emit(annotations,71,{n,11,kind});return n;
    };
    auto laneVar=builtin(41),subgroupVar=builtin(40);
    std::map<uint32_t,uint32_t> constValue;
    std::map<uint32_t,unsigned> references;
    for(const auto& i:input) {
        if(i.op==43 && i.a.size()==3 && i.a[0]==u)constValue[i.a[1]]=i.a[2];
        for(auto a:i.a)++references[a];
    }
    Code out;unsigned blocks=0;uint32_t originalLabel=0;
    std::map<uint32_t,uint32_t> lastLabel;
    for(size_t at=0;at<input.size();) {
        const auto& instruction=input[at];
        if(instruction.op==248)originalLabel=instruction.a[0];
        bool match=instruction.op==4450 && at+128<=input.size();
        std::array<uint32_t,4> av{};
        std::array<uint32_t,64> weights{};
        std::array<uint32_t,16> results{};
        for(unsigned n=0;match && n<64;++n) {
            const auto& dot=input[at+n*2];const auto& add=input[at+n*2+1];
            match=dot.op==4450 && dot.a.size()==5 && dot.a[0]==u && dot.a[4]==0 &&
                  add.op==128 && add.a.size()==4 && add.a[0]==u && add.a[3]==dot.a[1] &&
                  constValue.count(dot.a[3]) && references[dot.a[1]]==2;
            if(!match)break;
            if(n<4)av[n]=dot.a[2];
            match=dot.a[2]==av[n%4] &&
                (n%4==0 ? constValue.count(add.a[2]) && constValue[add.a[2]]==0 : add.a[2]==input[at+n*2-1].a[1]);
            if(n%4!=3)match=match && references[add.a[1]]==2;
            else results[n/4]=add.a[1];
            weights[n]=dot.a[3];
        }
        if(!match){out.push_back(instruction);++at;continue;}
        ++blocks;
        auto table=id();Instruction tableDef{44,{weightsArray,table}};
        tableDef.a.insert(tableDef.a.end(),weights.begin(),weights.end());decl.push_back(tableDef);
        auto tableVar=id();emit(decl,59,{weightsPtr,tableVar,6,table});
        const auto result=[&](uint32_t op,uint32_t t,std::initializer_list<uint32_t> operands) {
            auto n=id();Instruction i{op,{t,n}};i.a.insert(i.a.end(),operands.begin(),operands.end());out.push_back(i);return n;
        };
        auto ballot=result(339,v4,{c[3],truth});
        auto count=result(342,u,{c[3],0,ballot}); // Reduce
        auto full=result(170,b,{count,c[32]});
        auto yes=id(),no=id(),merge=id();
        emit(out,247,{merge,0});emit(out,250,{full,yes,no});emit(out,248,{yes});
        auto lane=result(61,u,{laneVar}),subgroup=result(61,u,{subgroupVar});
        auto aBase=result(132,u,{subgroup,c128});
        auto bBase=result(128,u,{result(132,u,{subgroup,c64}),c256});
        auto yBase=result(128,u,{result(132,u,{subgroup,c512}),c384});
        auto rowA=result(128,u,{aBase,result(132,u,{lane,c[4]})});
        for(unsigned k=0;k<4;++k) {
            auto ptr=result(65,ptrWG,{scratch,result(128,u,{rowA,c[k]})});emit(out,62,{ptr,av[k]});
        }
        for(unsigned k=0;k<2;++k) {
            auto index=result(128,u,{lane,k ? c[32] : c[0]});
            auto ptr=result(65,ptrPrivate,{tableVar,index});
            auto val=result(61,u,{ptr});
            auto target=result(65,ptrWG,{scratch,result(128,u,{bBase,index})});emit(out,62,{target,val});
        }
        const auto semantics=constant(u,264); // AcquireRelease | WorkgroupMemory
        emit(out,224,{c[3],c[3],semantics});
        auto bp=result(65,ptrWG,{scratch,bBase});
        auto bm=result(4457,matB,{bp,c[1],c[4]}); // column major, stride in uint words
        for(unsigned tile=0;tile<2;++tile) {
            auto ap=result(65,ptrWG,{scratch,result(128,u,{aBase,tile ? c64 : c[0]})});
            auto am=result(4457,matA,{ap,c[0],c[4]});
            auto cm=result(4459,matC,{am,bm,zero,15}); // signed A/B/C/result, wrapping INT32
            auto yp=result(65,ptrWG,{scratch,result(128,u,{yBase,tile ? c256 : c[0]})});
            emit(out,4458,{yp,cm,c[0],c[16]});
        }
        emit(out,224,{c[3],c[3],semantics});
        auto rowY=result(128,u,{yBase,result(132,u,{lane,c[16]})});
        std::array<uint32_t,16> coop{},scalar{};
        for(unsigned n=0;n<16;++n)coop[n]=result(61,u,{result(65,ptrWG,{scratch,result(128,u,{rowY,c[n]})})});
        emit(out,224,{c[3],c[3],semantics}); // no scratch reuse until every reader finished
        emit(out,249,{merge});emit(out,248,{no});
        for(unsigned n=0;n<128;++n) {
            auto old=input[at+n];
            if(n%8==7){scalar[n/8]=id();old.a[1]=scalar[n/8];}
            out.push_back(old);
        }
        emit(out,249,{merge});emit(out,248,{merge});
        for(unsigned n=0;n<16;++n)emit(out,245,{u,results[n],coop[n],yes,scalar[n],no});
        lastLabel[originalLabel]=merge;at+=128;
    }
    if(!blocks)return 0;
    for(auto& i:out)if(i.op==245)for(size_t n=3;n<i.a.size();n+=2)
        if(lastLabel.count(i.a[n]))i.a[n]=lastLabel[i.a[n]];
    Code final;
    const auto capability=[&](uint32_t cap) {
        for(const auto& i:input)if(i.op==17 && i.a[0]==cap)return;
        emit(final,17,{cap});
    };
    for(const auto& i:out)if(i.op==17)final.push_back(i);
    for(auto cap:{61u,64u,5345u,6022u})capability(cap);
    final.push_back(string_instruction(10,"SPV_KHR_cooperative_matrix"));
    bool memoryExtension=false;
    for(const auto& i:out)if(i.op==10 && std::strcmp(reinterpret_cast<const char*>(i.a.data()),"SPV_KHR_vulkan_memory_model")==0)memoryExtension=true;
    if(!memoryExtension)final.push_back(string_instruction(10,"SPV_KHR_vulkan_memory_model"));
    bool annotated=false,declared=false;
    for(auto i:out) {
        if(i.op==17)continue;
        if(i.op==14)i.a[1]=3; // VulkanKHR
        if(i.op==15){
            if(std::find(i.a.begin()+2,i.a.end(),laneVar)==i.a.end())i.a.push_back(laneVar);
            if(std::find(i.a.begin()+2,i.a.end(),subgroupVar)==i.a.end())i.a.push_back(subgroupVar);
        }
        if(!annotated && i.op>=19 && i.op<=39){final.insert(final.end(),annotations.begin(),annotations.end());annotated=true;}
        if(!declared && i.op==54){final.insert(final.end(),decl.begin(),decl.end());declared=true;}
        final.push_back(i);
    }
    if(!annotated || !declared)throw std::runtime_error("coopmat module lacks declaration sections");
    std::vector<uint32_t> rewritten(words.begin(),words.begin()+5);rewritten[3]=bound;
    for(const auto& i:final){rewritten.push_back((uint32_t(i.a.size()+1)<<16)|i.op);rewritten.insert(rewritten.end(),i.a.begin(),i.a.end());}
    words=std::move(rewritten);return blocks;
}
}
