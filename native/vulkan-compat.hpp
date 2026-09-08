#pragma once

#include <vulkan/vulkan.h>

#include <initializer_list>
#include <stdexcept>
#include <string>

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
  std::string message = "missing Vulkan device entry point: ";
  bool first = true;
  for (const char *name : names) {
    if (!first)
      message += " or ";
    message += name;
    first = false;
  }
  throw std::runtime_error(message);
}

template <typename Function>
inline Function load_promoted_device_function(
    VkDevice device, PFN_vkGetDeviceProcAddr get_device_proc_addr,
    uint32_t api_version, const char *core_name, const char *khr_name) {
  const bool prefer_core = VK_API_VERSION_MAJOR(api_version) > 1 ||
                           (VK_API_VERSION_MAJOR(api_version) == 1 &&
                            VK_API_VERSION_MINOR(api_version) >= 3);
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
                 uint32_t api_version = VK_API_VERSION_1_3) {
    load(device, get_device_proc_addr, api_version);
  }

  void load(VkDevice device, PFN_vkGetDeviceProcAddr get_device_proc_addr,
            uint32_t api_version = VK_API_VERSION_1_3) {
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
            "vkGetBufferDeviceAddress", "vkGetBufferDeviceAddressKHR");
    queue_submit2 = load_promoted_device_function<PFN_vkQueueSubmit2>(
        device, get_device_proc_addr, api_version, "vkQueueSubmit2",
        "vkQueueSubmit2KHR");
  }
};

} // namespace fsr4vk
