#pragma once
#include <vulkan/vulkan.h>
#include <stdexcept>

namespace fsr4core {
struct BufferMemoryChoice {
    uint32_t baseline;
    uint32_t preferred;
};

// Required properties always win. A preference must never expand memoryTypeBits
// or weaken HOST_VISIBLE/COHERENT requirements of the existing upload path.
inline BufferMemoryChoice choose_buffer_memory(const VkPhysicalDeviceMemoryProperties& memory,
    uint32_t bits,VkMemoryPropertyFlags required,VkMemoryPropertyFlags preferred=0) {
    if(memory.memoryTypeCount>VK_MAX_MEMORY_TYPES)
        throw std::runtime_error("invalid Vulkan memory type count");
    uint32_t baseline=VK_MAX_MEMORY_TYPES,choice=VK_MAX_MEMORY_TYPES;
    for(uint32_t i=0;i<memory.memoryTypeCount;++i) {
        const auto flags=memory.memoryTypes[i].propertyFlags;
        if(!(bits & (1u<<i)) || (flags & required)!=required) continue;
        if(baseline==VK_MAX_MEMORY_TYPES) baseline=i;
        if(choice==VK_MAX_MEMORY_TYPES && (flags & preferred)==preferred) choice=i;
    }
    if(baseline==VK_MAX_MEMORY_TYPES) throw std::runtime_error("no compatible Vulkan memory type");
    return {baseline,choice==VK_MAX_MEMORY_TYPES ? baseline : choice};
}

struct BufferMemoryOutcome {
    VkResult result;
    uint32_t type;
    bool fallback;
};

// Map before binding so a failed optional local-memory mapping can be released
// and retried without rebinding an already-bound buffer. No flush is necessary:
// callers that write mapped memory still require HOST_COHERENT.
template<class Allocate,class Map,class Free>
BufferMemoryOutcome allocate_buffer_memory(const VkMemoryAllocateInfo& original,
    BufferMemoryChoice choice,bool host_visible,VkDeviceSize map_size,
    VkDeviceMemory& memory,void*& mapped,Allocate allocate,Map map,Free release) {
    memory=VK_NULL_HANDLE; mapped=nullptr;
    const auto attempt=[&](uint32_t type) {
        auto info=original;info.memoryTypeIndex=type; // retain size and BDA pNext
        VkDeviceMemory candidate=VK_NULL_HANDLE;
        auto status=allocate(info,candidate);
        if(status!=VK_SUCCESS) return status; // failure outputs are undefined
        void* pointer=nullptr;
        if(host_visible) {
            status=map(candidate,map_size,pointer);
            if(status!=VK_SUCCESS) {release(candidate);return status;}
        }
        memory=candidate;mapped=pointer;
        return VK_SUCCESS;
    };
    auto result=attempt(choice.preferred);
    const bool fallback=choice.preferred!=choice.baseline &&
        (result==VK_ERROR_OUT_OF_DEVICE_MEMORY || result==VK_ERROR_MEMORY_MAP_FAILED);
    if(fallback) result=attempt(choice.baseline);
    return {result,fallback ? choice.baseline : choice.preferred,fallback};
}
}
