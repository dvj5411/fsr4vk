#include "resolution-plan.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
int main() {
    using namespace fsr4;
    const std::array<std::array<uint32_t,2>,14> baseline{{
        {120,68},{15,540},{15,540},{8,270},{8,270},{8,270},{4,135},
        {4,135},{4,135},{30,17},{8,270},{8,270},{15,540},{120,68}}};
    for(unsigned i=0;i<14;++i)assert(quality_dispatch(i,1920,1080)==baseline[i]);
    assert(scratch_capacity(1920,1080)==20880256);
    assert(scratch_capacity(1921,1080)==83232256);
    assert(scratch_capacity(1920,1081)==83232256);
    assert(sanitize_pre_exposure(0.5f)==0.5f);
    assert(sanitize_pre_exposure(0.0f)==1.0f);
    assert(sanitize_pre_exposure(-1.0f)==1.0f);
    assert(sanitize_pre_exposure(INFINITY)==1.0f);
    assert(sanitize_pre_exposure(NAN)==1.0f);
    for(auto size:{64u,65u,719u,720u,1079u,1080u,1081u,2160u}) {
        assert(round_up(size,8)>=size && round_up(size,8)-size<8);
        for(unsigned i=0;i<14;++i) {
            const auto group=quality_dispatch(i,size,size);
            assert(group[0]>0 && group[1]>0);
        }
    }
    for (auto width : {1920u,1280u,1129u,960u,640u})
        assert(!std::strcmp(resolution_preset(width,1920,true),"drs"));
    assert(!std::strcmp(resolution_preset(1920,1920),"native"));
    assert(!std::strcmp(resolution_preset(1280,1920),"quality"));
    assert(!std::strcmp(resolution_preset(900,1344),"quality"));
    assert(!std::strcmp(resolution_preset(1000,1488),"native"));
    assert(!std::strcmp(resolution_preset(1000,1496),"quality"));
    assert(!std::strcmp(resolution_preset(1129,1920),"balanced"));
    assert(!std::strcmp(resolution_preset(960,1920),"performance"));
    assert(!std::strcmp(resolution_preset(640,1920),"ultraperf"));
    for(const auto invalid:std::array<std::array<uint32_t,4>,6>{{
        {0,64,128,128},{63,64,128,128},{129,64,128,128},
        {64,129,128,128},{64,64,3841,2160},{64,64,3840,2161}}}) {
        bool rejected=false;
        try {validate_dimensions(invalid[0],invalid[1],invalid[2],invalid[3]);}
        catch(const std::invalid_argument&) {rejected=true;}
        assert(rejected);
    }
    validate_dimensions(64,64,3840,2160);
    validate_dimensions(3840,2160,3840,2160);
    std::cout<<"resolution plan tests passed\n";
}
