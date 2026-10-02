#pragma once

#include <vulkan/vulkan.h>

#include <initializer_list>
#include <stdexcept>
#include <string>
#include <cstring>

namespace fsr4vk {

template <typename Function>
inline Function
load_device_function(VkDevice device,
                     PFN_vkGetDeviceProcAddr get_device_proc_addr,
                     std::initializer_list<const char *> names) {
  if (!device || !get_device_proc_addr) {
    throw std::invalid_argument("missing Vulkan device dispatch");
  }
  for (const char *name : names) {
    if (auto function = get_device_proc_addr(device, name)) {
      return reinterpret_cast<Function>(function);
    }
  }
  std::string message = "FSR4VK_ERROR_MISSING_DEVICE_FEATURES: missing Vulkan device entry point: ";
  bool first = true;
  for (const char *name : names) {
    if (!first)
      message += " or ";
    message += name;
    first = false;
  }
  const auto* command=names.size() ? *names.begin() : "";
  if(std::strstr(command,"Descriptor"))
    message += "; required extension=VK_EXT_descriptor_buffer; required feature=VkPhysicalDeviceDescriptorBufferFeaturesEXT.descriptorBuffer=VK_TRUE";
  else if(std::strstr(command,"BufferDeviceAddress"))
    message += "; required API=Vulkan 1.2 or extension VK_KHR_buffer_device_address; required feature=VkPhysicalDeviceBufferDeviceAddressFeatures.bufferDeviceAddress=VK_TRUE (or VkPhysicalDeviceVulkan12Features.bufferDeviceAddress)";
  else if(std::strstr(command,"PipelineBarrier2") || std::strstr(command,"SetEvent2") ||
          std::strstr(command,"WriteTimestamp2") || std::strstr(command,"QueueSubmit2"))
    message += "; required API=Vulkan 1.3 or extension VK_KHR_synchronization2; required feature=VkPhysicalDeviceSynchronization2Features.synchronization2=VK_TRUE (or VkPhysicalDeviceVulkan13Features.synchronization2)";
  message += "; enable at vkCreateDevice, then recreate the device. Missing entry point is observed; the loader does not reveal which feature bit was disabled";
  throw std::runtime_error(message);
}

inline void validate_descriptor_commands(VkDevice device, PFN_vkGetDeviceProcAddr get) {
  for (const char* name : {"vkGetDescriptorEXT", "vkGetDescriptorSetLayoutSizeEXT",
       "vkGetDescriptorSetLayoutBindingOffsetEXT", "vkCmdBindDescriptorBuffersEXT",
       "vkCmdSetDescriptorBufferOffsetsEXT", "vkCmdBindDescriptorBufferEmbeddedSamplersEXT"})
    (void)load_device_function<PFN_vkVoidFunction>(device,get,{name});
}

template <typename Function>
inline Function load_promoted_device_function(
    VkDevice device, PFN_vkGetDeviceProcAddr get_device_proc_addr,
    uint32_t api_version, const char *core_name, const char *khr_name,
    uint32_t promoted_version=VK_API_VERSION_1_3) {
  const bool prefer_core = VK_API_VERSION_MAJOR(api_version) > 1 ||
                           (VK_API_VERSION_MAJOR(api_version) == 1 &&
                            VK_API_VERSION_MINOR(api_version) >= VK_API_VERSION_MINOR(promoted_version));
  if (prefer_core) {
    return load_device_function<Function>(device, get_device_proc_addr,
                                          {core_name, khr_name});
  }
  return load_device_function<Function>(device, get_device_proc_addr,
                                        {khr_name, core_name});
}

// Vulkan 1.2/1.3 commands used by the provider were promoted from extensions.
// Vulkan 1.3 applications prefer the core spellings. Vulkan 1.1 and 1.2 use
// the compatibility path and resolve the KHR spellings first: some loaders
// return non-null core trampolines for older instances whose driver dispatch
// slots are empty. The second spelling remains a defensive fallback.
struct DeviceDispatch {
  PFN_vkCmdPipelineBarrier2 cmd_pipeline_barrier2 = nullptr;
  PFN_vkCmdSetEvent2 cmd_set_event2 = nullptr;
  PFN_vkCmdWriteTimestamp2 cmd_write_timestamp2 = nullptr;
  PFN_vkGetBufferDeviceAddress get_buffer_device_address = nullptr;
  PFN_vkQueueSubmit2 queue_submit2 = nullptr;

  DeviceDispatch() = default;
  DeviceDispatch(VkDevice device,
                 PFN_vkGetDeviceProcAddr get_device_proc_addr,
                 uint32_t api_version = VK_API_VERSION_1_3, bool require_submission=true) {
    load(device, get_device_proc_addr, api_version,require_submission);
  }

  void load(VkDevice device, PFN_vkGetDeviceProcAddr get_device_proc_addr,
            uint32_t api_version = VK_API_VERSION_1_3, bool require_submission=true) {
    cmd_pipeline_barrier2 =
        load_promoted_device_function<PFN_vkCmdPipelineBarrier2>(
            device, get_device_proc_addr, api_version,
            "vkCmdPipelineBarrier2", "vkCmdPipelineBarrier2KHR");
    cmd_set_event2 = load_promoted_device_function<PFN_vkCmdSetEvent2>(
        device, get_device_proc_addr, api_version, "vkCmdSetEvent2",
        "vkCmdSetEvent2KHR");
    cmd_write_timestamp2 =
        load_promoted_device_function<PFN_vkCmdWriteTimestamp2>(
            device, get_device_proc_addr, api_version,
            "vkCmdWriteTimestamp2", "vkCmdWriteTimestamp2KHR");
    get_buffer_device_address =
        load_promoted_device_function<PFN_vkGetBufferDeviceAddress>(
            device, get_device_proc_addr, api_version,
            "vkGetBufferDeviceAddress", "vkGetBufferDeviceAddressKHR",VK_API_VERSION_1_2);
    queue_submit2 = require_submission ? load_promoted_device_function<PFN_vkQueueSubmit2>(
        device, get_device_proc_addr, api_version, "vkQueueSubmit2",
        "vkQueueSubmit2KHR") : nullptr;
  }
};

struct DescriptorDispatch {
  PFN_vkGetDescriptorSetLayoutSizeEXT get_layout_size{};
  PFN_vkGetDescriptorSetLayoutBindingOffsetEXT get_binding_offset{};
  PFN_vkGetDescriptorEXT get_descriptor{};
  PFN_vkCmdBindDescriptorBuffersEXT bind_descriptor_buffers{};
  PFN_vkCmdSetDescriptorBufferOffsetsEXT set_descriptor_offsets{};
  PFN_vkCmdBindDescriptorBufferEmbeddedSamplersEXT bind_embedded_samplers{};
  DescriptorDispatch()=default;
  DescriptorDispatch(VkDevice device,PFN_vkGetDeviceProcAddr get) {
    get_layout_size=load_device_function<PFN_vkGetDescriptorSetLayoutSizeEXT>(device,get,{"vkGetDescriptorSetLayoutSizeEXT"});
    get_binding_offset=load_device_function<PFN_vkGetDescriptorSetLayoutBindingOffsetEXT>(device,get,{"vkGetDescriptorSetLayoutBindingOffsetEXT"});
    get_descriptor=load_device_function<PFN_vkGetDescriptorEXT>(device,get,{"vkGetDescriptorEXT"});
    bind_descriptor_buffers=load_device_function<PFN_vkCmdBindDescriptorBuffersEXT>(device,get,{"vkCmdBindDescriptorBuffersEXT"});
    set_descriptor_offsets=load_device_function<PFN_vkCmdSetDescriptorBufferOffsetsEXT>(device,get,{"vkCmdSetDescriptorBufferOffsetsEXT"});
    bind_embedded_samplers=load_device_function<PFN_vkCmdBindDescriptorBufferEmbeddedSamplersEXT>(device,get,{"vkCmdBindDescriptorBufferEmbeddedSamplersEXT"});
  }
};

} // namespace fsr4vk
