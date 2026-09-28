// Compile with -I <patched-OptiScaler>/OptiScaler/upscalers/ffx.
#include "FFXVkCooperativeDevice.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace FFXVkCooperativeDevice;
    auto instance=reinterpret_cast<VkInstance>(1);auto device=reinterpret_cast<VkDevice>(2);
    const char* extensions[]={VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME};
    VkPhysicalDeviceCooperativeMatrixFeaturesKHR matrix{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR};
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext=&matrix;matrix.pNext=&v12;v12.pNext=&v13;
    info.enabledExtensionCount=1;info.ppEnabledExtensionNames=extensions;
    matrix.cooperativeMatrix=v12.vulkanMemoryModel=v12.vulkanMemoryModelDeviceScope=VK_TRUE;
    v13.subgroupSizeControl=v13.computeFullSubgroups=VK_TRUE;
    assert(!Enabled(device));Record(instance,device,info);assert(Enabled(device));
    v13.computeFullSubgroups=VK_FALSE;Record(instance,device,info);assert(!Enabled(device));
    v13.computeFullSubgroups=VK_TRUE;info.enabledExtensionCount=0;Record(instance,device,info);assert(!Enabled(device));
    info.enabledExtensionCount=1;Record(instance,device,info);assert(Enabled(device));
    Forget(instance);assert(!Enabled(device));
    VkPhysicalDeviceSubgroupSizeControlFeatures size{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES};
    VkPhysicalDeviceVulkanMemoryModelFeatures memory{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES};
    matrix.pNext=&size;size.pNext=&memory;
    size.subgroupSizeControl=size.computeFullSubgroups=memory.vulkanMemoryModel=memory.vulkanMemoryModelDeviceScope=VK_TRUE;
    Record(instance,device,info);assert(Enabled(device));
    memory.vulkanMemoryModelDeviceScope=VK_FALSE;Record(instance,device,info);assert(!Enabled(device));
    Forget(instance);std::cout<<"Host logical-device attestation tests passed\n";
}
