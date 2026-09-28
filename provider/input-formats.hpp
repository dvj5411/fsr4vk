#pragma once
#include "../amd-fidelityfx-sdk/Kits/FidelityFX/api/include/ffx_api_types.h"
#include <vulkan/vulkan.h>
#include <cstring>
#include <stdexcept>

namespace fsr4vk {
inline VkFormat motion_format_override(const char* value) {
    if(!value || !*value) return VK_FORMAT_UNDEFINED;
    if(std::strcmp(value,"R16G16_SNORM")==0) return VK_FORMAT_R16G16_SNORM;
    throw std::invalid_argument("FSR4_VK_MOTION_FORMAT must be R16G16_SNORM and must match the original VkImage format; unset to keep strict FLOAT input validation");
}
inline VkFormat depth_view_format(uint32_t usage) {
    switch(usage & (FFX_API_RESOURCE_USAGE_DEPTHTARGET | FFX_API_RESOURCE_USAGE_STENCILTARGET)) {
    case 0:return VK_FORMAT_R32_SFLOAT;
    case FFX_API_RESOURCE_USAGE_DEPTHTARGET:return VK_FORMAT_D32_SFLOAT;
    case FFX_API_RESOURCE_USAGE_DEPTHTARGET | FFX_API_RESOURCE_USAGE_STENCILTARGET:return VK_FORMAT_D32_SFLOAT_S8_UINT;
    default:throw std::invalid_argument("invalid depth resource: stencil-only usage cannot provide depth; expected no depth/stencil usage, DEPTHTARGET, or DEPTHTARGET|STENCILTARGET");
    }
}
}
