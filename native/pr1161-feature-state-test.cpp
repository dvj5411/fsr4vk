// Compile with -include <generated pr1161-features.hpp>. Tests the actual PR
// implementation extracted by tools/extract-pr1161-feature-test.py.
#include <cassert>
#include <iostream>
#include <algorithm>
static bool variableCountSupported=true;
template<class T> void fill(VkBaseOutStructure* base) {
    auto* bytes=reinterpret_cast<unsigned char*>(base);
    for(size_t n=sizeof(VkBaseOutStructure);n+sizeof(VkBool32)<=sizeof(T);n+=sizeof(VkBool32)) {
        VkBool32 yes=VK_TRUE;std::memcpy(bytes+n,&yes,sizeof(yes));
    }
}
static void VKAPI_CALL supported(VkPhysicalDevice,VkPhysicalDeviceFeatures2* features) {
    auto* bytes=reinterpret_cast<unsigned char*>(&features->features);
    for(size_t i=0;i<sizeof(features->features);i+=4){VkBool32 yes=VK_TRUE;std::memcpy(bytes+i,&yes,4);}
    for(auto* n=reinterpret_cast<VkBaseOutStructure*>(features->pNext);n;n=n->pNext) {
#define FEATURE(Type,Tag) case Tag:fill<Type>(n);break
        switch(static_cast<int>(n->sType)) {
        FEATURE(VkPhysicalDeviceVulkan12Features,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
        FEATURE(VkPhysicalDeviceVulkan13Features,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
        FEATURE(VkPhysicalDeviceVulkan14Features,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES);
        FEATURE(VkPhysicalDeviceShaderFloat16Int8Features,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES);
        FEATURE(VkPhysicalDevice8BitStorageFeatures,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES);
        FEATURE(VkPhysicalDeviceDescriptorIndexingFeatures,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES);
        FEATURE(VkPhysicalDeviceBufferDeviceAddressFeatures,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES);
        FEATURE(VkPhysicalDeviceVulkanMemoryModelFeatures,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES);
        FEATURE(VkPhysicalDeviceSynchronization2Features,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES);
        FEATURE(VkPhysicalDeviceSubgroupSizeControlFeatures,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES);
        FEATURE(VkPhysicalDeviceDescriptorBufferFeaturesEXT,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT);
        FEATURE(VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT);
        FEATURE(VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR);
        FEATURE(VkPhysicalDeviceShaderIntegerDotProductFeatures,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES);
        FEATURE(VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE,1000673000);
        FEATURE(VkPhysicalDeviceShaderFloatControls2Features,VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT_CONTROLS_2_FEATURES);
        default:assert(false);
        }
#undef FEATURE
        if(n->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES)
            reinterpret_cast<VkPhysicalDeviceDescriptorIndexingFeatures*>(n)->descriptorBindingVariableDescriptorCount=variableCountSupported;
        if(n->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES)
            reinterpret_cast<VkPhysicalDeviceVulkan12Features*>(n)->descriptorBindingVariableDescriptorCount=variableCountSupported;
    }
}
int main() {
    for(auto* e:{VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,VK_KHR_8BIT_STORAGE_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME,VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME,VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
        VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME,VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME,
        VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME,VK_KHR_SHADER_FLOAT_CONTROLS_2_EXTENSION_NAME})vkDeviceExtensions[e]=true;
    for(bool native:{false,true})for(bool promoted:{false,true})for(bool available:{false,true}) {
        if(native)vkDeviceExtensions[VK_VALVE_SHADER_MIXED_FLOAT_DOT_PRODUCT_EXTENSION_NAME]=true;
        else vkDeviceExtensions.erase(VK_VALVE_SHADER_MIXED_FLOAT_DOT_PRODUCT_EXTENSION_NAME);
        variableCountSupported=available;
        VkPhysicalDeviceVulkan14Features v14{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};
        VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};v13.pNext=&v14;
        VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};v12.pNext=&v13;
        VkPhysicalDeviceFeatures2 f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};f.features.robustBufferAccess=VK_TRUE;
        if(promoted)f.pNext=&v12;
        VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};ci.pNext=&f;
        {
            VulkanDeviceFeatureState::Impl s(&ci,supported);s.EnableFeatures(reinterpret_cast<VkPhysicalDevice>(1));
            assert(f.features.robustBufferAccess && f.features.shaderStorageImageReadWithoutFormat && f.features.shaderStorageImageWriteWithoutFormat);
            assert(s.mutableDescriptorType.mutableDescriptorType && s.computeDerivatives.computeDerivativeGroupLinear);
            assert((promoted?v12.descriptorBindingVariableDescriptorCount:s.descriptorIndexing.descriptorBindingVariableDescriptorCount)==available);
            assert(promoted?v13.shaderIntegerDotProduct:s.integerDot.shaderIntegerDotProduct);
            assert(promoted?v12.vulkanMemoryModelDeviceScope:s.vulkanMemoryModel.vulkanMemoryModelDeviceScope);
            assert((s.mixedDot.shaderMixedFloatDotProductFloat16AccFloat32!=0)==native);
            if(native)assert(promoted?v14.shaderFloatControls2:s.floatControls2.shaderFloatControls2);
            if(promoted)assert(!s.FindFeatureStruct<VkPhysicalDeviceShaderIntegerDotProductFeatures>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES));
        }
        assert(f.features.robustBufferAccess && !f.features.shaderStorageImageReadWithoutFormat);
        assert(!v12.descriptorBindingVariableDescriptorCount && !v13.shaderIntegerDotProduct && !v14.shaderFloatControls2);
    }
    std::cout<<"PR1161 feature-state tests passed: extension/core paths, portable/native, unavailable features, caller restoration\n";
}
