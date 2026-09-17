// Exercise the actual Windows provider's optional device-preparation ABI.
// No rendering, queue submission, model assets, or game prefix is required.
#include <windows.h>
#include <vulkan/vulkan.h>
#include "../amd-fidelityfx-sdk/Kits/FidelityFX/upscalers/include/ffx_upscale.h"
#include "../provider/ffx_vk_device_requirements.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void check(VkResult result, const char* name) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(name) + " result=" + std::to_string(result));
}

int main(int argc, char** argv) {
    const bool rdr2Context = argc == 3 && std::string(argv[2]) == "--rdr2-context";
    if (argc != 2 && !rdr2Context) {
        std::cerr << "usage: vulkan-device-negotiation-probe.exe PROVIDER.dll [--rdr2-context]\n";
        return 2;
    }
    HMODULE module = LoadLibraryA(argv[1]);
    if (!module) {
        std::cerr << "provider_load_error=" << GetLastError() << '\n';
        return 1;
    }
    auto query = reinterpret_cast<PfnFfxQuery>(GetProcAddress(module, "ffxQuery"));
    if (!query) {
        FreeLibrary(module);
        return 1;
    }
    VkInstance instance{};
    VkDevice device{};
    ffxQueryDescVkPrepareDevice request{};
    const auto release = [&] {
        if (!request.token) return;
        ffxQueryDescVkReleaseDevice done{};
        done.header.type = FFX_API_QUERY_DESC_TYPE_VK_RELEASE_DEVICE;
        done.token = request.token;
        const auto result = query(nullptr, &done.header);
        request.token = nullptr;
        std::cout << "release_result=" << result << '\n';
    };
    int result = 0;
    try {
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "fsr4-device-negotiation-probe";
        application.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instance_info.pApplicationInfo = &application;
        check(vkCreateInstance(&instance_info, nullptr, &instance), "vkCreateInstance");
        uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "physical_count");
        if (!count) throw std::runtime_error("no physical device");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "physical_devices");
        VkPhysicalDevice physical = devices.front();
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        std::cout << "device=" << properties.deviceName << '\n';

        VkPhysicalDeviceDynamicRenderingFeatures rendering{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
        VkPhysicalDeviceFeatures2 available{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        available.pNext = &rendering;
        vkGetPhysicalDeviceFeatures2(physical, &available);
        if (!rendering.dynamicRendering)
            throw std::runtime_error("test requires dynamicRendering support");
        VkPhysicalDeviceTimelineSemaphoreFeatures timeline{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
        rendering.pNext = &timeline;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues.data());
        uint32_t family = 0;
        while (family < count && !(queues[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++family;
        if (family == count) throw std::runtime_error("no compute queue family");
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_info.queueFamilyIndex = family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;
        VkDeviceCreateInfo source{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        source.pNext = &rendering;
        source.queueCreateInfoCount = 1;
        source.pQueueCreateInfos = &queue_info;
        const auto checkContext = [&](VkDevice target, bool expectedSuccess) {
            auto create = reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(module, "ffxCreateContext"));
            auto destroy = reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(module, "ffxDestroyContext"));
            if (!create || !destroy) throw std::runtime_error("context exports missing");
            struct Backend {
                ffxCreateContextDescHeader header;
                VkDevice device;
                VkPhysicalDevice physicalDevice;
                PFN_vkGetDeviceProcAddr getDeviceProcAddr;
            } backend{};
            backend.header.type = 3; // FFX Vulkan backend descriptor ABI.
            backend.device = target;
            backend.physicalDevice = physical;
            backend.getDeviceProcAddr = vkGetDeviceProcAddr;
            ffxCreateContextDescUpscale desc{};
            desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            desc.header.pNext = &backend.header;
            desc.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_DEPTH_INVERTED;
            desc.maxRenderSize = desc.maxUpscaleSize = {1920, 1080};
            ffxContext context = nullptr;
            const auto created = create(&context, &desc.header, nullptr);
            const bool success = created == FFX_API_RETURN_OK && context;
            if (context && destroy(&context, nullptr) != FFX_API_RETURN_OK)
                throw std::runtime_error("context destruction failed");
            if (success != expectedSuccess ||
                (!expectedSuccess && created != FFX_API_RETURN_ERROR_RUNTIME_ERROR))
                throw std::runtime_error("unexpected RDR2 context result=" + std::to_string(created));
            std::cout << "rdr2_context_flags=9 external_exposure=true negotiated=" << expectedSuccess
                      << " result=" << created << '\n';
        };
        if (rdr2Context) {
            // Deliberately omit the provider contract to reproduce the observed
            // rejection, then retry on a newly created, negotiated device.
            check(vkCreateDevice(physical, &source, nullptr, &device), "unprepared vkCreateDevice");
            if (vkGetDeviceProcAddr(device, "vkGetDescriptorEXT"))
                throw std::runtime_error("unprepared device unexpectedly exposes descriptor buffers");
            checkContext(device, false);
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }
        request.header.type = FFX_API_QUERY_DESC_TYPE_VK_PREPARE_DEVICE;
        request.physicalDevice = physical;
        request.sourceCreateInfo = &source;
        request.apiVersion = application.apiVersion;
        request.getPhysicalDeviceFeatures2 = vkGetPhysicalDeviceFeatures2;
        request.enumerateDeviceExtensionProperties = vkEnumerateDeviceExtensionProperties;
        const auto prepared = query(nullptr, &request.header);
        std::cout << "prepare_result=" << prepared << " message="
                  << (request.errorMessage ? request.errorMessage : "none") << '\n';
        if (prepared != FFX_API_RETURN_OK || !request.outputCreateInfo || !request.token)
            throw std::runtime_error("provider device preparation failed");
        const VkPhysicalDeviceDynamicRenderingFeatures* copied = nullptr;
        for (auto* node = static_cast<const VkBaseInStructure*>(request.outputCreateInfo->pNext);
             node; node = node->pNext) {
            if (node->sType == rendering.sType)
                copied = reinterpret_cast<const VkPhysicalDeviceDynamicRenderingFeatures*>(node);
        }
        if (!copied || copied == &rendering || copied->dynamicRendering != VK_TRUE ||
            rendering.pNext != &timeline || timeline.timelineSemaphore != VK_FALSE)
            throw std::runtime_error("dynamic-rendering/source preservation failed");
        check(vkCreateDevice(physical, request.outputCreateInfo, nullptr, &device), "vkCreateDevice");
        if (!vkGetDeviceProcAddr(device, "vkGetDescriptorEXT"))
            throw std::runtime_error("negotiated device lacks vkGetDescriptorEXT");
        if (rdr2Context) checkContext(device, true);
        std::cout << "dynamic_rendering=preserved device_create=ok\n";
        release();
    } catch (const std::exception& error) {
        std::cerr << "error=" << error.what() << '\n';
        result = 1;
    }
    release();
    if (device) vkDestroyDevice(device, nullptr);
    if (instance) vkDestroyInstance(instance, nullptr);
    FreeLibrary(module);
    return result;
}
