#include "descriptor-layout.hpp"
#include <iostream>
#include <stdexcept>

void require(bool value) {if(!value) throw std::runtime_error("descriptor layout regression");}
template<class Call> void rejected(Call call) {try{call();}catch(const std::exception&){return;}throw std::runtime_error("invalid layout accepted");}
int main() {
    VkPhysicalDeviceDescriptorBufferPropertiesEXT p{};
    p.samplerDescriptorSize=16; p.sampledImageDescriptorSize=32; p.storageImageDescriptorSize=32;
    p.uniformBufferDescriptorSize=p.storageBufferDescriptorSize=16;
    p.uniformTexelBufferDescriptorSize=p.storageTexelBufferDescriptorSize=16;
    p.robustUniformBufferDescriptorSize=p.robustStorageBufferDescriptorSize=16;
    p.robustUniformTexelBufferDescriptorSize=p.robustStorageTexelBufferDescriptorSize=16;
    // Different trailing/leading padding is legal, and must not affect stride.
    fsr4vk::DescriptorLayout layout(p,{16*128+64,32*128+192,32*128+256},{0,64,128});
    require(layout.mutable_stride==32 && layout.offset(1,127,32)==64+127*32);
    require(layout.offset(2,127,16)==128+127*32);
    rejected([&]{(void)layout.offset(2,128,16);});
    rejected([&]{(void)layout.offset(1,0,33);});
    rejected([&]{fsr4vk::DescriptorLayout bad(p,{1,4096,4096},{0,0,0});});
    p.robustStorageBufferDescriptorSize=64;
    rejected([&]{fsr4vk::DescriptorLayout unknown(p,{2048,8192,8192},{0,0,0});});
    fsr4vk::DescriptorLayout robust(p,{2048,8192+128,8192+64},{0,64,0},true);
    require(robust.mutable_stride==64 && robust.storage_buffer_size==64);
    fsr4vk::DescriptorLayout ordinary(p,{2048,4096,4096},{0,0,0},false);
    require(ordinary.mutable_stride==32 && ordinary.storage_buffer_size==16);
    rejected([&]{fsr4vk::DescriptorLayout bad(p,{2048,8192,8192},{0,UINT64_MAX,0},true);});
    std::cout << "padded/robust descriptor layout and checked-write tests passed\n";
}
