#include "buffer-memory-policy.hpp"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    using namespace fsr4core;
    constexpr auto required=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VkPhysicalDeviceMemoryProperties p{};p.memoryTypeCount=4;
    p.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    p.memoryTypes[1].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    p.memoryTypes[2].propertyFlags=required;
    p.memoryTypes[3].propertyFlags=required|VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    auto c=choose_buffer_memory(p,15,required,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    assert(c.baseline==2 && c.preferred==3);
    assert(choose_buffer_memory(p,15,required).preferred==2);
    assert(choose_buffer_memory(p,7,required,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT).preferred==2);
    assert(choose_buffer_memory(p,8,required,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT).baseline==3);
    auto uma=p;uma.memoryTypes[2].propertyFlags|=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    assert(choose_buffer_memory(uma,15,required,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT).preferred==2);
    auto noncoherent=p;noncoherent.memoryTypes[3].propertyFlags&=~VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    assert(choose_buffer_memory(noncoherent,15,required,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT).preferred==2);
    for(auto bits:{0u,1u,3u}) {
        bool rejected=false;
        try {choose_buffer_memory(p,bits,required);}catch(const std::runtime_error&){rejected=true;}
        assert(rejected);
    }
    p.memoryTypeCount=32;p.memoryTypes[31].propertyFlags=required;
    assert(choose_buffer_memory(p,1u<<31,required).preferred==31);
    p.memoryTypeCount=33;
    bool rejected=false;try{choose_buffer_memory(p,15,required);}catch(...){rejected=true;}assert(rejected);

    VkMemoryAllocateFlagsInfo bda{};bda.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    bda.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo original{};original.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    original.pNext=&bda;original.allocationSize=90112;
    const auto test=[&](VkResult first_allocate,VkResult first_map,VkResult second_allocate,
                        bool host,bool distinct,VkResult second_map=VK_SUCCESS) {
        std::vector<uint32_t> attempts;std::vector<VkDeviceMemory> freed;
        unsigned maps=0;
        VkDeviceMemory memory=VK_NULL_HANDLE;void* mapped=nullptr;
        auto result=allocate_buffer_memory(original,{2,distinct?3u:2u},host,89216,memory,mapped,
            [&](const VkMemoryAllocateInfo& info,VkDeviceMemory& output) {
                assert(info.pNext==&bda && info.allocationSize==90112);
                attempts.push_back(info.memoryTypeIndex);
                output=reinterpret_cast<VkDeviceMemory>(uintptr_t(attempts.size()));
                return attempts.size()==1 ? first_allocate : second_allocate;
            },[&](VkDeviceMemory allocation,VkDeviceSize bytes,void*& pointer) {
                assert(bytes==89216);++maps;pointer=reinterpret_cast<void*>(uintptr_t(allocation));
                return attempts.size()==1 ? first_map : second_map;
            },[&](VkDeviceMemory allocation) {freed.push_back(allocation);});
        const auto initial=first_allocate!=VK_SUCCESS ? first_allocate : host ? first_map : VK_SUCCESS;
        const bool retry=distinct && (initial==VK_ERROR_OUT_OF_DEVICE_MEMORY || initial==VK_ERROR_MEMORY_MAP_FAILED);
        assert(result.fallback==retry && attempts.size()==(retry?2u:1u));
        assert(result.type==(retry?2u:distinct?3u:2u));
        const auto freed_first=first_allocate==VK_SUCCESS && host && first_map!=VK_SUCCESS ? 1u:0u;
        const auto freed_second=retry && second_allocate==VK_SUCCESS && host && second_map!=VK_SUCCESS ? 1u:0u;
        assert(freed.size()==freed_first+freed_second);
        const auto expected=retry ? (second_allocate!=VK_SUCCESS ? second_allocate : host ? second_map : VK_SUCCESS) : initial;
        assert(result.result==expected);
        if(expected==VK_SUCCESS) assert(memory && (host ? mapped!=nullptr : mapped==nullptr));
        else assert(!memory && !mapped);
        if(first_allocate!=VK_SUCCESS && !retry)assert(maps==0 && freed.empty());
    };
    for(auto failure:{VK_SUCCESS,VK_ERROR_OUT_OF_DEVICE_MEMORY,VK_ERROR_OUT_OF_HOST_MEMORY,VK_ERROR_DEVICE_LOST})
        for(bool distinct:{false,true})test(failure,VK_SUCCESS,VK_SUCCESS,true,distinct);
    for(auto failure:{VK_ERROR_MEMORY_MAP_FAILED,VK_ERROR_OUT_OF_DEVICE_MEMORY,VK_ERROR_OUT_OF_HOST_MEMORY,VK_ERROR_DEVICE_LOST})
        for(bool distinct:{false,true})test(VK_SUCCESS,failure,VK_SUCCESS,true,distinct);
    test(VK_ERROR_OUT_OF_DEVICE_MEMORY,VK_SUCCESS,VK_ERROR_OUT_OF_DEVICE_MEMORY,true,true);
    test(VK_SUCCESS,VK_ERROR_MEMORY_MAP_FAILED,VK_ERROR_OUT_OF_HOST_MEMORY,true,true);
    test(VK_SUCCESS,VK_ERROR_MEMORY_MAP_FAILED,VK_SUCCESS,false,true);
    test(VK_SUCCESS,VK_ERROR_MEMORY_MAP_FAILED,VK_SUCCESS,true,true,VK_ERROR_MEMORY_MAP_FAILED);
    std::cout<<"buffer memory selection, upload mapping, cleanup and fallback tests passed\n";
}
