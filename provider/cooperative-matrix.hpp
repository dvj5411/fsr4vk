#pragma once
#include <vulkan/vulkan.h>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace fsr4vk {
inline bool cooperative_matrix_requested() {
    const char* value=std::getenv("FSR4_VK_COOP_MAT");
    if(!value || !*value || std::strcmp(value,"0")==0)return false;
    if(std::strcmp(value,"1")==0)return true;
    throw std::invalid_argument("FSR4_VK_COOP_MAT must be 0 or 1");
}
inline void validate_cooperative_matrix(VkPhysicalDevice physical,
    PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR query,VkBool32 enabled) {
    if(!query || !enabled)
        throw std::runtime_error("FSR4VK_ERROR_MISSING_DEVICE_FEATURES: FSR4_VK_COOP_MAT=1 requires host-confirmed cooperativeMatrix, subgroupSizeControl, computeFullSubgroups, vulkanMemoryModel enablement and the native cooperative-matrix context descriptor; update the host integration or unset FSR4_VK_COOP_MAT");
    uint32_t count=0;
    if(query(physical,&count,nullptr)!=VK_SUCCESS || !count)
        throw std::runtime_error("FSR4VK_ERROR_UNSUPPORTED_DEVICE_CAPABILITIES: no cooperative matrix configurations");
    std::vector<VkCooperativeMatrixPropertiesKHR> props(count);
    for(auto& p:props)p.sType=VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR;
    if(query(physical,&count,props.data())!=VK_SUCCESS)
        throw std::runtime_error("cooperative matrix configuration enumeration failed");
    bool supported=false;
    for(uint32_t i=0;i<count;++i) {
        const auto& p=props[i];
        supported|=p.MSize==16 && p.NSize==16 && p.KSize==16 &&
            p.AType==VK_COMPONENT_TYPE_SINT8_KHR && p.BType==VK_COMPONENT_TYPE_SINT8_KHR &&
            p.CType==VK_COMPONENT_TYPE_SINT32_KHR && p.ResultType==VK_COMPONENT_TYPE_SINT32_KHR &&
            p.scope==VK_SCOPE_SUBGROUP_KHR && !p.saturatingAccumulation;
    }
    if(!supported)throw std::runtime_error("FSR4VK_ERROR_UNSUPPORTED_DEVICE_CAPABILITIES: required cooperative matrix configuration is subgroup 16x16x16 SINT8/SINT8/SINT32/SINT32, non-saturating");
    VkPhysicalDeviceSubgroupSizeControlProperties size{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES};
    VkPhysicalDeviceSubgroupProperties subgroup{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext=&size;size.pNext=&subgroup;vkGetPhysicalDeviceProperties2(physical,&properties);
    if(size.minSubgroupSize>32 || size.maxSubgroupSize<32 || !(size.requiredSubgroupSizeStages&VK_SHADER_STAGE_COMPUTE_BIT) ||
       !(subgroup.supportedOperations&VK_SUBGROUP_FEATURE_BALLOT_BIT) || properties.properties.limits.maxComputeSharedMemorySize<5632)
        throw std::runtime_error("FSR4VK_ERROR_UNSUPPORTED_DEVICE_CAPABILITIES: cooperative path requires compute wave32, subgroup ballot and 5632 bytes shared memory");
}
}
