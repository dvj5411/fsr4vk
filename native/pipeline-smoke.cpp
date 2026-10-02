#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Pass {
    const char* name;
    const char* hash;
};

constexpr std::array<Pass, 14> kPasses{{
    {"pre", "9e5877bf41984816"},
    {"model_1", "3d9ec844637ea028"},
    {"model_2", "c23238ca713f8f78"},
    {"model_3", "5dab395eb925383e"},
    {"model_4", "6df946114912c931"},
    {"model_5", "32ada0046ef27f1b"},
    {"model_6", "0531325fe892e91e"},
    {"model_7", "85eaf87348d33113"},
    {"model_8", "a2411c43d0d109fe"},
    {"model_9", "bdd6574e0d73e2f0"},
    {"model_10", "1e12c37876ceeb66"},
    {"model_11", "607b5889b24360d4"},
    {"model_12", "2436e9ea16dc2fb4"},
    {"post", "c008d8a012f99497"},
}};

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                                 std::to_string(result));
    }
}

std::vector<std::uint32_t> read_spirv(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("cannot open " + path.string());
    }
    const auto byte_size = stream.tellg();
    if (byte_size <= 0 || byte_size % 4 != 0) {
        throw std::runtime_error("invalid SPIR-V byte size for " + path.string());
    }
    std::vector<std::uint32_t> code(static_cast<std::size_t>(byte_size) / 4);
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(code.data()), byte_size);
    if (!stream) {
        throw std::runtime_error("short read from " + path.string());
    }
    return code;
}

bool has_extension(VkPhysicalDevice physical_device, const char* wanted) {
    std::uint32_t count = 0;
    check(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr),
          "vkEnumerateDeviceExtensionProperties(count)");
    std::vector<VkExtensionProperties> extensions(count);
    check(vkEnumerateDeviceExtensionProperties(
              physical_device, nullptr, &count, extensions.data()),
          "vkEnumerateDeviceExtensionProperties(list)");
    for (const auto& extension : extensions) {
        if (std::string(extension.extensionName) == wanted) {
            return true;
        }
    }
    return false;
}

std::uint32_t find_compute_queue(VkPhysicalDevice physical_device) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, properties.data());
    for (std::uint32_t index = 0; index < count; ++index) {
        if (properties[index].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            return index;
        }
    }
    throw std::runtime_error("physical device exposes no compute queue");
}

}  // namespace

int main(int argc, char** argv) {
    const fs::path shader_dir = argc > 1
                                    ? fs::path(argv[1])
                                    : fs::path("captures/w6400-fsr4-402c-dispatch/shaders");

    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "fsr4-native-pipeline-smoke";
    application.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    application.pEngineName = "none";
    application.engineVersion = 0;
    application.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &application;

    VkInstance instance = VK_NULL_HANDLE;
    check(vkCreateInstance(&instance_info, nullptr, &instance), "vkCreateInstance");

    try {
        std::uint32_t physical_device_count = 0;
        check(vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr),
              "vkEnumeratePhysicalDevices(count)");
        if (!physical_device_count) {
            throw std::runtime_error("Vulkan reports no physical devices");
        }
        std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
        check(vkEnumeratePhysicalDevices(
                  instance, &physical_device_count, physical_devices.data()),
              "vkEnumeratePhysicalDevices(list)");
        const VkPhysicalDevice physical_device = physical_devices.front();

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical_device, &properties);
        std::cout << "device=" << properties.deviceName << " api="
                  << VK_VERSION_MAJOR(properties.apiVersion) << '.'
                  << VK_VERSION_MINOR(properties.apiVersion) << '.'
                  << VK_VERSION_PATCH(properties.apiVersion) << '\n';

        if (!has_extension(physical_device, VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME)) {
            throw std::runtime_error("VK_EXT_mutable_descriptor_type is required by captured ABI");
        }

        VkPhysicalDeviceVulkan13Features features13{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceVulkan12Features features12{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features2.pNext = &features12;
        features12.pNext = &features13;
        vkGetPhysicalDeviceFeatures2(physical_device, &features2);

        const bool required_features =
            features2.features.shaderInt16 &&
            features2.features.shaderStorageImageWriteWithoutFormat &&
            features12.shaderFloat16 && features12.shaderInt8 &&
            features12.storageBuffer8BitAccess && features12.runtimeDescriptorArray &&
            features12.shaderStorageBufferArrayNonUniformIndexing &&
            features12.shaderSampledImageArrayNonUniformIndexing &&
            features12.shaderStorageImageArrayNonUniformIndexing &&
            features12.descriptorBindingPartiallyBound && features12.bufferDeviceAddress &&
            features13.shaderIntegerDotProduct;
        if (!required_features) {
            throw std::runtime_error("device lacks one or more required FSR4 shader features");
        }

        const float queue_priority = 1.0f;
        const std::uint32_t queue_family = find_compute_queue(physical_device);
        VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_info.queueFamilyIndex = queue_family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &queue_priority;

        std::array<const char*, 1> extensions{{
            VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME,
        }};

        VkPhysicalDeviceVulkan13Features enable13{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        enable13.shaderIntegerDotProduct = VK_TRUE;
        VkPhysicalDeviceVulkan12Features enable12{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        enable12.pNext = &enable13;
        enable12.shaderFloat16 = VK_TRUE;
        enable12.shaderInt8 = VK_TRUE;
        enable12.storageBuffer8BitAccess = VK_TRUE;
        enable12.runtimeDescriptorArray = VK_TRUE;
        enable12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
        enable12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        enable12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
        enable12.descriptorBindingPartiallyBound = VK_TRUE;
        enable12.bufferDeviceAddress = VK_TRUE;

        VkPhysicalDeviceFeatures2 enable2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        enable2.pNext = &enable12;
        enable2.features.shaderInt16 = VK_TRUE;
        enable2.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;

        VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device_info.pNext = &enable2;
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;
        device_info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        device_info.ppEnabledExtensionNames = extensions.data();

        VkDevice device = VK_NULL_HANDLE;
        check(vkCreateDevice(physical_device, &device_info, nullptr, &device),
              "vkCreateDevice");

        try {
            constexpr std::uint32_t kDescriptorCount = 128;
            VkDescriptorSetLayoutBinding set0_binding{};
            set0_binding.binding = 0;
            set0_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            set0_binding.descriptorCount = kDescriptorCount;
            set0_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            const std::array<VkDescriptorType, 2> mutable_types{{
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            }};
            VkMutableDescriptorTypeListEXT mutable_list{};
            mutable_list.descriptorTypeCount =
                static_cast<std::uint32_t>(mutable_types.size());
            mutable_list.pDescriptorTypes = mutable_types.data();
            VkMutableDescriptorTypeCreateInfoEXT mutable_info{
                VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT};
            mutable_info.mutableDescriptorTypeListCount = 1;
            mutable_info.pMutableDescriptorTypeLists = &mutable_list;

            VkDescriptorSetLayoutBinding set1_binding{};
            set1_binding.binding = 1;
            set1_binding.descriptorType = VK_DESCRIPTOR_TYPE_MUTABLE_EXT;
            set1_binding.descriptorCount = kDescriptorCount;
            set1_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            VkDescriptorSetLayoutBinding set2_binding{};
            set2_binding.binding = 0;
            set2_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            set2_binding.descriptorCount = kDescriptorCount;
            set2_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            VkDescriptorSetLayoutBinding set3_binding{};
            set3_binding.binding = 0;
            set3_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            set3_binding.descriptorCount = 1;
            set3_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            std::array<VkDescriptorSetLayoutBinding, 4> bindings{{
                set0_binding, set1_binding, set2_binding, set3_binding}};
            std::array<VkDescriptorSetLayout, 4> set_layouts{};
            for (std::size_t index = 0; index < bindings.size(); ++index) {
                VkDescriptorSetLayoutCreateInfo layout_info{
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
                layout_info.bindingCount = 1;
                layout_info.pBindings = &bindings[index];
                if (index == 1) {
                    layout_info.pNext = &mutable_info;
                }
                check(vkCreateDescriptorSetLayout(
                          device, &layout_info, nullptr, &set_layouts[index]),
                      "vkCreateDescriptorSetLayout");
            }

            VkPushConstantRange push_constants{};
            push_constants.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            push_constants.offset = 0;
            push_constants.size = 24;

            VkPipelineLayoutCreateInfo pipeline_layout_info{
                VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            pipeline_layout_info.setLayoutCount =
                static_cast<std::uint32_t>(set_layouts.size());
            pipeline_layout_info.pSetLayouts = set_layouts.data();
            pipeline_layout_info.pushConstantRangeCount = 1;
            pipeline_layout_info.pPushConstantRanges = &push_constants;

            VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
            check(vkCreatePipelineLayout(
                      device, &pipeline_layout_info, nullptr, &pipeline_layout),
                  "vkCreatePipelineLayout");

            std::vector<VkShaderModule> modules;
            std::vector<VkPipeline> pipelines;
            modules.reserve(kPasses.size());
            pipelines.reserve(kPasses.size());

            try {
                for (const Pass& pass : kPasses) {
                    const fs::path path = shader_dir / (std::string(pass.hash) + ".spv");
                    const auto code = read_spirv(path);
                    VkShaderModuleCreateInfo module_info{
                        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
                    module_info.codeSize = code.size() * sizeof(std::uint32_t);
                    module_info.pCode = code.data();
                    VkShaderModule module = VK_NULL_HANDLE;
                    check(vkCreateShaderModule(device, &module_info, nullptr, &module),
                          pass.name);
                    modules.push_back(module);

                    VkPipelineShaderStageCreateInfo stage{
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                    stage.module = module;
                    stage.pName = "main";

                    VkComputePipelineCreateInfo pipeline_info{
                        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
                    pipeline_info.stage = stage;
                    pipeline_info.layout = pipeline_layout;

                    VkPipeline pipeline = VK_NULL_HANDLE;
                    check(vkCreateComputePipelines(
                              device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline),
                          pass.name);
                    pipelines.push_back(pipeline);
                    std::cout << "pipeline=" << pass.name << " hash=" << pass.hash
                              << " status=ok\n";
                }
            } catch (...) {
                for (VkPipeline pipeline : pipelines) {
                    vkDestroyPipeline(device, pipeline, nullptr);
                }
                for (VkShaderModule module : modules) {
                    vkDestroyShaderModule(device, module, nullptr);
                }
                vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
                for (VkDescriptorSetLayout layout : set_layouts) {
                    vkDestroyDescriptorSetLayout(device, layout, nullptr);
                }
                throw;
            }

            for (VkPipeline pipeline : pipelines) {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
            for (VkShaderModule module : modules) {
                vkDestroyShaderModule(device, module, nullptr);
            }
            vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
            for (VkDescriptorSetLayout layout : set_layouts) {
                vkDestroyDescriptorSetLayout(device, layout, nullptr);
            }
            std::cout << "summary=pipeline-smoke-pass pipelines=" << kPasses.size()
                      << '\n';
        } catch (...) {
            vkDestroyDevice(device, nullptr);
            throw;
        }

        vkDestroyDevice(device, nullptr);
    } catch (const std::exception& exception) {
        vkDestroyInstance(instance, nullptr);
        std::cerr << "error=" << exception.what() << '\n';
        return 1;
    }

    vkDestroyInstance(instance, nullptr);
    return 0;
}
