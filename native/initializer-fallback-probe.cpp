// W6400-only failure injection. Not part of the provider or shipped DLL.
#include <vulkan/vulkan.h>
#include <cstdio>
#include <stdexcept>
#ifndef FSR4_TEST_INITIALIZER_MAP_FAILURE
#define FSR4_TEST_INITIALIZER_MAP_FAILURE 0
#endif
namespace initializer_failure_test {
inline bool injected=false;
inline VkDeviceMemory target=VK_NULL_HANDLE;
inline VkResult allocate(VkDevice device,const VkMemoryAllocateInfo* info,
    const VkAllocationCallbacks* callbacks,VkDeviceMemory* memory) {
    if(!injected && info->allocationSize==89216 && info->memoryTypeIndex==3) {
        if(!FSR4_TEST_INITIALIZER_MAP_FAILURE) {
            injected=true;
            std::fprintf(stderr,"initializer_failure_injected=allocation\n");
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        auto result=vkAllocateMemory(device,info,callbacks,memory);
        if(result==VK_SUCCESS)target=*memory;
        return result;
    }
    return vkAllocateMemory(device,info,callbacks,memory);
}
inline VkResult map(VkDevice device,VkDeviceMemory memory,VkDeviceSize offset,
    VkDeviceSize size,VkMemoryMapFlags flags,void** pointer) {
    if(!injected && target && target==memory) {
        injected=true;
        std::fprintf(stderr,"initializer_failure_injected=mapping\n");
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    return vkMapMemory(device,memory,offset,size,flags,pointer);
}
}
#define vkAllocateMemory initializer_failure_test::allocate
#define vkMapMemory initializer_failure_test::map
#include "quality-context.hpp"
#undef vkMapMemory
#undef vkAllocateMemory
#include "dispatch-smoke.cpp"
