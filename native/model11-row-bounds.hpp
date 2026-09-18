#pragma once
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <vector>

namespace fsr4 {
// Captured model 11 expands each invocation into a 2x2 tensor block. At the
// 1920-capacity tier its 480-wide input rounds up to 512 lanes; unchecked stores
// from the extra lanes alias the next tensor row. Protect the CAPACITY stride,
// not the active render size (which changes with presets/DRS). Other passes and
// aligned 3840-capacity modules are untouched. See the row-race investigation.
//
// IDs below are a checked contract of the shipped native and portable captures,
// not a general SPIR-V rewriter. Reject changed layouts rather than mispatching.
inline bool guard_model11_rows(std::vector<std::uint32_t>& words) {
    const auto invalid=[] { throw std::runtime_error("unsupported model-11 SPIR-V row-bounds contract"); };
    if(words.size()<5 || words[0]!=0x07230203 || words[3]<100 ||
       words[3]>std::numeric_limits<std::uint32_t>::max()-5) invalid();
    bool entry=false, local=false, builtin=false, uint_type=false, vector_type=false;
    bool bool_type=false, pointer_type=false, variable=false, void_type=false;
    std::uint32_t width=0;
    std::size_t function=0, entry_label=0, entry_branch=0;
    for(std::size_t i=5;i<words.size();) {
        const auto count=words[i]>>16, op=words[i]&0xffff;
        if(!count || count>words.size()-i) invalid();
        const auto matches=[&](std::initializer_list<std::uint32_t> args) {
            if(args.size()+1!=count) return false;
            std::size_t j=i+1;
            for(auto a:args) if(words[j++]!=a) return false;
            return true;
        };
        if(op==15) { // GLCompute %4 "main", interface variables %17/%25
            if(entry || !matches({5,4,1852399981,0,17,25})) invalid();
            entry=true;
        }
        if(op==16 && matches({4,17,64,1,1})) local=true;
        if(op==71 && matches({17,11,28})) builtin=true; // GlobalInvocationId
        if(op==19 && matches({2})) void_type=true;
        if(op==21 && matches({6,32,0})) uint_type=true;
        if(op==23 && matches({15,6,3})) vector_type=true;
        if(op==20 && matches({93})) bool_type=true;
        if(op==32 && matches({16,1,15})) pointer_type=true;
        if(op==59 && matches({16,17,1})) variable=true;
        if(op==43 && count==4 && words[i+1]==6 && words[i+2]==95) width=words[i+3];
        if(op==224) invalid(); // an early return must never bypass a workgroup barrier
        if(op==54 && count==5 && words[i+2]==4) {
            if(function || !matches({2,4,0,3})) invalid();
            function=i;
        } else if(function && !entry_label) {
            if(op!=248 || !matches({5})) invalid();
            entry_label=i;
        } else if(entry_label && !entry_branch) {
            if(op!=249 || count!=2) invalid(); // no function variables/instructions to move
            entry_branch=i;
        }
        i+=count;
    }
    if(!entry || !local || !builtin || !uint_type || !vector_type || !bool_type ||
       !pointer_type || !variable || !void_type || !entry_branch ||
       (width!=480 && width!=960)) invalid();
    if(width%64==0) return false; // keep aligned 4K-capacity shader byte-identical
    const auto id=words[3];
    const std::vector<std::uint32_t> guard{
        (4u<<16)|61,15,id,17,             // load GlobalInvocationId
        (5u<<16)|81,6,id+1,id,0,          // extract X
        (5u<<16)|176,93,id+2,id+1,95,     // unsigned X < captured row width
        (3u<<16)|247,id+3,0,              // selection merge
        (4u<<16)|250,id+2,id+3,id+4,
        (2u<<16)|248,id+4, (1u<<16)|253,  // excess lane: return
        (2u<<16)|248,id+3                 // valid lane: original entry branch
    };
    words.insert(words.begin()+entry_branch,guard.begin(),guard.end());
    words[3]+=5;
    return true;
}
}
