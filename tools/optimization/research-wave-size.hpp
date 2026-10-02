#pragma once
// Single-context native research harness only; never included in the provider.
#include <vulkan/vulkan.h>
#include <cstdio>
#include <stdexcept>
#ifndef FSR4_RESEARCH_WAVE32_MASK
#define FSR4_RESEARCH_WAVE32_MASK 0
#endif
static_assert((FSR4_RESEARCH_WAVE32_MASK & ~0x3fff) == 0);
namespace fsr4wavetest {
inline unsigned pipeline_index=0;
inline VkPhysicalDeviceSubgroupSizeControlFeatures enable(
    VkPhysicalDevice physical,VkDeviceCreateInfo& device_info,uint32_t api) {
    if(api!=VK_API_VERSION_1_3 || device_info.pNext)
        throw std::runtime_error("wave experiment requires unchained Vulkan 1.3 device");
    VkPhysicalDeviceSubgroupSizeControlFeatures sub{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext=&sub; vkGetPhysicalDeviceFeatures2(physical,&features);
    VkPhysicalDeviceSubgroupSizeControlProperties properties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES};
    VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props.pNext=&properties; vkGetPhysicalDeviceProperties2(physical,&props);
    if(!sub.subgroupSizeControl || properties.minSubgroupSize>32 ||
       properties.maxSubgroupSize<32 || properties.maxComputeWorkgroupSubgroups<8 ||
       !(properties.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT))
        throw std::runtime_error("wave32 experiment unsupported");
    sub.computeFullSubgroups=VK_FALSE;
    return sub; // Caller owns this structure through vkCreateDevice.
}
inline VkResult create(VkDevice device,VkPipelineCache cache,uint32_t count,
    const VkComputePipelineCreateInfo* input,const VkAllocationCallbacks* alloc,VkPipeline* out) {
    if(count!=1 || pipeline_index>=15 || input->stage.pNext)
        throw std::runtime_error("unexpected single-context pipeline layout");
    const unsigned index=pipeline_index++;
    const bool selected=(FSR4_RESEARCH_WAVE32_MASK & (1u<<index))!=0;
    auto info=*input;
    VkPipelineShaderStageRequiredSubgroupSizeCreateInfo sub{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO};
    sub.requiredSubgroupSize=32;
    if(selected) info.stage.pNext=&sub;
    std::fprintf(stderr,"{\"kind\":\"wave_size\",\"pipeline_index\":%u,\"requested_wave\":%u}\n",
                 index,selected ? 32u : 0u);
    return vkCreateComputePipelines(device,cache,count,&info,alloc,out);
}
}
