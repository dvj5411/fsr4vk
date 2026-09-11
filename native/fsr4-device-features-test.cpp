#include "../provider/vulkan_device_features.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<const char *> available_extensions;
bool expose_synchronization2 = true;
bool expose_mixed_dot = true;

void VKAPI_CALL query_features(VkPhysicalDevice,
                               VkPhysicalDeviceFeatures2 *features) {
  features->features.shaderInt16 = VK_TRUE;
  features->features.shaderStorageImageReadWithoutFormat = VK_TRUE;
  features->features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
  for (auto *node = reinterpret_cast<VkBaseOutStructure *>(features->pNext);
       node; node = node->pNext) {
    switch (static_cast<int>(node->sType)) {
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: {
      auto *value = reinterpret_cast<VkPhysicalDeviceVulkan12Features *>(node);
      value->shaderFloat16 = value->shaderInt8 =
          value->storageBuffer8BitAccess = VK_TRUE;
      value->runtimeDescriptorArray =
          value->descriptorBindingVariableDescriptorCount = VK_TRUE;
      value->bufferDeviceAddress = VK_TRUE;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES: {
      auto *value = reinterpret_cast<VkPhysicalDeviceVulkan13Features *>(node);
      value->synchronization2 = expose_synchronization2;
      value->shaderIntegerDotProduct = VK_TRUE;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES: {
      auto *value =
          reinterpret_cast<VkPhysicalDeviceShaderFloat16Int8Features *>(node);
      value->shaderFloat16 = value->shaderInt8 = VK_TRUE;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES:
      reinterpret_cast<VkPhysicalDevice8BitStorageFeatures *>(node)
          ->storageBuffer8BitAccess = VK_TRUE;
      break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES: {
      auto *value =
          reinterpret_cast<VkPhysicalDeviceDescriptorIndexingFeatures *>(node);
      value->runtimeDescriptorArray =
          value->descriptorBindingVariableDescriptorCount = VK_TRUE;
      break;
    }
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES:
      reinterpret_cast<VkPhysicalDeviceBufferDeviceAddressFeatures *>(node)
          ->bufferDeviceAddress = VK_TRUE;
      break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES:
      reinterpret_cast<VkPhysicalDeviceSynchronization2Features *>(node)
          ->synchronization2 = expose_synchronization2;
      break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES:
      reinterpret_cast<VkPhysicalDeviceShaderIntegerDotProductFeatures *>(node)
          ->shaderIntegerDotProduct = VK_TRUE;
      break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT:
      reinterpret_cast<VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT *>(node)
          ->mutableDescriptorType = VK_TRUE;
      break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT:
      reinterpret_cast<VkPhysicalDeviceDescriptorBufferFeaturesEXT *>(node)
          ->descriptorBuffer = VK_TRUE;
      break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR:
      reinterpret_cast<VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR *>(
          node)
          ->computeDerivativeGroupLinear = VK_TRUE;
      break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MIXED_FLOAT_DOT_PRODUCT_FEATURES_VALVE:
      reinterpret_cast<
          VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE *>(node)
          ->shaderMixedFloatDotProductFloat16AccFloat32 = expose_mixed_dot;
      break;
    case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT_CONTROLS_2_FEATURES_KHR:
      reinterpret_cast<VkPhysicalDeviceShaderFloatControls2FeaturesKHR *>(node)
          ->shaderFloatControls2 = VK_TRUE;
      break;
    default:
      break;
    }
  }
}

VkResult VKAPI_CALL enumerate_extensions(VkPhysicalDevice, const char *,
                                         uint32_t *count,
                                         VkExtensionProperties *properties) {
  if (!properties) {
    *count = static_cast<uint32_t>(available_extensions.size());
    return VK_SUCCESS;
  }
  const uint32_t written =
      std::min(*count, static_cast<uint32_t>(available_extensions.size()));
  for (uint32_t index = 0; index < written; ++index) {
    std::strncpy(properties[index].extensionName, available_extensions[index],
                 VK_MAX_EXTENSION_NAME_SIZE - 1);
    properties[index].extensionName[VK_MAX_EXTENSION_NAME_SIZE - 1] = '\0';
  }
  *count = written;
  return written == available_extensions.size() ? VK_SUCCESS : VK_INCOMPLETE;
}

bool has_extension(const VkDeviceCreateInfo *info, const char *name) {
  for (uint32_t index = 0; index < info->enabledExtensionCount; ++index)
    if (std::strcmp(info->ppEnabledExtensionNames[index], name) == 0)
      return true;
  return false;
}

bool has_structure(const VkDeviceCreateInfo *info, VkStructureType type) {
  for (auto *node = static_cast<const VkBaseInStructure *>(info->pNext); node;
       node = node->pNext)
    if (node->sType == type)
      return true;
  return false;
}

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
} // namespace

int main() {
  available_extensions = {
      VK_KHR_SHADER_FLOAT_CONTROLS_2_EXTENSION_NAME,
      VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME,
      VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
      VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME,
      VK_VALVE_SHADER_MIXED_FLOAT_DOT_PRODUCT_EXTENSION_NAME,
      VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,
      VK_KHR_8BIT_STORAGE_EXTENSION_NAME,
      VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
      VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
      VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
      VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
      VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME,
  };
  const char *existing_extension = "VK_KHR_swapchain";
  VkDeviceCreateInfo source{};
  source.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  source.enabledExtensionCount = 1;
  source.ppEnabledExtensionNames = &existing_extension;
  VkPhysicalDeviceSynchronization2Features source_synchronization{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
  VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures shader_demote{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES};
  shader_demote.shaderDemoteToHelperInvocation = VK_TRUE;
  VkPhysicalDeviceTimelineSemaphoreFeatures timeline{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
  VkPhysicalDeviceSubgroupSizeControlFeatures subgroup_size{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES};
  VkPhysicalDeviceBufferDeviceAddressFeaturesEXT legacy_address{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT};
  legacy_address.bufferDeviceAddress = VK_TRUE;
  VkPhysicalDeviceFeatures2 source_features{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  VkPhysicalDeviceDescriptorIndexingFeatures source_descriptor_indexing{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
  VkPhysicalDeviceShaderIntegerDotProductFeatures source_integer_dot{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES};
  VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT source_mutable_descriptor{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT};
  VkPhysicalDeviceShaderFloat16Int8Features source_float16_int8{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
  VkPhysicalDevice16BitStorageFeatures storage16{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
  VkPhysicalDeviceMemoryPriorityFeaturesEXT memory_priority{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT};
  VkPhysicalDeviceDepthClipEnableFeaturesEXT depth_clip{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT};
  depth_clip.depthClipEnable = VK_TRUE;
  VkPhysicalDeviceCoherentMemoryFeaturesAMD coherent_memory{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COHERENT_MEMORY_FEATURES_AMD};
  coherent_memory.deviceCoherentMemory = VK_TRUE;
  VkDeviceMemoryOverallocationCreateInfoAMD memory_overallocation{
      VK_STRUCTURE_TYPE_DEVICE_MEMORY_OVERALLOCATION_CREATE_INFO_AMD};
  memory_overallocation.overallocationBehavior =
      VK_MEMORY_OVERALLOCATION_BEHAVIOR_DISALLOWED_AMD;
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_pipeline{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
  ray_tracing_pipeline.rayTracingPipeline = VK_TRUE;
  VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  acceleration_structure.accelerationStructure = VK_TRUE;
  VkPhysicalDeviceRayQueryFeaturesKHR ray_query{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
  ray_query.rayQuery = VK_TRUE;
  VkPhysicalDeviceFragmentShadingRateFeaturesKHR fragment_shading_rate{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
  fragment_shading_rate.pipelineFragmentShadingRate = VK_TRUE;
  source_synchronization.pNext = &shader_demote;
  shader_demote.pNext = &timeline;
  timeline.pNext = &subgroup_size;
  subgroup_size.pNext = &legacy_address;
  legacy_address.pNext = &source_features;
  source_features.pNext = &source_descriptor_indexing;
  source_descriptor_indexing.pNext = &source_integer_dot;
  source_integer_dot.pNext = &source_mutable_descriptor;
  source_mutable_descriptor.pNext = &source_float16_int8;
  source_float16_int8.pNext = &storage16;
  storage16.pNext = &memory_priority;
  memory_priority.pNext = &depth_clip;
  depth_clip.pNext = &coherent_memory;
  coherent_memory.pNext = &memory_overallocation;
  memory_overallocation.pNext = &ray_tracing_pipeline;
  ray_tracing_pipeline.pNext = &acceleration_structure;
  acceleration_structure.pNext = &ray_query;
  ray_query.pNext = &fragment_shading_rate;
  source.pNext = &source_synchronization;

  fsr4vk::DeviceFeatures vk11(reinterpret_cast<VkPhysicalDevice>(1), source,
                              VK_API_VERSION_1_1, query_features,
                              enumerate_extensions);
  const auto *vk11_info = vk11.get();
  require(has_extension(vk11_info, existing_extension),
          "source extension was dropped");
  require(has_extension(vk11_info, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME),
          "Vulkan 1.1 float16/int8 extension missing");
  require(has_extension(vk11_info, VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME),
          "Vulkan 1.1 float-controls extension missing");
  require(has_extension(vk11_info, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME),
          "Vulkan 1.1 synchronization2 extension missing");
  require(has_extension(vk11_info,
                        VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME),
          "Vulkan 1.1 integer-dot extension missing");
  require(has_structure(
              vk11_info,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES),
          "Vulkan 1.1 float16/int8 feature struct missing");
  require(has_structure(
              vk11_info,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES),
          "Vulkan 1.1 synchronization2 feature struct missing");
  require(has_structure(
              vk11_info,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT),
          "source EXT buffer-address feature struct was dropped");
  require(has_structure(
              vk11_info,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES),
          "Vulkan 1.1 KHR buffer-address feature struct missing");
  require(has_structure(
              vk11_info,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES),
          "source shader-demote feature struct was dropped");
  require(has_structure(
              vk11_info,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES),
          "source subgroup-size-control feature struct was dropped");
  require(has_structure(
              vk11_info,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT),
          "source depth-clip feature struct was dropped");
  require(has_structure(
              vk11_info,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COHERENT_MEMORY_FEATURES_AMD),
          "source coherent-memory feature struct was dropped");
  require(has_structure(
              vk11_info,
              VK_STRUCTURE_TYPE_DEVICE_MEMORY_OVERALLOCATION_CREATE_INFO_AMD),
          "source memory-overallocation device struct was dropped");
  require(has_structure(
              vk11_info,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR),
          "source ray-tracing-pipeline feature struct was dropped");
  require(has_structure(
              vk11_info,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR),
          "source acceleration-structure feature struct was dropped");
  require(has_structure(
              vk11_info,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR),
          "source ray-query feature struct was dropped");
  require(has_structure(
              vk11_info,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR),
          "source fragment-shading-rate feature struct was dropped");

  fsr4vk::DeviceFeatures vk12(reinterpret_cast<VkPhysicalDevice>(1), source,
                              VK_API_VERSION_1_2, query_features,
                              enumerate_extensions);
  require(
      !has_extension(vk12.get(), VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME),
      "Vulkan 1.2 path unnecessarily requires promoted float16/int8 extension");
  require(has_extension(vk12.get(), VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME),
          "Vulkan 1.2 path omitted synchronization2 extension");
  require(has_extension(vk12.get(),
                        VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME),
          "Vulkan 1.2 path omitted integer-dot extension");

  fsr4vk::DeviceFeatures vk13(reinterpret_cast<VkPhysicalDevice>(1), source,
                              VK_API_VERSION_1_3, query_features,
                              enumerate_extensions);
  require(!has_extension(vk13.get(), VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME),
          "Vulkan 1.3 path unnecessarily requires promoted synchronization2 "
          "extension");
  require(
      !has_extension(vk13.get(), VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME),
      "Vulkan 1.3 path unnecessarily requires promoted float16/int8 extension");

  // Reproduce real device chains recorded before vkCreateDevice. Oversized,
  // aligned storage lets the copier exercise every typed case without making
  // this regression test depend on newer Vulkan-header type declarations.
  struct alignas(std::max_align_t) LoggedNode {
    VkStructureType sType{};
    void *pNext{};
    std::array<std::byte, 1024> payload{};
  };
  const auto verify_logged_chain = [&](const std::vector<int> &types,
                                       const char *name) {
    std::vector<LoggedNode> nodes(types.size());
    for (size_t index = 0; index < nodes.size(); ++index) {
      nodes[index].sType = static_cast<VkStructureType>(types[index]);
      nodes[index].pNext =
          index + 1 < nodes.size() ? static_cast<void *>(&nodes[index + 1])
                                   : nullptr;
    }
    VkDeviceCreateInfo logged_source = source;
    logged_source.pNext = nodes.data();
    fsr4vk::DeviceFeatures copied(reinterpret_cast<VkPhysicalDevice>(1),
                                  logged_source, VK_API_VERSION_1_3,
                                  query_features, enumerate_extensions);
    for (const auto type : types)
      require(has_structure(copied.get(), static_cast<VkStructureType>(type)),
              name);
  };
  verify_logged_chain(
      {1000059000, 49, 51, 53, 1000189000, 1000347000, 1000150013,
       1000348013, 1000226003},
      "Doom device-chain structure was dropped");
  verify_logged_chain(
      {1000527000, 1000275000, 1000387000, 1000323000, 1000528000,
       1000248000, 1000294001, 1000630000, 1000584000, 1000574000,
       1000545000, 1000470000, 1000232000, 1000190002, 1000028000,
       1000462000, 1000286000, 1000422000, 1000392000, 1000238000,
       1000259000, 1000320000, 1000251000, 1000455000, 1000499000,
       1000135009, 1000283000, 1000102000, 1000287002, 1000411000,
       1000339000, 53, 51, 49},
      "Endfield device-chain structure was dropped");

  for (bool extension_present : {false, true}) {
    const auto saved_extensions = available_extensions;
    if (!extension_present)
      available_extensions.erase(
          std::remove(available_extensions.begin(), available_extensions.end(),
                      VK_VALVE_SHADER_MIXED_FLOAT_DOT_PRODUCT_EXTENSION_NAME),
          available_extensions.end());
    for (bool feature_present : {false, true}) {
      expose_mixed_dot = feature_present;
      const bool expected_native = extension_present && feature_present;
      require(fsr4vk::supportsNativeMixedDot(reinterpret_cast<VkPhysicalDevice>(1),
                  query_features, enumerate_extensions) == expected_native,
              "incorrect shader backend selection");
      fsr4vk::DeviceFeatures prepared(reinterpret_cast<VkPhysicalDevice>(1),
                                     source, VK_API_VERSION_1_1,
                                     query_features, enumerate_extensions);
      require(has_extension(prepared.get(),
                  VK_VALVE_SHADER_MIXED_FLOAT_DOT_PRODUCT_EXTENSION_NAME) == expected_native,
              "mixed-dot extension must match selected shader backend");
      require(has_structure(prepared.get(),
                  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MIXED_FLOAT_DOT_PRODUCT_FEATURES_VALVE)
                  == expected_native,
              "mixed-dot feature must match selected shader backend");
    }
    available_extensions = saved_extensions;
  }
  expose_mixed_dot = true;

  expose_synchronization2 = false;
  try {
    fsr4vk::DeviceFeatures missing(reinterpret_cast<VkPhysicalDevice>(1),
                                   source, VK_API_VERSION_1_1, query_features,
                                   enumerate_extensions);
    (void)missing;
    throw std::runtime_error("missing synchronization2 feature was accepted");
  } catch (const std::runtime_error &error) {
    require(std::strstr(error.what(), "synchronization2") != nullptr,
            "missing-feature diagnostic does not name synchronization2");
  }

  expose_synchronization2 = true;
  available_extensions.erase(
      std::remove(available_extensions.begin(), available_extensions.end(),
                  VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME),
      available_extensions.end());
  try {
    fsr4vk::DeviceFeatures missing(reinterpret_cast<VkPhysicalDevice>(1),
                                   source, VK_API_VERSION_1_1, query_features,
                                   enumerate_extensions);
    (void)missing;
    throw std::runtime_error("missing synchronization2 extension was accepted");
  } catch (const std::runtime_error &error) {
    require(
        std::strstr(error.what(), VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) !=
            nullptr,
        "missing-extension diagnostic does not name VK_KHR_synchronization2");
  }

  try {
    fsr4vk::DeviceFeatures vk10(reinterpret_cast<VkPhysicalDevice>(1), source,
                                VK_API_VERSION_1_0, query_features,
                                enumerate_extensions);
    (void)vk10;
    throw std::runtime_error("Vulkan 1.0 was accepted");
  } catch (const std::runtime_error &error) {
    require(std::strstr(error.what(), "Vulkan 1.1") != nullptr,
            "Vulkan 1.0 rejection does not name the minimum version");
  }

  std::cout << "Vulkan 1.1 device feature-chain tests passed\n";
}
