#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Compatibility with the Vulkan 1.4.310 headers bundled with OptiScaler.
#ifndef VK_VALVE_shader_mixed_float_dot_product
#define VK_VALVE_SHADER_MIXED_FLOAT_DOT_PRODUCT_EXTENSION_NAME "VK_VALVE_shader_mixed_float_dot_product"
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MIXED_FLOAT_DOT_PRODUCT_FEATURES_VALVE                                \
    static_cast<VkStructureType>(1000673000)
struct VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE
{
    VkStructureType sType;
    void* pNext;
    VkBool32 shaderMixedFloatDotProductFloat16AccFloat32;
    VkBool32 shaderMixedFloatDotProductFloat16AccFloat16;
    VkBool32 shaderMixedFloatDotProductBFloat16Acc;
    VkBool32 shaderMixedFloatDotProductFloat8AccFloat32;
};
#endif

// Compatibility with device-feature structures newer than the Vulkan 1.4.310
// headers bundled with OptiScaler. These definitions mirror the Khronos ABI so
// application-owned pNext nodes can be copied without dropping their payload.
#ifndef VK_KHR_unified_image_layouts
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFIED_IMAGE_LAYOUTS_FEATURES_KHR static_cast<VkStructureType>(1000527000)
struct VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR
{
    VkStructureType sType;
    void* pNext;
    VkBool32 unifiedImageLayouts;
    VkBool32 unifiedImageLayoutsVideo;
};
#endif

#ifndef VK_KHR_shader_untyped_pointers
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNTYPED_POINTERS_FEATURES_KHR static_cast<VkStructureType>(1000387000)
struct VkPhysicalDeviceShaderUntypedPointersFeaturesKHR
{
    VkStructureType sType;
    void* pNext;
    VkBool32 shaderUntypedPointers;
};
#endif

#ifndef VK_KHR_maintenance9
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_9_FEATURES_KHR static_cast<VkStructureType>(1000584000)
struct VkPhysicalDeviceMaintenance9FeaturesKHR
{
    VkStructureType sType;
    void* pNext;
    VkBool32 maintenance9;
};
#endif

#ifndef VK_KHR_maintenance10
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_10_FEATURES_KHR static_cast<VkStructureType>(1000630000)
struct VkPhysicalDeviceMaintenance10FeaturesKHR
{
    VkStructureType sType;
    void* pNext;
    VkBool32 maintenance10;
};
#endif

#ifndef VK_EXT_descriptor_heap
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT static_cast<VkStructureType>(1000135009)
struct VkPhysicalDeviceDescriptorHeapFeaturesEXT
{
    VkStructureType sType;
    void* pNext;
    VkBool32 descriptorHeap;
    VkBool32 descriptorHeapCaptureReplay;
};
#endif

namespace fsr4vk
{
// Owns copies: never modifies the application's const pNext chain. Unknown
// structures are rejected, not silently dropped or guessed. Keep alive through
// vkCreateDevice. Only used by the explicitly opted-in experimental path.
class DeviceFeatures
{
    std::vector<std::shared_ptr<void>> nodes;
    std::vector<const char*> extensions;
    VkPhysicalDeviceFeatures core {};
    VkDeviceCreateInfo info {};
    template <class T> T* copy(const T& value)
    {
        auto node = std::make_shared<T>(value);
        auto* ptr = node.get();
        nodes.push_back(std::move(node));
        return ptr;
    }
    VkBaseOutStructure* clone(const VkBaseInStructure* node)
    {
#define FSR4_COPY(type, tag)                                                                                           \
    case tag:                                                                                                          \
        return reinterpret_cast<VkBaseOutStructure*>(copy(*reinterpret_cast<const type*>(node)))
        switch (static_cast<int>(node->sType))
        {
            FSR4_COPY(VkPhysicalDeviceFeatures2, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
            FSR4_COPY(VkPhysicalDeviceVulkan11Features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
            FSR4_COPY(VkPhysicalDeviceVulkan12Features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
            FSR4_COPY(VkPhysicalDeviceVulkan13Features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
            FSR4_COPY(VkPhysicalDeviceVulkan14Features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES);
            FSR4_COPY(VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFIED_IMAGE_LAYOUTS_FEATURES_KHR);
            FSR4_COPY(VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceShaderUntypedPointersFeaturesKHR,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNTYPED_POINTERS_FEATURES_KHR);
            FSR4_COPY(VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_UNIFORM_CONTROL_FLOW_FEATURES_KHR);
            FSR4_COPY(VkPhysicalDeviceShaderFloatControls2FeaturesKHR,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT_CONTROLS_2_FEATURES_KHR);
            FSR4_COPY(VkPhysicalDevicePresentWaitFeaturesKHR, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR);
            FSR4_COPY(VkPhysicalDevicePresentIdFeaturesKHR, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR);
            FSR4_COPY(VkPhysicalDeviceMaintenance10FeaturesKHR,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_10_FEATURES_KHR);
            FSR4_COPY(VkPhysicalDeviceMaintenance9FeaturesKHR,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_9_FEATURES_KHR);
            FSR4_COPY(VkPhysicalDeviceMaintenance8FeaturesKHR,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_8_FEATURES_KHR);
            FSR4_COPY(VkPhysicalDeviceMaintenance6Features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES);
            FSR4_COPY(VkPhysicalDeviceMaintenance5Features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES);
            FSR4_COPY(VkPhysicalDeviceDynamicRenderingLocalReadFeatures,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES);
            FSR4_COPY(VkPhysicalDeviceVertexAttributeDivisorFeatures,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES);
            FSR4_COPY(VkPhysicalDeviceTransformFeedbackFeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MODULE_IDENTIFIER_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceRobustness2FeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NON_SEAMLESS_CUBE_MAP_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceMultiDrawFeaturesEXT, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceLineRasterizationFeatures,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES);
            FSR4_COPY(VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceExtendedDynamicState3FeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceDescriptorHeapFeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceDepthBiasControlFeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_BIAS_CONTROL_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceCustomBorderColorFeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceBorderColorSwizzleFeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BORDER_COLOR_SWIZZLE_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceShaderFloat16Int8Features,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES);
            FSR4_COPY(VkPhysicalDevice8BitStorageFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES);
            FSR4_COPY(VkPhysicalDeviceDescriptorIndexingFeatures,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES);
            FSR4_COPY(VkPhysicalDeviceBufferDeviceAddressFeatures,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES);
            FSR4_COPY(VkPhysicalDeviceBufferDeviceAddressFeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceShaderIntegerDotProductFeatures,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES);
            FSR4_COPY(VkPhysicalDeviceSynchronization2Features,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES);
            FSR4_COPY(VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES);
            FSR4_COPY(VkPhysicalDeviceSubgroupSizeControlFeatures,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES);
            FSR4_COPY(VkPhysicalDeviceTimelineSemaphoreFeatures,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES);
            FSR4_COPY(VkPhysicalDevice16BitStorageFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES);
            FSR4_COPY(VkPhysicalDeviceMemoryPriorityFeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceDepthClipEnableFeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceCoherentMemoryFeaturesAMD,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COHERENT_MEMORY_FEATURES_AMD);
            FSR4_COPY(VkDeviceMemoryOverallocationCreateInfoAMD,
                      VK_STRUCTURE_TYPE_DEVICE_MEMORY_OVERALLOCATION_CREATE_INFO_AMD);
            FSR4_COPY(VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR);
            FSR4_COPY(VkPhysicalDeviceRayTracingPipelineFeaturesKHR,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR);
            FSR4_COPY(VkPhysicalDeviceAccelerationStructureFeaturesKHR,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR);
            FSR4_COPY(VkPhysicalDeviceRayQueryFeaturesKHR, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR);
            FSR4_COPY(VkPhysicalDeviceFragmentShadingRateFeaturesKHR,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR);
            FSR4_COPY(VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceDescriptorBufferFeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT);
            FSR4_COPY(VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR);
            FSR4_COPY(VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MIXED_FLOAT_DOT_PRODUCT_FEATURES_VALVE);
        default:
            throw std::runtime_error("unhandled device pNext sType=" + std::to_string(node->sType));
        }
#undef FSR4_COPY
    }
    template <class T> T* find(VkStructureType tag)
    {
        for (auto* n = static_cast<const VkBaseInStructure*>(info.pNext); n; n = n->pNext)
            if (n->sType == tag)
                return const_cast<T*>(reinterpret_cast<const T*>(n));
        return nullptr;
    }
    template <class T> T* ensure(VkStructureType tag)
    {
        if (auto* found = find<T>(tag))
            return found;
        T value {};
        value.sType = tag;
        auto* node = copy(value);
        node->pNext = const_cast<void*>(info.pNext);
        info.pNext = node;
        return node;
    }

  public:
    DeviceFeatures(VkPhysicalDevice physical, const VkDeviceCreateInfo& source, uint32_t apiVersion,
                   PFN_vkGetPhysicalDeviceFeatures2 query, PFN_vkEnumerateDeviceExtensionProperties enumerate)
        : info(source)
    {
        if (!query || !enumerate)
            throw std::runtime_error("missing Vulkan feature-query functions");
        if (VK_API_VERSION_MAJOR(apiVersion) < 1 ||
            (VK_API_VERSION_MAJOR(apiVersion) == 1 && VK_API_VERSION_MINOR(apiVersion) < 1))
            throw std::runtime_error("FSR4 requires Vulkan 1.1 or newer");

        VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE mixed {};
        mixed.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MIXED_FLOAT_DOT_PRODUCT_FEATURES_VALVE;
        VkPhysicalDeviceShaderFloatControls2FeaturesKHR floatControls {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT_CONTROLS_2_FEATURES_KHR
        };
        VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR derivatives {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR
        };
        VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptors {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT
        };
        VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutableType {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT
        };
        VkPhysicalDeviceVulkan13Features v13 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        VkPhysicalDeviceVulkan12Features v12 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        VkPhysicalDeviceShaderFloat16Int8Features float16Int8 {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES
        };
        VkPhysicalDevice8BitStorageFeatures storage8 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES };
        VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexing {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES
        };
        VkPhysicalDeviceBufferDeviceAddressFeatures bufferAddress {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES
        };
        VkPhysicalDeviceShaderIntegerDotProductFeatures integerDot {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES
        };
        VkPhysicalDeviceSynchronization2Features synchronization2 {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES
        };
        VkPhysicalDeviceFeatures2 available { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        VkBaseOutStructure* queryTail = reinterpret_cast<VkBaseOutStructure*>(&available);
        const auto appendQuery = [&queryTail](auto& feature)
        {
            queryTail->pNext = reinterpret_cast<VkBaseOutStructure*>(&feature);
            queryTail = reinterpret_cast<VkBaseOutStructure*>(&feature);
        };
        const bool core12 = VK_API_VERSION_MAJOR(apiVersion) > 1 || VK_API_VERSION_MINOR(apiVersion) >= 2;
        const bool core13 = VK_API_VERSION_MAJOR(apiVersion) > 1 || VK_API_VERSION_MINOR(apiVersion) >= 3;
        if (core12)
            appendQuery(v12);
        else
        {
            appendQuery(float16Int8);
            appendQuery(storage8);
            appendQuery(descriptorIndexing);
            appendQuery(bufferAddress);
        }
        if (core13)
            appendQuery(v13);
        else
        {
            appendQuery(synchronization2);
            appendQuery(integerDot);
        }
        appendQuery(mutableType);
        appendQuery(descriptors);
        appendQuery(derivatives);
        appendQuery(mixed);
        appendQuery(floatControls);
        query(physical, &available);
        std::vector<const char*> missingFeatures;
        const auto requireFeature = [&missingFeatures](VkBool32 availableFeature, const char* name)
        {
            if (!availableFeature)
                missingFeatures.push_back(name);
        };
        requireFeature(available.features.shaderInt16, "shaderInt16");
        requireFeature(available.features.shaderStorageImageReadWithoutFormat, "shaderStorageImageReadWithoutFormat");
        requireFeature(available.features.shaderStorageImageWriteWithoutFormat, "shaderStorageImageWriteWithoutFormat");
        requireFeature(core12 ? v12.shaderFloat16 : float16Int8.shaderFloat16, "shaderFloat16");
        requireFeature(core12 ? v12.shaderInt8 : float16Int8.shaderInt8, "shaderInt8");
        requireFeature(core12 ? v12.storageBuffer8BitAccess : storage8.storageBuffer8BitAccess,
                       "storageBuffer8BitAccess");
        requireFeature(core12 ? v12.runtimeDescriptorArray : descriptorIndexing.runtimeDescriptorArray,
                       "runtimeDescriptorArray");
        requireFeature(core12 ? v12.descriptorBindingVariableDescriptorCount
                              : descriptorIndexing.descriptorBindingVariableDescriptorCount,
                       "descriptorBindingVariableDescriptorCount");
        requireFeature(core12 ? v12.bufferDeviceAddress : bufferAddress.bufferDeviceAddress, "bufferDeviceAddress");
        requireFeature(core13 ? v13.synchronization2 : synchronization2.synchronization2, "synchronization2");
        requireFeature(core13 ? v13.shaderIntegerDotProduct : integerDot.shaderIntegerDotProduct,
                       "shaderIntegerDotProduct");
        requireFeature(mutableType.mutableDescriptorType, "mutableDescriptorType");
        requireFeature(descriptors.descriptorBuffer, "descriptorBuffer");
        requireFeature(derivatives.computeDerivativeGroupLinear, "computeDerivativeGroupLinear");
        requireFeature(mixed.shaderMixedFloatDotProductFloat16AccFloat32,
                       "shaderMixedFloatDotProductFloat16AccFloat32");
        requireFeature(floatControls.shaderFloatControls2, "shaderFloatControls2");
        if (!missingFeatures.empty())
        {
            std::string message = "missing FSR4 device feature(s): ";
            for (size_t i = 0; i < missingFeatures.size(); ++i)
            {
                if (i)
                    message += ", ";
                message += missingFeatures[i];
            }
            throw std::runtime_error(message);
        }
        uint32_t count = 0;
        if (enumerate(physical, nullptr, &count, nullptr) != VK_SUCCESS)
            throw std::runtime_error("extension count failed");
        std::vector<VkExtensionProperties> availableExtensions(count);
        if (enumerate(physical, nullptr, &count, availableExtensions.data()) != VK_SUCCESS)
            throw std::runtime_error("extension enumeration failed");
        availableExtensions.resize(count);
        for (uint32_t i = 0; i < source.enabledExtensionCount; ++i)
            if (std::none_of(extensions.begin(), extensions.end(),
                             [&](auto* e) { return std::strcmp(e, source.ppEnabledExtensionNames[i]) == 0; }))
                extensions.push_back(source.ppEnabledExtensionNames[i]);
        std::vector<const char*> requiredExtensions { VK_KHR_SHADER_FLOAT_CONTROLS_2_EXTENSION_NAME,
                                                      VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME,
                                                      VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
                                                      VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME,
                                                      VK_VALVE_SHADER_MIXED_FLOAT_DOT_PRODUCT_EXTENSION_NAME };
        if (!core12)
        {
            requiredExtensions.insert(requiredExtensions.end(),
                                      { VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME, VK_KHR_8BIT_STORAGE_EXTENSION_NAME,
                                        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
                                        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
                                        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME });
        }
        if (!core13)
        {
            requiredExtensions.insert(requiredExtensions.end(), { VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
                                                                  VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME });
        }
        for (auto* required : requiredExtensions)
        {
            if (std::none_of(availableExtensions.begin(), availableExtensions.end(),
                             [&](const auto& e) { return std::strcmp(e.extensionName, required) == 0; }))
                throw std::runtime_error(std::string("missing extension: ") + required);
            if (std::none_of(extensions.begin(), extensions.end(),
                             [&](auto* e) { return std::strcmp(e, required) == 0; }))
                extensions.push_back(required);
        }
        info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        info.pNext = nullptr;
        VkBaseOutStructure* tail = nullptr;
        std::vector<VkStructureType> seen;
        for (auto* n = static_cast<const VkBaseInStructure*>(source.pNext); n; n = n->pNext)
        {
            if (seen.size() >= 64 || std::find(seen.begin(), seen.end(), n->sType) != seen.end())
                throw std::runtime_error("duplicate or cyclic device pNext");
            seen.push_back(n->sType);
            auto* c = clone(n);
            c->pNext = nullptr;
            if (tail)
                tail->pNext = c;
            else
                info.pNext = c;
            tail = c;
        }
        if (auto* f = find<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2))
        {
            if (source.pEnabledFeatures)
                throw std::runtime_error("both legacy and features2 enabled");
            f->features.shaderInt16 = VK_TRUE;
            f->features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
            f->features.shaderStorageImageReadWithoutFormat = VK_TRUE;
        }
        else
        {
            if (source.pEnabledFeatures)
                core = *source.pEnabledFeatures;
            core.shaderInt16 = VK_TRUE;
            core.shaderStorageImageWriteWithoutFormat = VK_TRUE;
            core.shaderStorageImageReadWithoutFormat = VK_TRUE;
            info.pEnabledFeatures = &core;
        }
        auto* e12 = core12
                        ? find<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES)
                        : nullptr;
        auto* e13 = core13
                        ? find<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES)
                        : nullptr;
        if (e12)
        {
            e12->shaderFloat16 = e12->shaderInt8 = e12->storageBuffer8BitAccess = e12->runtimeDescriptorArray =
                e12->descriptorBindingVariableDescriptorCount = e12->bufferDeviceAddress = VK_TRUE;
            e12->shaderStorageBufferArrayNonUniformIndexing |= v12.shaderStorageBufferArrayNonUniformIndexing;
            e12->shaderSampledImageArrayNonUniformIndexing |= v12.shaderSampledImageArrayNonUniformIndexing;
            e12->shaderStorageImageArrayNonUniformIndexing |= v12.shaderStorageImageArrayNonUniformIndexing;
        }
        else
        {
            auto* f = ensure<VkPhysicalDeviceShaderFloat16Int8Features>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES);
            f->shaderFloat16 = f->shaderInt8 = VK_TRUE;
            ensure<VkPhysicalDevice8BitStorageFeatures>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES)
                ->storageBuffer8BitAccess = VK_TRUE;
            auto* d = ensure<VkPhysicalDeviceDescriptorIndexingFeatures>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES);
            d->runtimeDescriptorArray = d->descriptorBindingVariableDescriptorCount = VK_TRUE;
            d->shaderStorageBufferArrayNonUniformIndexing |=
                core12 ? v12.shaderStorageBufferArrayNonUniformIndexing
                       : descriptorIndexing.shaderStorageBufferArrayNonUniformIndexing;
            d->shaderSampledImageArrayNonUniformIndexing |=
                core12 ? v12.shaderSampledImageArrayNonUniformIndexing
                       : descriptorIndexing.shaderSampledImageArrayNonUniformIndexing;
            d->shaderStorageImageArrayNonUniformIndexing |=
                core12 ? v12.shaderStorageImageArrayNonUniformIndexing
                       : descriptorIndexing.shaderStorageImageArrayNonUniformIndexing;
            ensure<VkPhysicalDeviceBufferDeviceAddressFeatures>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES)
                ->bufferDeviceAddress = VK_TRUE;
        }
        if (e13)
            e13->synchronization2 = e13->shaderIntegerDotProduct = VK_TRUE;
        else
        {
            ensure<VkPhysicalDeviceSynchronization2Features>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES)
                ->synchronization2 = VK_TRUE;
            ensure<VkPhysicalDeviceShaderIntegerDotProductFeatures>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES)
                ->shaderIntegerDotProduct = VK_TRUE;
        }
        ensure<VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT)
            ->mutableDescriptorType = VK_TRUE;
        ensure<VkPhysicalDeviceDescriptorBufferFeaturesEXT>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT)
            ->descriptorBuffer = VK_TRUE;
        ensure<VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR)
            ->computeDerivativeGroupLinear = VK_TRUE;
        ensure<VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MIXED_FLOAT_DOT_PRODUCT_FEATURES_VALVE)
            ->shaderMixedFloatDotProductFloat16AccFloat32 = VK_TRUE;
        if (auto* e14 = find<VkPhysicalDeviceVulkan14Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES))
            e14->shaderFloatControls2 = VK_TRUE;
        else
            ensure<VkPhysicalDeviceShaderFloatControls2FeaturesKHR>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT_CONTROLS_2_FEATURES_KHR)
                ->shaderFloatControls2 = VK_TRUE;
    }
    DeviceFeatures(const DeviceFeatures&) = delete;
    DeviceFeatures& operator=(const DeviceFeatures&) = delete;
    const VkDeviceCreateInfo* get() const { return &info; }
};
} // namespace fsr4vk
