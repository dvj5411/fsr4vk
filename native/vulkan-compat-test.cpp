#include "vulkan-compat.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
bool expose_core = false;
bool expose_khr = false;
bool expose_submit = true;
std::vector<std::string> requested_names;

VKAPI_ATTR void VKAPI_CALL fake_barrier(VkCommandBuffer,
                                        const VkDependencyInfo *) {}
VKAPI_ATTR void VKAPI_CALL fake_event(VkCommandBuffer, VkEvent,
                                      const VkDependencyInfo *) {}
VKAPI_ATTR void VKAPI_CALL fake_timestamp(VkCommandBuffer,
                                          VkPipelineStageFlags2, VkQueryPool,
                                          uint32_t) {}
VKAPI_ATTR VkDeviceAddress VKAPI_CALL
fake_address(VkDevice, const VkBufferDeviceAddressInfo *) {
  return 1;
}
VKAPI_ATTR VkResult VKAPI_CALL fake_submit(VkQueue, uint32_t,
                                           const VkSubmitInfo2 *, VkFence) {
  return VK_SUCCESS;
}

PFN_vkVoidFunction VKAPI_CALL fake_get_device_proc_addr(VkDevice,
                                                        const char *name) {
  requested_names.emplace_back(name);
  if (!expose_submit && std::strstr(name, "QueueSubmit2")) return nullptr;
  const bool core = std::strcmp(name, "vkCmdPipelineBarrier2") == 0 ||
                    std::strcmp(name, "vkCmdSetEvent2") == 0 ||
                    std::strcmp(name, "vkCmdWriteTimestamp2") == 0 ||
                    std::strcmp(name, "vkGetBufferDeviceAddress") == 0 ||
                    std::strcmp(name, "vkQueueSubmit2") == 0;
  const bool khr = std::strcmp(name, "vkCmdPipelineBarrier2KHR") == 0 ||
                   std::strcmp(name, "vkCmdSetEvent2KHR") == 0 ||
                   std::strcmp(name, "vkCmdWriteTimestamp2KHR") == 0 ||
                   std::strcmp(name, "vkGetBufferDeviceAddressKHR") == 0 ||
                   std::strcmp(name, "vkQueueSubmit2KHR") == 0;
  if ((core && !expose_core) || (khr && !expose_khr))
    return nullptr;
  if (std::strstr(name, "PipelineBarrier2"))
    return reinterpret_cast<PFN_vkVoidFunction>(fake_barrier);
  if (std::strstr(name, "SetEvent2"))
    return reinterpret_cast<PFN_vkVoidFunction>(fake_event);
  if (std::strstr(name, "WriteTimestamp2"))
    return reinterpret_cast<PFN_vkVoidFunction>(fake_timestamp);
  if (std::strstr(name, "GetBufferDeviceAddress"))
    return reinterpret_cast<PFN_vkVoidFunction>(fake_address);
  if (std::strstr(name, "QueueSubmit2"))
    return reinterpret_cast<PFN_vkVoidFunction>(fake_submit);
  return nullptr;
}

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
} // namespace

int main() {
  const auto device = reinterpret_cast<VkDevice>(1);
  expose_khr = true;
  expose_core = true;
  requested_names.clear();
  fsr4vk::DeviceDispatch core(device, fake_get_device_proc_addr,
                              VK_API_VERSION_1_3);
  require(core.cmd_pipeline_barrier2 == fake_barrier,
          "core barrier was not selected");
  require(core.get_buffer_device_address == fake_address,
          "core address was not selected");
  require(core.queue_submit2 == fake_submit, "core submit was not selected");
  require(!requested_names.empty() &&
              requested_names.front() == "vkCmdPipelineBarrier2",
          "Vulkan 1.3 did not prefer the core path");

  requested_names.clear();
  fsr4vk::DeviceDispatch khr12(device, fake_get_device_proc_addr,
                               VK_API_VERSION_1_2);
  require(khr12.cmd_pipeline_barrier2 == fake_barrier,
          "Vulkan 1.2 barrier path failed");
  require(!requested_names.empty() &&
              requested_names.front() == "vkCmdPipelineBarrier2KHR",
          "Vulkan 1.2 did not prefer the compatibility path");
  require(std::find(requested_names.begin(), requested_names.end(), "vkGetBufferDeviceAddressKHR") == requested_names.end(),
          "Vulkan 1.2 did not prefer its promoted buffer-address command");

  expose_submit = false;
  requested_names.clear();
  fsr4vk::DeviceDispatch recording(device, fake_get_device_proc_addr, VK_API_VERSION_1_3, false);
  require(recording.queue_submit2 == nullptr, "recording-only dispatch loaded submission commands");
  require(std::none_of(requested_names.begin(), requested_names.end(), [](const auto& name) {
    return name.find("QueueSubmit2") != std::string::npos;
  }), "recording-only dispatch queried unused submission commands");
  expose_submit = true;

  expose_core = false;
  requested_names.clear();
  fsr4vk::DeviceDispatch khr(device, fake_get_device_proc_addr,
                             VK_API_VERSION_1_1);
  require(khr.cmd_pipeline_barrier2 == fake_barrier,
          "KHR barrier fallback failed");
  require(khr.cmd_set_event2 == fake_event, "KHR event fallback failed");
  require(khr.cmd_write_timestamp2 == fake_timestamp,
          "KHR timestamp fallback failed");
  require(khr.get_buffer_device_address == fake_address,
          "KHR address fallback failed");
  require(khr.queue_submit2 == fake_submit, "KHR submit fallback failed");
  require(!requested_names.empty() &&
              requested_names.front() == "vkCmdPipelineBarrier2KHR",
          "Vulkan 1.1 did not prefer the compatibility path");

  expose_khr = false;
  try {
    fsr4vk::DeviceDispatch missing(device, fake_get_device_proc_addr,
                                   VK_API_VERSION_1_1);
    (void)missing;
    throw std::runtime_error("missing entry points were accepted");
  } catch (const std::runtime_error &error) {
    require(std::strstr(error.what(), "vkCmdPipelineBarrier2KHR") != nullptr &&
                std::strstr(error.what(), "vkCmdPipelineBarrier2") != nullptr,
            "missing-entry-point diagnostic is not actionable");
  }

  std::cout << "Vulkan promoted-command dispatch tests passed\n";
}
