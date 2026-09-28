#pragma once
#include <vulkan/vulkan.h>
#include <string>
namespace fsr4vk {
inline std::string result_description(VkResult result) {
    const char* name="UNKNOWN_VK_RESULT";
    switch(result) {
    case VK_SUCCESS:name="VK_SUCCESS";break;
    case VK_ERROR_OUT_OF_HOST_MEMORY:name="VK_ERROR_OUT_OF_HOST_MEMORY";break;
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:name="VK_ERROR_OUT_OF_DEVICE_MEMORY";break;
    case VK_ERROR_INITIALIZATION_FAILED:name="VK_ERROR_INITIALIZATION_FAILED";break;
    case VK_ERROR_DEVICE_LOST:name="VK_ERROR_DEVICE_LOST";break;
    case VK_ERROR_MEMORY_MAP_FAILED:name="VK_ERROR_MEMORY_MAP_FAILED";break;
    case VK_ERROR_FEATURE_NOT_PRESENT:name="VK_ERROR_FEATURE_NOT_PRESENT";break;
    case VK_ERROR_EXTENSION_NOT_PRESENT:name="VK_ERROR_EXTENSION_NOT_PRESENT";break;
    case VK_ERROR_FORMAT_NOT_SUPPORTED:name="VK_ERROR_FORMAT_NOT_SUPPORTED";break;
    case VK_ERROR_TOO_MANY_OBJECTS:name="VK_ERROR_TOO_MANY_OBJECTS";break;
    case VK_ERROR_FRAGMENTED_POOL:name="VK_ERROR_FRAGMENTED_POOL";break;
    case VK_ERROR_OUT_OF_POOL_MEMORY:name="VK_ERROR_OUT_OF_POOL_MEMORY";break;
    default:break;
    }
    return std::string(name)+"("+std::to_string(result)+")";
}
}
