#include <vulkan/vulkan.h>
#include "quality-recorder.hpp"
#include "quality-context.hpp"
#include "experimental-resolution.hpp"
#include "../provider/vulkan_device_features.hpp"
#include <memory>
#if defined(FSR4_EXTERNAL_PROVIDER)
#include <windows.h>
#include "../amd-fidelityfx-sdk/Kits/FidelityFX/upscalers/include/ffx_upscale.h"
struct CreateBackendVkDesc {
    ffxCreateContextDescHeader header;
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr;
};
#else
#include "../provider/ffx_vk_provider.cpp"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
using fsr4::kPasses;

PFN_vkCmdPipelineBarrier2 command_pipeline_barrier2 = nullptr;
PFN_vkGetBufferDeviceAddress get_buffer_device_address = nullptr;

void image_barrier(VkCommandBuffer command_buffer, VkImage image,
                   VkImageLayout old_layout, VkImageLayout new_layout,
                   VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access,
                   VkPipelineStageFlags2 destination_stage, VkAccessFlags2 destination_access,
                   VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
    fsr4::image_barrier(command_pipeline_barrier2, command_buffer, image, old_layout,
                        new_layout, source_stage, source_access, destination_stage,
                        destination_access, aspect);
}

std::uint32_t kRenderWidth = fsr4experiment::render_width;
std::uint32_t kRenderHeight = fsr4experiment::render_height;
std::uint32_t kOutputWidth = 1920;
std::uint32_t kOutputHeight = 1080;
constexpr VkDeviceSize kScratchSize = 20880256;
constexpr VkDeviceSize kPhysicalConstantBackingSize = 2048;
constexpr std::uint32_t kDescriptorCount = 128;

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
};

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct OptimizedConstants {
    float inv_size[2];
    float scale[2];
    float inv_scale[2];
    float jitter[2];
    float mv_scale[2];
    float tex_size[2];
    float max_render_size[2];
    float motion_vector_jitter_cancellation[2];
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t reset;
    std::uint32_t width_lr;
    std::uint32_t height_lr;
    float pre_exposure;
    float previous_pre_exposure;
    std::uint32_t rcas_enabled;
    float rcas_sharpness;
    float padding;
};

static_assert(sizeof(OptimizedConstants) == 104);

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                                 std::to_string(result));
    }
}

std::vector<std::uint8_t> read_bytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("cannot open " + path.string());
    const auto size = stream.tellg();
    if (size <= 0) throw std::runtime_error("empty file " + path.string());
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!stream) throw std::runtime_error("short read from " + path.string());
    return bytes;
}

std::vector<std::uint32_t> read_spirv(const fs::path& path) {
    const auto bytes = read_bytes(path);
    if (bytes.size() % 4) throw std::runtime_error("invalid SPIR-V size");
    std::vector<std::uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    return words;
}

std::uint32_t memory_type(VkPhysicalDevice physical_device,
                          std::uint32_t bits,
                          VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);
    for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
        if ((bits & (1u << index)) &&
            (properties.memoryTypes[index].propertyFlags & flags) == flags) {
            return index;
        }
    }
    throw std::runtime_error("no compatible Vulkan memory type");
}

Buffer create_buffer(VkPhysicalDevice physical_device,
                     VkDevice device,
                     VkDeviceSize size,
                     VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags properties,
                     bool device_address = false) {
    Buffer result{};
    result.size = size;
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device, &buffer_info, nullptr, &result.buffer), "vkCreateBuffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, result.buffer, &requirements);
    VkMemoryAllocateFlagsInfo flags_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    flags_info.flags = device_address ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.pNext = device_address ? &flags_info : nullptr;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type(physical_device, requirements.memoryTypeBits,
                                             properties);
    check(vkAllocateMemory(device, &allocation, nullptr, &result.memory),
          "vkAllocateMemory(buffer)");
    check(vkBindBufferMemory(device, result.buffer, result.memory, 0), "vkBindBufferMemory");
    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        check(vkMapMemory(device, result.memory, 0, size, 0, &result.mapped), "vkMapMemory");
    }
    return result;
}

Image create_image(VkPhysicalDevice physical_device,
                   VkDevice device,
                   std::uint32_t width,
                   std::uint32_t height,
                   VkFormat format,
                   VkImageUsageFlags usage) {
    Image result{};
    result.format = format;
    result.width = width;
    result.height = height;
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = usage;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(device, &image_info, nullptr, &result.image), "vkCreateImage");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, result.image, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type(physical_device, requirements.memoryTypeBits,
                                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device, &allocation, nullptr, &result.memory),
          "vkAllocateMemory(image)");
    check(vkBindImageMemory(device, result.image, result.memory, 0), "vkBindImageMemory");

    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = result.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = format==VK_FORMAT_D32_SFLOAT_S8_UINT ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    check(vkCreateImageView(device, &view_info, nullptr, &result.view), "vkCreateImageView");
    return result;
}

void destroy_buffer(VkDevice device, Buffer& buffer) {
    if (buffer.mapped) vkUnmapMemory(device, buffer.memory);
    if (buffer.buffer) vkDestroyBuffer(device, buffer.buffer, nullptr);
    if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
    buffer = {};
}

void destroy_image(VkDevice device, Image& image) {
    if (image.view) vkDestroyImageView(device, image.view, nullptr);
    if (image.image) vkDestroyImage(device, image.image, nullptr);
    if (image.memory) vkFreeMemory(device, image.memory, nullptr);
    image = {};
}

std::uint16_t float_to_half(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    int exponent = static_cast<int>((bits >> 23) & 0xffu) - 127 + 15;
    std::uint32_t mantissa = bits & 0x7fffffu;
    if (exponent <= 0) {
        if (exponent < -10) return static_cast<std::uint16_t>(sign);
        mantissa = (mantissa | 0x800000u) >> (1 - exponent);
        return static_cast<std::uint16_t>(sign | ((mantissa + 0x1000u) >> 13));
    }
    if (exponent >= 31) return static_cast<std::uint16_t>(sign | 0x7c00u);
    mantissa += 0x1000u;
    if (mantissa & 0x800000u) {
        mantissa = 0;
        if (++exponent >= 31) return static_cast<std::uint16_t>(sign | 0x7c00u);
    }
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exponent) << 10) |
                                      (mantissa >> 13));
}

float half_to_float(std::uint16_t value) {
    const std::uint32_t sign = static_cast<std::uint32_t>(value & 0x8000u) << 16;
    std::uint32_t exponent = (value >> 10) & 0x1fu;
    std::uint32_t mantissa = value & 0x3ffu;
    std::uint32_t bits = 0;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            int unbiased = -14;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                --unbiased;
            }
            bits = sign | (static_cast<std::uint32_t>(unbiased + 127) << 23) |
                   ((mantissa & 0x3ffu) << 13);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7f800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent - 15u + 127u) << 23) | (mantissa << 13);
    }
    float result = 0;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

struct FrameInputs {
    std::vector<std::uint16_t> color;
    std::vector<float> depth;
    std::vector<std::uint16_t> motion;
};

// Preserve the reference probe's floating-point operation order: +/-1/6
// is not bit-identical to Halton(index, 3) - 0.5f.
float reference_jitter(unsigned frame_index, unsigned base) {
    float result = 0.0f;
    float fraction = 1.0f;
    for (unsigned index = frame_index + 1; index; index /= base) {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
    }
    return result - 0.5f;
}

void generate_reference_frame(FrameInputs& inputs, unsigned frame_index = 0) {
    if (frame_index > 1) throw std::invalid_argument("reference harness currently supports frames zero and one");
    const std::size_t pixels = static_cast<std::size_t>(kRenderWidth) * kRenderHeight;
    inputs.color.resize(pixels * 4);
    inputs.depth.resize(pixels);
    inputs.motion.assign(pixels * 2, float_to_half(0.0f));
    const float jitter_x = reference_jitter(frame_index, 2);
    const float jitter_y = reference_jitter(frame_index, 3);
    const float object_x = 180.0f + frame_index*11.0f;
    const float object_y = 155.0f + frame_index*5.0f;
    const float glass_x = 850.0f - frame_index*7.0f;
    const float glass_y = 385.0f;
    for (std::uint32_t y = 0; y < kRenderHeight; ++y) {
        for (std::uint32_t x = 0; x < kRenderWidth; ++x) {
            const std::size_t pixel = static_cast<std::size_t>(y) * kRenderWidth + x;
            const float sample_x = static_cast<float>(x) + 0.5f + jitter_x;
            const float sample_y = static_cast<float>(y) + 0.5f + jitter_y;
            const float u = sample_x / static_cast<float>(kRenderWidth);
            const float v = sample_y / static_cast<float>(kRenderHeight);
            const bool checker = ((static_cast<int>(sample_x) / 12) ^
                                  (static_cast<int>(sample_y) / 12)) & 1;
            const bool moving = sample_x >= object_x && sample_x < object_x + 235.0f &&
                                sample_y >= object_y && sample_y < object_y + 178.0f;
            const float dx = sample_x - glass_x;
            const float dy = sample_y - glass_y;
            const bool glass = dx * dx + dy * dy < 82.0f * 82.0f;
            const bool diagonal = std::fabs(sample_y - (0.42f * sample_x + 65.0f)) < 0.75f;
            float red = 0.04f + 0.38f * u;
            float green = 0.07f + 0.34f * v;
            float blue = checker ? 0.22f : 0.10f;
            float depth = 0.82f + 0.12f * v;
            if (diagonal) {
                red = green = blue = 0.95f;
                depth = 0.48f;
            }
            if (moving) {
                const bool stripe = (static_cast<int>(sample_x - object_x) / 5) & 1;
                red = stripe ? 0.92f : 0.16f;
                green = stripe ? 0.18f : 0.86f;
                blue = 0.08f + 0.18f * v;
                depth = 0.24f;
            }
            if (glass) {
                constexpr float alpha = 0.45f;
                red = red * (1.0f - alpha) + 0.10f * alpha;
                green = green * (1.0f - alpha) + 0.70f * alpha;
                blue = blue * (1.0f - alpha) + 1.00f * alpha;
                depth = 0.40f;
            }
            inputs.color[pixel * 4 + 0] = float_to_half(red);
            inputs.color[pixel * 4 + 1] = float_to_half(green);
            inputs.color[pixel * 4 + 2] = float_to_half(blue);
            inputs.color[pixel * 4 + 3] = float_to_half(1.0f);
            inputs.depth[pixel] = depth;
            if (frame_index && moving) {
                inputs.motion[pixel*2]=float_to_half(-11.0f/kRenderWidth);
                inputs.motion[pixel*2+1]=float_to_half(-5.0f/kRenderHeight);
            }
            if (frame_index && glass) inputs.motion[pixel*2]=float_to_half(7.0f/kRenderWidth);
        }
    }
}

VkDeviceAddress device_address(VkDevice device, VkBuffer buffer) {
    VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    info.buffer = buffer;
    return get_buffer_device_address(device, &info);
}

std::uint32_t compute_queue_family(VkPhysicalDevice physical_device) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, properties.data());
    for (std::uint32_t index = 0; index < count; ++index) {
        if (properties[index].queueFlags & VK_QUEUE_COMPUTE_BIT) return index;
    }
    throw std::runtime_error("no Vulkan compute queue");
}

std::uint64_t fnv1a64(const std::uint8_t* bytes, std::size_t count) {
    std::uint64_t hash = 1469598103934665603ull;
    for (std::size_t index = 0; index < count; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5 && argc != 6) {
        std::cerr << "usage: " << argv[0]
                  << " SHADER_DIR INITIALIZERS.BIN QUALITY_WEIGHTS.BIN OUTPUT.RGBA16F [--context|--provider|--provider-nms|--provider-nms-formats|--provider-doom-formats|--provider-temporal]\n";
        return 2;
    }
    const fs::path shader_dir = argv[1];
    const fs::path initializer_path = argv[2];
    const fs::path weights_path = argv[3];
    const fs::path output_path = argv[4];

    const bool nms_formats = argc == 6 && std::string(argv[5]) == "--provider-nms-formats";
    const bool doom_formats = argc == 6 && std::string(argv[5]) == "--provider-doom-formats";
    const bool game_formats = nms_formats || doom_formats;
    const bool temporal_test = argc == 6 && std::string(argv[5]) == "--provider-temporal";
    const bool nms_inputs = temporal_test || game_formats || (argc == 6 && std::string(argv[5]) == "--provider-nms");
    const bool use_provider = nms_inputs || (argc == 6 && std::string(argv[5]) == "--provider");
    const bool use_context = use_provider || (argc == 6 && std::string(argv[5]) == "--context");
    if (argc == 6 && !use_context) return 2;
    const auto read_size=[](const char* name,uint32_t& w,uint32_t& h) {
        if(const char* value=std::getenv(name)) {
            unsigned x=0,y=0;char extra=0;
            if(std::sscanf(value,"%ux%u%c",&x,&y,&extra)!=2)return false;
            w=x;h=y;
        }
        return true;
    };
    if(!read_size("FSR4_TEST_RENDER_SIZE",kRenderWidth,kRenderHeight) ||
       !read_size("FSR4_TEST_OUTPUT_SIZE",kOutputWidth,kOutputHeight))return 2;
    try { fsr4::validate_dimensions(kRenderWidth,kRenderHeight,kOutputWidth,kOutputHeight); }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 2; }
    if((std::getenv("FSR4_TEST_RENDER_SIZE") || std::getenv("FSR4_TEST_OUTPUT_SIZE")) && !temporal_test)return 2;
#if defined(FSR4_EXTERNAL_PROVIDER)
    auto module = LoadLibraryA("amd_fidelityfx_upscaler_vk.dll");
    if (!module) { std::cerr << "provider DLL load failed: " << GetLastError() << '\n'; return 1; }
    auto ffxCreateContext = reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(module,"ffxCreateContext"));
    auto ffxDispatch = reinterpret_cast<PfnFfxDispatch>(GetProcAddress(module,"ffxDispatch"));
    auto ffxDestroyContext = reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(module,"ffxDestroyContext"));
    if (!ffxCreateContext || !ffxDispatch || !ffxDestroyContext) return 1;
#endif
    ffxContext api_context = nullptr;
    std::unique_ptr<fsr4core::QualityContext> context;
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    std::array<VkDescriptorSetLayout, 4> set_layouts{};
    std::vector<VkShaderModule> modules;
    std::vector<VkPipeline> pipelines;
    std::vector<Buffer> buffers;
    std::vector<Image> images;

    try {
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "fsr4-native-dispatch-smoke";
        application.applicationVersion = VK_MAKE_VERSION(0, 2, 0);
        const bool vulkan11 = std::getenv("FSR4_TEST_VULKAN_1_1") != nullptr;
        application.apiVersion = vulkan11 ? VK_API_VERSION_1_1 : VK_API_VERSION_1_3;
        VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instance_info.pApplicationInfo = &application;
        check(vkCreateInstance(&instance_info, nullptr, &instance), "vkCreateInstance");

        std::uint32_t physical_count = 0;
        check(vkEnumeratePhysicalDevices(instance, &physical_count, nullptr),
              "vkEnumeratePhysicalDevices(count)");
        if (!physical_count) throw std::runtime_error("no Vulkan physical device");
        std::vector<VkPhysicalDevice> physical_devices(physical_count);
        check(vkEnumeratePhysicalDevices(instance, &physical_count, physical_devices.data()),
              "vkEnumeratePhysicalDevices(list)");
        const VkPhysicalDevice physical_device = physical_devices.front();
        VkPhysicalDeviceProperties physical_properties{};
        vkGetPhysicalDeviceProperties(physical_device, &physical_properties);
        std::cout << "device=" << physical_properties.deviceName
                  << " requested_api=" << VK_API_VERSION_MAJOR(application.apiVersion) << '.'
                  << VK_API_VERSION_MINOR(application.apiVersion) << '\n';

        const std::uint32_t queue_family = compute_queue_family(physical_device);
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_info.queueFamilyIndex = queue_family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;
        VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;
        VkPhysicalDeviceBufferDeviceAddressFeaturesEXT legacy_buffer_address{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT};
        const char* legacy_buffer_address_extension =
            VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME;
        if (std::getenv("FSR4_TEST_LEGACY_BDA_EXT")) {
            legacy_buffer_address.bufferDeviceAddress = VK_TRUE;
            device_info.pNext = &legacy_buffer_address;
            device_info.enabledExtensionCount = 1;
            device_info.ppEnabledExtensionNames = &legacy_buffer_address_extension;
        }
        fsr4vk::DeviceFeatures enabled_features(
            physical_device, device_info, application.apiVersion,
            vkGetPhysicalDeviceFeatures2, vkEnumerateDeviceExtensionProperties);
        check(vkCreateDevice(physical_device, enabled_features.get(), nullptr, &device),
              "vkCreateDevice");

        const fsr4vk::DeviceDispatch promoted_dispatch(
            device, vkGetDeviceProcAddr, application.apiVersion);
        command_pipeline_barrier2 = promoted_dispatch.cmd_pipeline_barrier2;
        get_buffer_device_address = promoted_dispatch.get_buffer_device_address;

        VkQueue queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(device, queue_family, 0, &queue);

        VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.maxAnisotropy = 16.0f;
        sampler_info.compareOp = VK_COMPARE_OP_NEVER;
        sampler_info.maxLod = std::numeric_limits<float>::max();
        sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        check(vkCreateSampler(device, &sampler_info, nullptr, &sampler),
              "vkCreateSampler");

        const VkDescriptorBindingFlags variable_descriptor_count =
            VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
        const std::array<VkDescriptorType, 6> mutable_types{{
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
        }};
        const VkMutableDescriptorTypeListEXT mutable_list{
            static_cast<std::uint32_t>(mutable_types.size()), mutable_types.data()};

        const auto create_set_layout = [&](std::uint32_t index,
                                           const VkDescriptorSetLayoutBinding* bindings,
                                           std::uint32_t binding_count,
                                           const void* next,
                                           VkDescriptorSetLayoutCreateFlags flags) {
            VkDescriptorSetLayoutCreateInfo layout_info{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            layout_info.pNext = next;
            layout_info.flags = flags;
            layout_info.bindingCount = binding_count;
            layout_info.pBindings = bindings;
            check(vkCreateDescriptorSetLayout(device, &layout_info, nullptr,
                                               &set_layouts[index]),
                  "vkCreateDescriptorSetLayout");
        };

        const VkDescriptorSetLayoutBinding sampler_heap_binding{
            0, VK_DESCRIPTOR_TYPE_SAMPLER, kDescriptorCount,
            VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutBindingFlagsCreateInfo sampler_heap_flags{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
        sampler_heap_flags.bindingCount = 1;
        sampler_heap_flags.pBindingFlags = &variable_descriptor_count;
        create_set_layout(0, &sampler_heap_binding, 1, &sampler_heap_flags,
                          VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);

        const std::array<VkDescriptorSetLayoutBinding, 2> image_heap_bindings{{
            {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4,
             VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_MUTABLE_EXT, kDescriptorCount,
             VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        }};
        const std::array<VkDescriptorBindingFlags, 2> image_heap_flag_values{{
            0, variable_descriptor_count}};
        const std::array<VkMutableDescriptorTypeListEXT, 2> image_heap_lists{{
            {0, nullptr}, mutable_list}};
        VkMutableDescriptorTypeCreateInfoEXT image_heap_mutable_info{
            VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT};
        image_heap_mutable_info.mutableDescriptorTypeListCount = image_heap_lists.size();
        image_heap_mutable_info.pMutableDescriptorTypeLists = image_heap_lists.data();
        VkDescriptorSetLayoutBindingFlagsCreateInfo image_heap_flags{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
        image_heap_flags.pNext = &image_heap_mutable_info;
        image_heap_flags.bindingCount = image_heap_flag_values.size();
        image_heap_flags.pBindingFlags = image_heap_flag_values.data();
        create_set_layout(1, image_heap_bindings.data(), image_heap_bindings.size(),
                          &image_heap_flags,
                          VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);

        const VkDescriptorSetLayoutBinding buffer_heap_binding{
            0, VK_DESCRIPTOR_TYPE_MUTABLE_EXT, kDescriptorCount,
            VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkMutableDescriptorTypeCreateInfoEXT buffer_heap_mutable_info{
            VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT};
        buffer_heap_mutable_info.mutableDescriptorTypeListCount = 1;
        buffer_heap_mutable_info.pMutableDescriptorTypeLists = &mutable_list;
        VkDescriptorSetLayoutBindingFlagsCreateInfo buffer_heap_flags{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
        buffer_heap_flags.pNext = &buffer_heap_mutable_info;
        buffer_heap_flags.bindingCount = 1;
        buffer_heap_flags.pBindingFlags = &variable_descriptor_count;
        create_set_layout(2, &buffer_heap_binding, 1, &buffer_heap_flags,
                          VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);

        const VkDescriptorSetLayoutBinding embedded_sampler_binding{
            0, VK_DESCRIPTOR_TYPE_SAMPLER, 1,
            VK_SHADER_STAGE_COMPUTE_BIT, &sampler};
        create_set_layout(
            3, &embedded_sampler_binding, 1, nullptr,
            VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT |
                VK_DESCRIPTOR_SET_LAYOUT_CREATE_EMBEDDED_IMMUTABLE_SAMPLERS_BIT_EXT);

        VkPushConstantRange push_range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 24};
        VkPipelineLayoutCreateInfo pipeline_layout_info{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipeline_layout_info.setLayoutCount = set_layouts.size();
        pipeline_layout_info.pSetLayouts = set_layouts.data();
        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pPushConstantRanges = &push_range;
        check(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr,
                                     &pipeline_layout),
              "vkCreatePipelineLayout");

        modules.reserve(kPasses.size());
        pipelines.reserve(kPasses.size());
        for (const auto& pass : kPasses) {
            // NMS bundles contain their captured pre permutation, not the
            // unrelated external-exposure pre shader used by baseline mode.
            const char* hash = nms_inputs && std::string(pass.name) == "pre"
                ? "c853540cef1e8d64" : pass.hash;
            const auto index=unsigned(&pass-kPasses.data())+1;
            auto filename=fs::exists(shader_dir/"pass-00.spv")
                ? "pass-"+std::string(index<10 ? "0" : "")+std::to_string(index)+".spv"
                : std::string(hash)+".spv";
            if (index == 1 && !fsr4vk::supportsNativeMixedDot(
                    physical_device, vkGetPhysicalDeviceFeatures2, vkEnumerateDeviceExtensionProperties))
                filename.insert(filename.size() - 4, ".portable");
            const auto code = read_spirv(shader_dir / filename);
            VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            module_info.codeSize = code.size() * sizeof(std::uint32_t);
            module_info.pCode = code.data();
            VkShaderModule module = VK_NULL_HANDLE;
            check(vkCreateShaderModule(device, &module_info, nullptr, &module), pass.name);
            modules.push_back(module);
            VkPipelineShaderStageCreateInfo stage{
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            stage.module = module;
            stage.pName = "main";
            VkComputePipelineCreateInfo pipeline_info{
                VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            pipeline_info.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
            pipeline_info.stage = stage;
            pipeline_info.layout = pipeline_layout;
            VkPipeline pipeline = VK_NULL_HANDLE;
            check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
                                           nullptr, &pipeline),
                  pass.name);
            pipelines.push_back(pipeline);
        }

        const auto get_layout_size = reinterpret_cast<PFN_vkGetDescriptorSetLayoutSizeEXT>(
            vkGetDeviceProcAddr(device, "vkGetDescriptorSetLayoutSizeEXT"));
        const auto get_binding_offset =
            reinterpret_cast<PFN_vkGetDescriptorSetLayoutBindingOffsetEXT>(
                vkGetDeviceProcAddr(device,
                                    "vkGetDescriptorSetLayoutBindingOffsetEXT"));
        const auto get_descriptor = reinterpret_cast<PFN_vkGetDescriptorEXT>(
            vkGetDeviceProcAddr(device, "vkGetDescriptorEXT"));
        const auto bind_descriptor_buffers =
            reinterpret_cast<PFN_vkCmdBindDescriptorBuffersEXT>(
                vkGetDeviceProcAddr(device, "vkCmdBindDescriptorBuffersEXT"));
        const auto set_descriptor_offsets =
            reinterpret_cast<PFN_vkCmdSetDescriptorBufferOffsetsEXT>(
                vkGetDeviceProcAddr(device,
                                    "vkCmdSetDescriptorBufferOffsetsEXT"));
        const auto bind_embedded_samplers =
            reinterpret_cast<PFN_vkCmdBindDescriptorBufferEmbeddedSamplersEXT>(
                vkGetDeviceProcAddr(
                    device, "vkCmdBindDescriptorBufferEmbeddedSamplersEXT"));
        if (!get_layout_size || !get_binding_offset || !get_descriptor ||
            !bind_descriptor_buffers || !set_descriptor_offsets ||
            !bind_embedded_samplers) {
            throw std::runtime_error("descriptor buffer entry point is unavailable");
        }

        VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptor_properties{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT};
        VkPhysicalDeviceProperties2 properties2{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties2.pNext = &descriptor_properties;
        vkGetPhysicalDeviceProperties2(physical_device, &properties2);
        std::array<VkDeviceSize, 3> descriptor_layout_sizes{};
        std::array<VkDeviceSize, 3> descriptor_binding_offsets{};
        constexpr std::array<std::uint32_t, 3> descriptor_bindings{{0, 1, 0}};
        for (std::size_t index = 0; index < descriptor_layout_sizes.size(); ++index) {
            get_layout_size(device, set_layouts[index], &descriptor_layout_sizes[index]);
            get_binding_offset(device, set_layouts[index], descriptor_bindings[index],
                               &descriptor_binding_offsets[index]);
        }
        // Set 2 consists solely of the mutable descriptor array, so its queried
        // byte size exposes the implementation-selected stride for that binding.
        // This may include padding beyond the largest concrete descriptor size.
        const VkDeviceSize mutable_descriptor_stride =
            descriptor_layout_sizes[2] / kDescriptorCount;
        const VkDeviceSize image_heap_stride =
            (descriptor_layout_sizes[1] - descriptor_binding_offsets[1]) /
            kDescriptorCount;
        if (mutable_descriptor_stride == 0 ||
            mutable_descriptor_stride != image_heap_stride) {
            throw std::runtime_error("inconsistent mutable descriptor strides");
        }
        std::cout << "descriptor_layout_sizes=" << descriptor_layout_sizes[0] << ','
                  << descriptor_layout_sizes[1] << ',' << descriptor_layout_sizes[2]
                  << ",embedded"
                  << " binding_offsets=" << descriptor_binding_offsets[0] << ','
                  << descriptor_binding_offsets[1] << ','
                  << descriptor_binding_offsets[2] << ",embedded"
                  << " mutable_stride=" << mutable_descriptor_stride
                  << " concrete_sizes="
                  << descriptor_properties.uniformBufferDescriptorSize << ','
                  << descriptor_properties.storageBufferDescriptorSize << ','
                  << descriptor_properties.sampledImageDescriptorSize << ','
                  << descriptor_properties.uniformTexelBufferDescriptorSize << ','
                  << descriptor_properties.storageImageDescriptorSize << ','
                  << descriptor_properties.storageTexelBufferDescriptorSize << '\n';

        buffers.reserve(9);
        for (std::size_t index = 0; index < descriptor_layout_sizes.size(); ++index) {
            const bool sampler_buffer = index == 0;
            const VkBufferUsageFlags usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                (sampler_buffer
                     ? VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT
                     : VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT);
            buffers.push_back(create_buffer(
                physical_device, device, descriptor_layout_sizes[index], usage,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                true));
            std::memset(buffers.back().mapped, 0,
                        static_cast<std::size_t>(descriptor_layout_sizes[index]));
        }

        buffers.push_back(create_buffer(
            physical_device, device, kScratchSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true));
        Buffer& scratch = buffers.back();
        const auto initializer_bytes = read_bytes(initializer_path);
        buffers.push_back(create_buffer(
            physical_device, device, initializer_bytes.size(),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true));
        Buffer& initializer = buffers.back();
        std::memcpy(initializer.mapped, initializer_bytes.data(), initializer_bytes.size());
        const auto weights_bytes = read_bytes(weights_path);
        if (weights_bytes.size() != 1024) throw std::runtime_error("weights must be 1024 bytes");
        buffers.push_back(create_buffer(
            physical_device, device, kPhysicalConstantBackingSize,
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true));
        Buffer& constants = buffers.back();
        FrameInputs frame;
        generate_reference_frame(frame);
        if (nms_inputs) for (auto& depth : frame.depth) depth = 1.0f-depth;
        std::vector<uint32_t> packed_color;
        if (game_formats) {
            // Positive finite test input: unsigned 11/10-bit float shares the
            // half exponent and drops four/five mantissa bits, rounded to even.
            const auto pack=[](uint16_t h,unsigned drop) -> uint32_t {
                return (uint32_t(h)+((1u<<(drop-1))-1)+((h>>drop)&1u))>>drop;
            };
            for (size_t i=0;i<frame.color.size();i+=4) {
                if (doom_formats) {
                    // RGB9E5 is sampled-only here; zero is a valid finite shared-
                    // exponent value and is sufficient to exercise the exact
                    // Doom input image/view and provider resource contract.
                    packed_color.push_back(0);
                } else {
                    packed_color.push_back(pack(frame.color[i],4) | (pack(frame.color[i+1],4)<<11) |
                                           (pack(frame.color[i+2],5)<<22));
                }
            }
        }
        const void* color_data=game_formats ? static_cast<const void*>(packed_color.data()) : frame.color.data();
        const VkDeviceSize color_size = game_formats ? packed_color.size()*4 : frame.color.size()*2;
        const auto expected_color_hash=fnv1a64(reinterpret_cast<const uint8_t*>(color_data),color_size);
        const VkDeviceSize depth_size = frame.depth.size() * sizeof(float);
        const VkDeviceSize motion_size = frame.motion.size() * sizeof(std::uint16_t);
        const VkDeviceSize exposure_offset = color_size + depth_size + motion_size;
        const VkDeviceSize staging_size = exposure_offset + sizeof(float);
        buffers.push_back(create_buffer(
            physical_device, device, staging_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
        Buffer& staging = buffers.back();
        auto* staging_bytes = static_cast<std::uint8_t*>(staging.mapped);
        std::memcpy(staging_bytes, color_data, color_size);
        std::memcpy(staging_bytes + color_size, frame.depth.data(), depth_size);
        std::memcpy(staging_bytes + color_size + depth_size, frame.motion.data(), motion_size);
        const float exposure = 1.0f;
        std::memcpy(staging_bytes + exposure_offset, &exposure, sizeof(exposure));
        const VkDeviceSize output_size =
            static_cast<VkDeviceSize>(kOutputWidth) * kOutputHeight * 8;
        buffers.push_back(create_buffer(
            physical_device, device, output_size * 3 + color_size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
        Buffer& readback = buffers.back();

        images.reserve(8);
        images.push_back(create_image(physical_device, device, kRenderWidth, kRenderHeight,
                                      doom_formats ? VK_FORMAT_E5B9G9R9_UFLOAT_PACK32 :
                                          (nms_formats ? VK_FORMAT_B10G11R11_UFLOAT_PACK32 : VK_FORMAT_R16G16B16A16_SFLOAT),
                                      VK_IMAGE_USAGE_SAMPLED_BIT |
                                          VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
        Image& color = images.back();
        images.push_back(create_image(physical_device, device, kRenderWidth, kRenderHeight,
                                      game_formats ? VK_FORMAT_D32_SFLOAT_S8_UINT : VK_FORMAT_R32_SFLOAT,
                                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));
        Image& depth = images.back();
        images.push_back(create_image(physical_device, device, kRenderWidth, kRenderHeight,
                                      VK_FORMAT_R16G16_SFLOAT,
                                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));
        Image& motion = images.back();
        images.push_back(create_image(physical_device, device, 1, 1, VK_FORMAT_R32_SFLOAT,
                                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));
        Image& exposure_image = images.back();
        const VkImageUsageFlags internal_usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                                                 VK_IMAGE_USAGE_STORAGE_BIT |
                                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        images.push_back(create_image(physical_device, device, kOutputWidth, kOutputHeight,
                                      VK_FORMAT_R16G16B16A16_SFLOAT, internal_usage));
        Image& history = images.back();
        images.push_back(create_image(physical_device, device, kOutputWidth, kOutputHeight,
                                      VK_FORMAT_R16G16B16A16_SFLOAT, internal_usage));
        Image& reprojected = images.back();
        images.push_back(create_image(
            physical_device, device, kOutputWidth, kOutputHeight,
            VK_FORMAT_R8G8B8A8_UNORM, internal_usage));
        Image& recurrent = images.back();
        images.push_back(create_image(
            physical_device, device, kOutputWidth, kOutputHeight,
            nms_formats ? VK_FORMAT_B10G11R11_UFLOAT_PACK32 : VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
        Image& output = images.back();

        OptimizedConstants main_constants{};
        main_constants.inv_size[0] = 1.0f / kOutputWidth;
        main_constants.inv_size[1] = 1.0f / kOutputHeight;
        main_constants.scale[0] = float(kOutputWidth)/kRenderWidth;
        main_constants.scale[1] = float(kOutputHeight)/kRenderHeight;
        main_constants.inv_scale[0] = 1.0f/main_constants.scale[0];
        main_constants.inv_scale[1] = 1.0f/main_constants.scale[1];
        main_constants.jitter[0] = reference_jitter(0, 2);
        main_constants.jitter[1] = reference_jitter(0, 3);
        main_constants.mv_scale[0] = 1.0f;
        main_constants.mv_scale[1] = 1.0f;
        main_constants.tex_size[0] = static_cast<float>(kOutputWidth);
        main_constants.tex_size[1] = static_cast<float>(kOutputHeight);
        main_constants.max_render_size[0] = static_cast<float>(kRenderWidth);
        main_constants.max_render_size[1] = static_cast<float>(kRenderHeight);
        main_constants.width = kOutputWidth;
        main_constants.height = kOutputHeight;
        main_constants.reset = 1;
        main_constants.width_lr = kRenderWidth;
        main_constants.height_lr = kRenderHeight;
        main_constants.pre_exposure = 1.0f;
        main_constants.previous_pre_exposure = 0.0f;
        std::memcpy(constants.mapped, &main_constants, sizeof(main_constants));
        // VKD3D stages the pre-pass CBVs into one physical-address upload ring:
        // main constants at +0 and pass weights at +1024.
        std::memcpy(static_cast<std::uint8_t*>(constants.mapped) + 1024,
                    weights_bytes.data(), weights_bytes.size());

        const auto write_image_descriptor_buffer = [&](std::uint32_t index,
                                                       VkDescriptorType type,
                                                       VkImageView view) {
            VkDescriptorImageInfo image_info{};
            image_info.imageView = view;
            image_info.imageLayout = type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                : VK_IMAGE_LAYOUT_GENERAL;
            VkDescriptorGetInfoEXT descriptor_info{
                VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
            descriptor_info.type = type;
            std::size_t descriptor_size = 0;
            if (type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
                descriptor_info.data.pSampledImage = &image_info;
                descriptor_size = descriptor_properties.sampledImageDescriptorSize;
            } else {
                descriptor_info.data.pStorageImage = &image_info;
                descriptor_size = descriptor_properties.storageImageDescriptorSize;
            }
            auto* destination = static_cast<std::uint8_t*>(buffers[1].mapped) +
                descriptor_binding_offsets[1] + index * mutable_descriptor_stride;
            get_descriptor(device, &descriptor_info, descriptor_size, destination);
        };
        const auto write_buffer_descriptor_buffer = [&](std::uint32_t index,
                                                        const Buffer& buffer) {
            VkDescriptorAddressInfoEXT address_info{
                VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT};
            address_info.address = device_address(device, buffer.buffer);
            address_info.range = buffer.size;
            VkDescriptorGetInfoEXT descriptor_info{
                VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
            descriptor_info.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            descriptor_info.data.pStorageBuffer = &address_info;
            auto* destination = static_cast<std::uint8_t*>(buffers[2].mapped) +
                descriptor_binding_offsets[2] + index * mutable_descriptor_stride;
            get_descriptor(device, &descriptor_info,
                           descriptor_properties.storageBufferDescriptorSize,
                           destination);
        };

        // Pre-pass image table: SRV base 12 and UAV base 0.
        write_image_descriptor_buffer(12, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                      history.view);       // t0
        write_image_descriptor_buffer(13, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                      motion.view);        // t1
        write_image_descriptor_buffer(14, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                      depth.view);         // t2
        write_image_descriptor_buffer(15, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                      color.view);         // t3
        write_image_descriptor_buffer(16, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                      recurrent.view);     // t4
        write_image_descriptor_buffer(29, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                      exposure_image.view);  // t17
        write_image_descriptor_buffer(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                      reprojected.view);  // u3
        // Post-pass tables: UAV base 51 and SRV base 63.
        write_image_descriptor_buffer(66, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                      color.view);          // t3
        write_image_descriptor_buffer(72, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                      reprojected.view);    // t9
        write_image_descriptor_buffer(80, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                      exposure_image.view);  // t17
        write_image_descriptor_buffer(52, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                      history.view);       // u1
        write_image_descriptor_buffer(53, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                      output.view);        // u2
        write_image_descriptor_buffer(57, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                      recurrent.view);     // u6

        for (const auto index : {
                 11u, 20u, 22u, 24u, 26u, 28u, 30u,
                 34u, 38u, 42u, 46u, 48u, 50u, 62u}) {
            write_buffer_descriptor_buffer(index, scratch);
        }
        for (const auto index : {32u, 36u, 40u, 44u}) {
            write_buffer_descriptor_buffer(index, initializer);
        }
        write_buffer_descriptor_buffer(62, scratch);  // post u11

        VkCommandPoolCreateInfo command_pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        command_pool_info.queueFamilyIndex = queue_family;
        check(vkCreateCommandPool(device, &command_pool_info, nullptr, &command_pool),
              "vkCreateCommandPool");
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo command_allocation{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        command_allocation.commandPool = command_pool;
        command_allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_allocation.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device, &command_allocation, &command_buffer),
              "vkAllocateCommandBuffers");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        check(vkBeginCommandBuffer(command_buffer, &begin), "vkBeginCommandBuffer");

        const auto transition_to_copy = [&](const Image& image) {
            image_barrier(command_buffer, image.image, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                          VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                          image.format==VK_FORMAT_D32_SFLOAT_S8_UINT ? VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_COLOR_BIT);
        };
        for (const auto& image : images) transition_to_copy(image);
        VkClearColorValue zero{};
        VkClearColorValue output_sentinel{};
        output_sentinel.float32[0] = 1.0f;
        output_sentinel.float32[1] = 1.0f;
        output_sentinel.float32[2] = 1.0f;
        output_sentinel.float32[3] = 1.0f;
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        for (const Image* image : {&history, &recurrent}) {
            vkCmdClearColorImage(command_buffer, image->image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
        }
        vkCmdClearColorImage(command_buffer, reprojected.image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &output_sentinel, 1,
                             &range);
        vkCmdClearColorImage(command_buffer, output.image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &output_sentinel, 1,
                             &range);
        const auto copy_to_image = [&](const Image& image, VkDeviceSize offset) {
            VkBufferImageCopy copy{};
            copy.bufferOffset = offset;
            copy.imageSubresource.aspectMask = image.format==VK_FORMAT_D32_SFLOAT_S8_UINT ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = {image.width, image.height, 1};
            vkCmdCopyBufferToImage(command_buffer, staging.buffer, image.image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        };
        copy_to_image(color, 0);
        copy_to_image(depth, color_size);
        copy_to_image(motion, color_size + depth_size);
        copy_to_image(exposure_image, exposure_offset);
        vkCmdFillBuffer(command_buffer, scratch.buffer, 0, VK_WHOLE_SIZE, 0);

        for (const Image* image : {&color, &depth, &motion, &exposure_image,
                                   &history, &recurrent}) {
            image_barrier(command_buffer, image->image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT,
                          VK_ACCESS_2_TRANSFER_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                          image->format==VK_FORMAT_D32_SFLOAT_S8_UINT ? VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_COLOR_BIT);
        }
        for (const Image* image : {&reprojected, &output}) {
            image_barrier(command_buffer, image->image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_GENERAL,
                          VK_PIPELINE_STAGE_2_CLEAR_BIT,
                          VK_ACCESS_2_TRANSFER_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                              VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        }
        std::array<VkBufferMemoryBarrier2, 6> compute_input_barriers{};
        auto& scratch_barrier = compute_input_barriers[0];
        scratch_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        scratch_barrier.srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
        scratch_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        scratch_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        scratch_barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        scratch_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        scratch_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        scratch_barrier.buffer = scratch.buffer;
        scratch_barrier.size = VK_WHOLE_SIZE;
        for (std::size_t index = 0; index < 3; ++index) {
            auto& barrier = compute_input_barriers[index + 1];
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_DESCRIPTOR_BUFFER_READ_BIT_EXT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = buffers[index].buffer;
            barrier.size = VK_WHOLE_SIZE;
        }
        const auto host_input_barrier = [&](std::size_t index,
                                            const Buffer& input) {
            auto& barrier = compute_input_barriers[index];
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = input.buffer;
            barrier.size = VK_WHOLE_SIZE;
        };
        host_input_barrier(4, initializer);
        host_input_barrier(5, constants);
        VkDependencyInfo scratch_dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        scratch_dependency.bufferMemoryBarrierCount = compute_input_barriers.size();
        scratch_dependency.pBufferMemoryBarriers = compute_input_barriers.data();
        command_pipeline_barrier2(command_buffer, &scratch_dependency);

        const std::array<VkDeviceAddress, 3> descriptor_addresses{{
            device_address(device, buffers[0].buffer),
            device_address(device, buffers[1].buffer),
            device_address(device, buffers[2].buffer)}};
        const auto wrap = [](const Image& image) {
            return fsr4core::Image{image.image, VK_NULL_HANDLE, image.view,
                                  image.format, image.width, image.height};
        };
        fsr4core::OptimizedConstants context_constants{};
        std::memcpy(&context_constants, &main_constants, sizeof(main_constants));
        bool checked_depth_rejection = false;
        const auto record_context = [&] {
            if (!use_provider) {
                context->record(command_buffer, wrap(color), wrap(depth), wrap(motion),
                                wrap(exposure_image), wrap(output), context_constants);
                return;
            }
            const auto resource = [](const Image& image, uint32_t format, uint32_t state) {
                FfxApiResource r{};
                r.resource=reinterpret_cast<void*>(image.image);
                r.description.type=FFX_API_RESOURCE_TYPE_TEXTURE2D;
                r.description.format=format;
                r.description.width=image.width; r.description.height=image.height;
                r.description.depth=1; r.description.mipCount=1; r.state=state;
                return r;
            };
            ffxDispatchDescUpscale d{};
            d.header.type=FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
            d.commandList=command_buffer;
            d.color=resource(color,FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT,FFX_API_RESOURCE_STATE_COMPUTE_READ);
            d.depth=resource(depth,FFX_API_SURFACE_FORMAT_R32_FLOAT,FFX_API_RESOURCE_STATE_COMPUTE_READ);
            d.motionVectors=resource(motion,FFX_API_SURFACE_FORMAT_R16G16_FLOAT,FFX_API_RESOURCE_STATE_COMPUTE_READ);
            d.exposure=resource(exposure_image,FFX_API_SURFACE_FORMAT_R32_FLOAT,FFX_API_RESOURCE_STATE_COMPUTE_READ);
            if (nms_inputs) d.exposure={};
            d.output=resource(output,FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT,FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
            if (game_formats) {
                d.color.description.format=doom_formats ? FFX_API_SURFACE_FORMAT_R9G9B9E5_SHAREDEXP :
                    FFX_API_SURFACE_FORMAT_R11G11B10_FLOAT;
                if (nms_formats)
                    d.output.description.format=FFX_API_SURFACE_FORMAT_R11G11B10_FLOAT;
                d.depth.description.usage=FFX_API_RESOURCE_USAGE_DEPTHTARGET|FFX_API_RESOURCE_USAGE_STENCILTARGET;
            }
            d.renderSize={kRenderWidth,kRenderHeight}; d.upscaleSize={kOutputWidth,kOutputHeight};
            d.jitterOffset={context_constants.jitter[0],context_constants.jitter[1]};
            d.motionVectorScale={1,1}; d.preExposure=1; d.reset=context_constants.reset;
            if (temporal_test) d.motionVectorScale={float(kRenderWidth),float(kRenderHeight)};
            if (!checked_depth_rejection) {
                const auto valid_usage=d.depth.description.usage;
                d.depth.description.usage = FFX_API_RESOURCE_USAGE_DEPTHTARGET;
                if (ffxDispatch(&api_context,&d.header)==FFX_API_RETURN_OK)
                    throw std::runtime_error("ambiguous depth target was not rejected");
                d.depth.description.usage = valid_usage;
                checked_depth_rejection = true;
                std::cout << "depth_target=cleanly-rejected\n";
            }
            if (ffxDispatch(&api_context,&d.header)!=FFX_API_RETURN_OK)
                throw std::runtime_error("provider dispatch failed");
        };
        if (use_context) {
            if (use_provider) {
                CreateBackendVkDesc backend{{3,nullptr},device,physical_device,vkGetDeviceProcAddr};
                ffxCreateContextDescUpscale create{};
                create.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE,&backend.header};
                create.maxRenderSize={kRenderWidth,kRenderHeight}; create.maxUpscaleSize={kOutputWidth,kOutputHeight};
                if (nms_inputs) create.flags=FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
                if (game_formats) create.flags |= FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
                if(ffxCreateContext(&api_context,&create.header,nullptr)!=FFX_API_RETURN_OK)
                    throw std::runtime_error("provider creation failed");
            } else {
                context = std::make_unique<fsr4core::QualityContext>(
                    physical_device, device, shader_dir, initializer_path,
                    weights_path, false, vkGetDeviceProcAddr,
                    fsr4experiment::render_width, fsr4experiment::render_height,
                    kOutputWidth, kOutputHeight, application.apiVersion);
            }
            record_context();
        } else {
            fsr4::record_quality_frame(
                command_buffer, pipeline_layout, pipelines, descriptor_addresses,
                device_address(device, constants.buffer), history.image,
                recurrent.image, reprojected.image, bind_descriptor_buffers,
                set_descriptor_offsets, bind_embedded_samplers,
                VK_NULL_HANDLE, 0, kOutputWidth, kOutputHeight,
                command_pipeline_barrier2, promoted_dispatch.cmd_write_timestamp2);
        }
        std::cout << "dispatch=quality-frame passes=" << kPasses.size() + (nms_inputs ? 1 : 0)
                  << " status=recorded\n";

        const std::array<const Image*, 3> inspected_images{{
            &output, &history, &reprojected}};
        const std::array<VkImageLayout, 3> inspected_layouts{{
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        }};
        const std::size_t inspected_count = use_context ? 1 : inspected_images.size();
        for (std::size_t index = 0; index < inspected_count; ++index) {
            const auto* image = inspected_images[index];
            image_barrier(command_buffer, image->image, inspected_layouts[index],
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                              VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                          VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        }
        for (std::size_t index = 0; index < inspected_count; ++index) {
            VkBufferImageCopy copy{};
            copy.bufferOffset = output_size * index;
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = {kOutputWidth, kOutputHeight, 1};
            vkCmdCopyImageToBuffer(command_buffer, inspected_images[index]->image,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1,
                                   &copy);
        }
        image_barrier(command_buffer, color.image,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                      VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy color_copy{};
        color_copy.bufferOffset = output_size * inspected_images.size();
        color_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        color_copy.imageSubresource.layerCount = 1;
        color_copy.imageExtent = {kRenderWidth, kRenderHeight, 1};
        vkCmdCopyImageToBuffer(command_buffer, color.image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1,
                               &color_copy);
        check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");
        VkCommandBufferSubmitInfo command_submit{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        command_submit.commandBuffer = command_buffer;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &command_submit;
        check(promoted_dispatch.queue_submit2(queue, 1, &submit, VK_NULL_HANDLE),
              "vkQueueSubmit2/vkQueueSubmit2KHR");
        check(vkQueueWaitIdle(queue), "vkQueueWaitIdle");

        if(temporal_test) {
            if(!output_path.parent_path().empty())fs::create_directories(output_path.parent_path());
            std::ofstream first(output_path.string()+".frame0.rgba16f",std::ios::binary);
            first.write(static_cast<const char*>(readback.mapped),VkDeviceSize(kOutputWidth)*kOutputHeight*8);
            if(!first)throw std::runtime_error("failed to save reset-frame output");
        }

        if (use_context) {
            // Exercise descriptor/event reuse beyond the eight allocated slots.
            // Reset frames use the same oracle input; temporal frames are a
            // lifetime smoke test, not an image-quality oracle.
            const unsigned last_frame=temporal_test ? 1 : 18;
            for (unsigned frame_index = 1; frame_index <= last_frame; ++frame_index) {
                if ((frame_index - 1) % 8 == 0) {
                check(vkResetCommandPool(device, command_pool, 0), "vkResetCommandPool");
                check(vkBeginCommandBuffer(command_buffer, &begin), "vkBeginCommandBuffer(reuse)");
                }
                image_barrier(command_buffer, output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                if (frame_index == 1 && !temporal_test)
                    image_barrier(command_buffer, color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                context_constants.reset = frame_index <= 16 || frame_index == 18;
                if (temporal_test) {
                    generate_reference_frame(frame,1);
                    for (auto& z:frame.depth) z=1.0f-z;
                    std::memcpy(staging_bytes,frame.color.data(),color_size);
                    std::memcpy(staging_bytes+color_size,frame.depth.data(),depth_size);
                    std::memcpy(staging_bytes+color_size+depth_size,frame.motion.data(),motion_size);
                    for (const Image* input : {&color,&depth,&motion})
                        image_barrier(command_buffer,input->image,input==&color ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,VK_ACCESS_2_MEMORY_READ_BIT,
                            VK_PIPELINE_STAGE_2_COPY_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT);
                    copy_to_image(color,0); copy_to_image(depth,color_size); copy_to_image(motion,color_size+depth_size);
                    for (const Image* input : {&color,&depth,&motion})
                        image_barrier(command_buffer,input->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_2_COPY_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                    context_constants.reset=0;
                    context_constants.jitter[0]=reference_jitter(1, 2);
                    context_constants.jitter[1]=reference_jitter(1, 3);
                }
                record_context();
                image_barrier(command_buffer, output.image, VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT);
                VkBufferImageCopy copy{};
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copy.imageExtent = {kOutputWidth, kOutputHeight, 1};
                if (frame_index == last_frame) vkCmdCopyImageToBuffer(command_buffer, output.image,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
                if (frame_index % 8 == 0 || frame_index == last_frame) {
                if (frame_index % 8 == 0) {
                    bool rejected = false;
                    try { record_context(); }
                    catch (const std::runtime_error&) { rejected = true; }
                    if (!rejected) throw std::runtime_error("ninth pending frame was not rejected");
                }
                check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer(reuse)");
                check(promoted_dispatch.queue_submit2(queue, 1, &submit, VK_NULL_HANDLE),
                      "vkQueueSubmit2/vkQueueSubmit2KHR(reuse)");
                check(vkQueueWaitIdle(queue), "vkQueueWaitIdle(reuse)");
                }
            }
            if (temporal_test) std::cout << "context_frames=2 moving_reference_frame=1 reset=false\n";
            else std::cout << "context_frames=19 batch_size=8 overflow=rejected slot_reuse=passed temporal_frame=17 reset_frame=18\n";
        }

        if (!output_path.parent_path().empty()) {
            fs::create_directories(output_path.parent_path());
        }
        if (nms_formats) {
            // Expand packed RGB to the harness's RGBA16F output; alpha is absent
            // from the game format and is reported as one, not compared to FSR alpha.
            auto* bytes=static_cast<uint8_t*>(readback.mapped);
            for (size_t i=size_t(kOutputWidth)*kOutputHeight;i-- > 0;) {
                uint32_t p; std::memcpy(&p,bytes+i*4,4);
                const uint16_t rgba[]{uint16_t((p&2047)<<4),uint16_t(((p>>11)&2047)<<4),
                                      uint16_t((p>>22)<<5),0x3c00};
                std::memcpy(bytes+i*8,rgba,8);
            }
        }
        std::ofstream output_stream(output_path, std::ios::binary);
        output_stream.write(static_cast<const char*>(readback.mapped), output_size);
        if (!output_stream) throw std::runtime_error("failed to write output image");
        const auto* output_bytes = static_cast<const std::uint8_t*>(readback.mapped);
        const auto* scratch_bytes = static_cast<const std::uint8_t*>(scratch.mapped);
        const auto scratch_nonzero = std::count_if(
            scratch_bytes, scratch_bytes + scratch.size,
            [](std::uint8_t value) { return value != 0; });
        if (!use_context) std::cout << "scratch_nonzero_bytes=" << scratch_nonzero << '\n';
        const auto print_image_stats = [&](const char* name, const std::uint8_t* bytes) {
            const auto hash = fnv1a64(bytes, output_size);
            const auto* halves = reinterpret_cast<const std::uint16_t*>(bytes);
            const std::size_t value_count = output_size / sizeof(std::uint16_t);
            float minimum = std::numeric_limits<float>::infinity();
            float maximum = -std::numeric_limits<float>::infinity();
            double sum = 0;
            std::uint64_t nonfinite = 0;
            for (std::size_t index = 0; index < value_count; ++index) {
                const float value = half_to_float(halves[index]);
                if (!std::isfinite(value)) {
                    ++nonfinite;
                    continue;
                }
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
                sum += value;
            }
            std::cout << std::hex << std::setfill('0') << name << "_fnv1a64="
                      << std::setw(16) << hash << std::dec << '\n';
            std::cout << std::setprecision(12) << name << "_min=" << minimum
                      << " max=" << maximum
                      << " mean=" << (sum / static_cast<double>(value_count - nonfinite))
                      << " nonfinite=" << nonfinite << '\n';
            if (nonfinite) throw std::runtime_error("nonfinite output values");
        };
        print_image_stats("output", output_bytes);
        if (!use_context) {
            print_image_stats("history", output_bytes + output_size);
            print_image_stats("reprojected", output_bytes + output_size * 2);
        }
        const auto* uploaded_color = output_bytes + output_size * 3;
        const auto color_hash = fnv1a64(uploaded_color, color_size);
        std::cout << std::hex << std::setfill('0') << "uploaded_color_fnv1a64="
                  << std::setw(16) << color_hash << " expected=" << std::setw(16)
                  << expected_color_hash << std::dec << '\n';
        std::cout << "output=" << output_path << '\n';
    } catch (const std::exception& exception) {
        std::cerr << "error=" << exception.what() << '\n';
        if (device) vkDeviceWaitIdle(device);
        context.reset();
        if (api_context) ffxDestroyContext(&api_context,nullptr);
        for (auto pipeline : pipelines) vkDestroyPipeline(device, pipeline, nullptr);
        for (auto module : modules) vkDestroyShaderModule(device, module, nullptr);
        if (sampler) vkDestroySampler(device, sampler, nullptr);
        if (pipeline_layout) vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        for (auto layout : set_layouts) if (layout) vkDestroyDescriptorSetLayout(device, layout, nullptr);
        for (auto& image : images) destroy_image(device, image);
        for (auto& buffer : buffers) destroy_buffer(device, buffer);
        if (command_pool) vkDestroyCommandPool(device, command_pool, nullptr);
        if (device) vkDestroyDevice(device, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
        return 1;
    }

    context.reset();
    if (api_context) ffxDestroyContext(&api_context,nullptr);
    for (auto pipeline : pipelines) vkDestroyPipeline(device, pipeline, nullptr);
    for (auto module : modules) vkDestroyShaderModule(device, module, nullptr);
    vkDestroySampler(device, sampler, nullptr);
    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    for (auto layout : set_layouts) vkDestroyDescriptorSetLayout(device, layout, nullptr);
    for (auto& image : images) destroy_image(device, image);
    for (auto& buffer : buffers) destroy_buffer(device, buffer);
    vkDestroyCommandPool(device, command_pool, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
}
