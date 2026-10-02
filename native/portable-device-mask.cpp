// Test-only LD_PRELOAD shim: exercise capability-selected portable shaders on
// a VALVE-capable Linux GPU. This never ships in the provider DLL.
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <cstdlib>

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(
    VkPhysicalDevice physical, VkPhysicalDeviceFeatures2* features) {
    static auto real=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
        dlsym(RTLD_NEXT,"vkGetPhysicalDeviceFeatures2"));
    if(!real)std::abort();
    real(physical,features);
    for(auto* node=reinterpret_cast<VkBaseOutStructure*>(features->pNext);node;node=node->pNext)
        if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MIXED_FLOAT_DOT_PRODUCT_FEATURES_VALVE)
            reinterpret_cast<VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE*>(node)
                ->shaderMixedFloatDotProductFloat16AccFloat32=VK_FALSE;
}
