#include "timestamp-policy.hpp"
#include <iostream>
#include <stdexcept>
int main() {
    if(fsr4vk::timestamp_delta(250,3,8)!=9 || fsr4vk::timestamp_delta(0,8,64)!=8 ||
       fsr4vk::timestamp_delta(UINT64_MAX,2,64)!=3) throw std::runtime_error("timestamp wrap regression");
    for(auto bits:{0u,65u}) {
        bool rejected=false;try{(void)fsr4vk::timestamp_delta(0,0,bits);}catch(const std::invalid_argument&){rejected=true;}
        if(!rejected) throw std::runtime_error("unsupported timestamp precision accepted");
    }
    std::cout<<"timestamp policy tests passed\n";
}
