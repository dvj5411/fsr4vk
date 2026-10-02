#pragma once
// Isolated harness instrumentation. Never include in the distributed provider.
#include <vulkan/vulkan.h>
#include <cstdio>
#include <map>
#include <stdexcept>

#ifndef FSR4_RESEARCH_PREFER_LOCAL_CONSTANTS
#define FSR4_RESEARCH_PREFER_LOCAL_CONSTANTS 0
#endif
#ifndef FSR4_RESEARCH_PREFER_LOCAL_DESCRIPTORS
#define FSR4_RESEARCH_PREFER_LOCAL_DESCRIPTORS 0
#endif
#ifndef FSR4_RESEARCH_PREFER_LOCAL_INITIALIZERS
#define FSR4_RESEARCH_PREFER_LOCAL_INITIALIZERS 0
#endif
static_assert(FSR4_RESEARCH_PREFER_LOCAL_CONSTANTS == 0 ||
              FSR4_RESEARCH_PREFER_LOCAL_CONSTANTS == 1);

namespace fsr4memorytest {
constexpr VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
inline uint32_t choose(const VkPhysicalDeviceMemoryProperties& props, uint32_t bits,
                       uint32_t baseline, bool prefer) {
    if (props.memoryTypeCount > VK_MAX_MEMORY_TYPES || baseline >= props.memoryTypeCount ||
        !(bits & (1u << baseline)) ||
        (props.memoryTypes[baseline].propertyFlags & required) != required)
        throw std::runtime_error("invalid constant-buffer baseline memory type");
    if (prefer) {
        for (uint32_t i=0; i<props.memoryTypeCount; ++i)
            if ((bits & (1u<<i)) && (props.memoryTypes[i].propertyFlags &
                (required | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) ==
                (required | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return i;
    }
    return baseline;
}

template<class Allocate>
VkResult attempt(uint32_t baseline, uint32_t preferred, Allocate allocate,
                 uint32_t& selected, bool& fallback) {
    selected=preferred; fallback=false;
    VkResult result=allocate(selected);
    if (result == VK_ERROR_OUT_OF_DEVICE_MEMORY && preferred != baseline) {
        selected=baseline; fallback=true;
        result=allocate(selected);
    }
    return result;
}

struct BufferInfo { VkDeviceSize size{}; VkBufferUsageFlags usage{}; };
struct Pending { bool valid=false; BufferInfo info{}; VkMemoryRequirements requirements{}; };
inline thread_local std::map<VkBuffer,BufferInfo> buffers;
inline thread_local Pending pending;
inline thread_local VkPhysicalDeviceMemoryProperties properties{};

inline VkResult create_buffer(VkDevice device,const VkBufferCreateInfo* info,
    const VkAllocationCallbacks* callbacks,VkBuffer* buffer) {
    auto result=vkCreateBuffer(device,info,callbacks,buffer);
    if (result==VK_SUCCESS) buffers[*buffer]={info->size,info->usage};
    return result;
}
inline void buffer_requirements(VkDevice device,VkBuffer buffer,VkMemoryRequirements* req) {
    vkGetBufferMemoryRequirements(device,buffer,req);
    pending={};
    auto found=buffers.find(buffer);
    if (found!=buffers.end()) pending={true,found->second,*req};
}
inline void memory_properties(VkPhysicalDevice physical,VkPhysicalDeviceMemoryProperties* props) {
    vkGetPhysicalDeviceMemoryProperties(physical,props);
    properties=*props;
}
inline VkResult allocate_memory(VkDevice device,const VkMemoryAllocateInfo* info,
    const VkAllocationCallbacks* callbacks,VkDeviceMemory* memory) {
    const auto request=pending;
    pending={}; // Image allocations must never inherit the preceding buffer role.
    const bool constants=request.valid && request.info.size==2048 &&
        request.info.usage==VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    const auto descriptor_usage=VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT |
        VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
    const bool descriptors=request.valid && (request.info.usage & descriptor_usage)!=0 &&
        (request.info.usage & ~(descriptor_usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT))==0;
    const bool initializer=request.valid && request.info.usage==
        (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    // Scratch includes TRANSFER_DST; harness allocations are outside the hooks.
    if (!constants && !descriptors && !initializer)
        return vkAllocateMemory(device,info,callbacks,memory);
    if (info->allocationSize!=request.requirements.size)
        throw std::runtime_error("stale research allocation metadata");
    const auto baseline=info->memoryTypeIndex;
    const bool prefer=constants ? FSR4_RESEARCH_PREFER_LOCAL_CONSTANTS!=0 :
        descriptors ? FSR4_RESEARCH_PREFER_LOCAL_DESCRIPTORS!=0 :
                      FSR4_RESEARCH_PREFER_LOCAL_INITIALIZERS!=0;
    const char* role=constants ? "frame_constants" : descriptors ? "descriptors" : "initializers";
    const auto preferred=choose(properties,request.requirements.memoryTypeBits,baseline,prefer);
    uint32_t selected=baseline; bool fallback=false;
    auto result=attempt(baseline,preferred,[&](uint32_t type) {
        auto allocation=*info; // Preserve pNext, device-address flags and allocation size.
        allocation.memoryTypeIndex=type;
        return vkAllocateMemory(device,&allocation,callbacks,memory);
    },selected,fallback);
    std::fprintf(stderr,"{\"kind\":\"memory_placement\",\"role\":\"%s\","
        "\"prefer_local\":%d,\"requested_bytes\":%llu,\"allocated_bytes\":%llu,"
        "\"memory_type_bits\":%u,\"baseline_type\":%u,\"preferred_type\":%u,"
        "\"selected_type\":%u,\"heap\":%u,\"property_flags\":%u,"
        "\"allocation_fallback\":%s,\"result\":%d}\n",
        role,int(prefer),
        static_cast<unsigned long long>(request.info.size),
        static_cast<unsigned long long>(info->allocationSize),request.requirements.memoryTypeBits,
        baseline,preferred,selected,properties.memoryTypes[selected].heapIndex,
        properties.memoryTypes[selected].propertyFlags,fallback ? "true" : "false",int(result));
    return result;
}
} // namespace fsr4memorytest
