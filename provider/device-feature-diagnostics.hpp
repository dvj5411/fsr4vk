#pragma once
#include "vulkan_device_features.hpp"
#include <map>
#include <string>
#include <cstring>

namespace fsr4vk {
// Read only the known boolean fields, never guess sizes of caller pNext nodes.
// Called after DeviceFeatures has validated the chain. Promoted and extension
// forms use the same keys so the diagnostic does not report false differences.
inline std::map<std::string,bool> diagnostic_feature_bits(const VkDeviceCreateInfo& info) {
    std::map<std::string,bool> result;
    const auto core=[&](const VkPhysicalDeviceFeatures& f) {
        result["shaderInt16"]=f.shaderInt16;
        result["shaderStorageImageReadWithoutFormat"]=f.shaderStorageImageReadWithoutFormat;
        result["shaderStorageImageWriteWithoutFormat"]=f.shaderStorageImageWriteWithoutFormat;
    };
    if(info.pEnabledFeatures) core(*info.pEnabledFeatures);
    auto* n=static_cast<const VkBaseInStructure*>(info.pNext);
    for(unsigned count=0;n && count<64;++count,n=n->pNext) {
#define FSR4_NODE(tag,type) case tag: { const auto& f=*reinterpret_cast<const type*>(n);
#define FSR4_BIT(field) result[#field]=f.field
        switch(static_cast<int>(n->sType)) {
        FSR4_NODE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR,VkPhysicalDeviceCooperativeMatrixFeaturesKHR)
            FSR4_BIT(cooperativeMatrix); break; }
        FSR4_NODE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES,VkPhysicalDeviceSubgroupSizeControlFeatures)
            FSR4_BIT(subgroupSizeControl); FSR4_BIT(computeFullSubgroups); break; }
        FSR4_NODE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,VkPhysicalDeviceFeatures2)
            core(f.features); break; }
        FSR4_NODE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,VkPhysicalDeviceVulkan12Features)
            FSR4_BIT(shaderFloat16); FSR4_BIT(shaderInt8); FSR4_BIT(storageBuffer8BitAccess);
            FSR4_BIT(runtimeDescriptorArray); FSR4_BIT(descriptorBindingVariableDescriptorCount);
            FSR4_BIT(bufferDeviceAddress); FSR4_BIT(vulkanMemoryModel); FSR4_BIT(vulkanMemoryModelDeviceScope);
            FSR4_BIT(shaderStorageBufferArrayNonUniformIndexing); FSR4_BIT(shaderSampledImageArrayNonUniformIndexing);
            FSR4_BIT(shaderStorageImageArrayNonUniformIndexing); break; }
        FSR4_NODE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,VkPhysicalDeviceVulkan13Features)
            FSR4_BIT(synchronization2); FSR4_BIT(shaderIntegerDotProduct);
            FSR4_BIT(subgroupSizeControl); FSR4_BIT(computeFullSubgroups); break; }
        FSR4_NODE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,VkPhysicalDeviceVulkan14Features)
            FSR4_BIT(shaderFloatControls2); break; }
        FSR4_NODE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,VkPhysicalDeviceShaderFloat16Int8Features)
            FSR4_BIT(shaderFloat16); FSR4_BIT(shaderInt8); break; }
        FSR4_NODE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES,VkPhysicalDevice8BitStorageFeatures)
            FSR4_BIT(storageBuffer8BitAccess); break; }
        FSR4_NODE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,VkPhysicalDeviceDescriptorIndexingFeatures)
            FSR4_BIT(runtimeDescriptorArray); FSR4_BIT(descriptorBindingVariableDescriptorCount);
            FSR4_BIT(shaderStorageBufferArrayNonUniformIndexing); FSR4_BIT(shaderSampledImageArrayNonUniformIndexing);
            FSR4_BIT(shaderStorageImageArrayNonUniformIndexing); break; }
        FSR4_NODE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,VkPhysicalDeviceBufferDeviceAddressFeatures)
            FSR4_BIT(bufferDeviceAddress); break; }
        FSR4_NODE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,VkPhysicalDeviceSynchronization2Features)
            FSR4_BIT(synchronization2); break; }
        FSR4_NODE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES,VkPhysicalDeviceShaderIntegerDotProductFeatures)
            FSR4_BIT(shaderIntegerDotProduct); break; }
        FSR4_NODE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT,VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT)
            FSR4_BIT(mutableDescriptorType); break; }
        FSR4_NODE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT,VkPhysicalDeviceDescriptorBufferFeaturesEXT)
            FSR4_BIT(descriptorBuffer); break; }
        FSR4_NODE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR,VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR)
            FSR4_BIT(computeDerivativeGroupLinear); break; }
        FSR4_NODE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MIXED_FLOAT_DOT_PRODUCT_FEATURES_VALVE,VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE)
            FSR4_BIT(shaderMixedFloatDotProductFloat16AccFloat32); break; }
        FSR4_NODE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT_CONTROLS_2_FEATURES_KHR,VkPhysicalDeviceShaderFloatControls2FeaturesKHR)
            FSR4_BIT(shaderFloatControls2); break; }
        FSR4_NODE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES,VkPhysicalDeviceVulkanMemoryModelFeatures)
            FSR4_BIT(vulkanMemoryModel); FSR4_BIT(vulkanMemoryModelDeviceScope); break; }
        default: break;
        }
#undef FSR4_BIT
#undef FSR4_NODE
    }
    return result;
}
inline std::string device_enablement_diagnostic(const VkDeviceCreateInfo& before,const VkDeviceCreateInfo& after) {
    auto source=diagnostic_feature_bits(before);
    std::string features,extensions;
    for(const auto& [name,enabled]:diagnostic_feature_bits(after))
        if(enabled && !source[name]) {
            if(!features.empty()) features+=", ";
            features+=name+"=VK_TRUE";
        }
    for(uint32_t i=0;i<after.enabledExtensionCount;++i) {
        const auto* name=after.ppEnabledExtensionNames[i];
        bool found=false;
        for(uint32_t j=0;j<before.enabledExtensionCount;++j)
            found|=std::strcmp(name,before.ppEnabledExtensionNames[j])==0;
        if(!found) { if(!extensions.empty()) extensions+=", "; extensions+=name; }
    }
    return "device negotiation: features not enabled in host request=["+(features.empty() ? "none" : features)+
        "]; extensions absent from host request=["+(extensions.empty() ? "none" : extensions)+
        "]; added to provider-owned VkDeviceCreateInfo; host must use outputCreateInfo in vkCreateDevice. This is preparation, not proof of successful device creation";
}
inline std::string device_contract_diagnostic(const VkDeviceCreateInfo& info) {
    std::string text="provider device-create contract: features=[";
    bool first=true;
    for(const auto& [name,enabled]:diagnostic_feature_bits(info)) if(enabled) {
        if(!first) text+=", ";
        text+=name+"=VK_TRUE"; first=false;
    }
    text+="]; extensions=[";
    for(uint32_t i=0;i<info.enabledExtensionCount;++i) {
        if(i) text+=", ";
        text+=info.ppEnabledExtensionNames[i];
    }
    return text+"]; if vulkanMemoryModel is enabled, also enable vulkanMemoryModelDeviceScope; this is a contract, NOT a list of observed disabled bits";
}
}
