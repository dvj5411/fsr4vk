// Streaming, bounded-memory RGBA16F comparison for large reference matrices.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
float decode(uint16_t h) {
    const uint32_t sign=uint32_t(h&0x8000)<<16;
    uint32_t exponent=(h>>10)&31, mantissa=h&1023, bits;
    if (!exponent) {
        if (!mantissa) bits=sign;
        else { int e=-14; while (!(mantissa&1024)) { mantissa<<=1; --e; }
            bits=sign | uint32_t(e+127)<<23 | (mantissa&1023)<<13; }
    } else bits=sign | (exponent==31 ? 255u : exponent+112u)<<23 | mantissa<<13;
    float value; std::memcpy(&value,&bits,4); return value;
}
int main(int argc,char** argv) {
    if(argc!=3) return 64;
    std::ifstream a(argv[1],std::ios::binary), b(argv[2],std::ios::binary);
    if(!a || !b) return 2;
    uint16_t x[32768],y[32768]; uint64_t count=0,nonfinite=0,above=0;
    double sum=0,max_error=0;
    while(true) {
        a.read(reinterpret_cast<char*>(x),sizeof(x)); b.read(reinterpret_cast<char*>(y),sizeof(y));
        auto size=a.gcount(); if(size!=b.gcount() || size%2) return 3;
        if(!size) break;
        for(int i=0;i<size/2;++i) {
            double p=decode(x[i]),q=decode(y[i]); ++count;
            if(!std::isfinite(p)||!std::isfinite(q)) { ++nonfinite; continue; }
            double e=std::abs(p-q); sum+=e*e; max_error=std::max(max_error,e); above+=e>0.001;
        }
    }
    if(!count || count%4 || a.bad() || b.bad()) return 4;
    std::cout<<std::setprecision(14)<<"{\"values\":"<<count<<",\"nonfinite_pairs\":"<<nonfinite
      <<",\"rmse\":"<<std::sqrt(sum/count)<<",\"max_abs_error\":"<<max_error
      <<",\"above_tolerance_fraction\":"<<double(above)/count<<"}\n";
}
