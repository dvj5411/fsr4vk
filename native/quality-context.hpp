#pragma once
#include "experimental-resolution.hpp"
#include "quality-recorder.hpp"
#include "embedded-assets.hpp"
#include "vulkan-compat.hpp"
#include "../provider/vulkan_device_features.hpp"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

namespace fsr4core {
namespace fs = std::filesystem;
using fsr4::kPasses;
using fsr4::image_barrier;
constexpr uint32_t kDescriptorCount = 128;
constexpr uint32_t kOutputWidth = 1920, kOutputHeight = 1080;
constexpr VkDeviceSize kScratchSize = 20880256;
constexpr VkDeviceSize kPhysicalConstantBackingSize = 2048;
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

inline void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                                 std::to_string(result));
    }
}

inline std::vector<std::uint8_t> read_bytes(const fs::path& path) {
    if (fsr4assets::is_embedded(path)) return fsr4assets::read(path);
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

inline std::vector<std::uint32_t> read_spirv(const fs::path& path) {
    const auto bytes = read_bytes(path);
    if (bytes.size() % 4) throw std::runtime_error("invalid SPIR-V size");
    std::vector<std::uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    return words;
}

inline std::uint32_t memory_type(VkPhysicalDevice physical_device,
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

inline Buffer create_buffer(VkPhysicalDevice physical_device,
                     VkDevice device,
                     VkDeviceSize size,
                     VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags properties,
                     bool device_address = false) {
    Buffer result{};
    result.size = size;
    try {
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
    } catch (...) {
        if (result.mapped) vkUnmapMemory(device,result.memory);
        if (result.buffer) vkDestroyBuffer(device,result.buffer,nullptr);
        if (result.memory) vkFreeMemory(device,result.memory,nullptr);
        throw;
    }
}

inline Image create_image(VkPhysicalDevice physical_device,
                   VkDevice device,
                   std::uint32_t width,
                   std::uint32_t height,
                   VkFormat format,
                   VkImageUsageFlags usage) {
    Image result{};
    result.format = format;
    result.width = width;
    result.height = height;
    try {
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
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    check(vkCreateImageView(device, &view_info, nullptr, &result.view), "vkCreateImageView");
    return result;
    } catch (...) {
        if (result.view) vkDestroyImageView(device,result.view,nullptr);
        if (result.image) vkDestroyImage(device,result.image,nullptr);
        if (result.memory) vkFreeMemory(device,result.memory,nullptr);
        throw;
    }
}

inline void destroy_buffer(VkDevice device, Buffer& buffer) {
    if (buffer.mapped) vkUnmapMemory(device, buffer.memory);
    if (buffer.buffer) vkDestroyBuffer(device, buffer.buffer, nullptr);
    if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
    buffer = {};
}

inline void destroy_image(VkDevice device, Image& image) {
    if (image.view) vkDestroyImageView(device, image.view, nullptr);
    if (image.image) vkDestroyImage(device, image.image, nullptr);
    if (image.memory) vkFreeMemory(device, image.memory, nullptr);
    image = {};
}


inline VkDeviceAddress device_address(PFN_vkGetBufferDeviceAddress get_buffer_device_address,
                                      VkDevice device, VkBuffer buffer) {
    VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    info.buffer = buffer;
    return get_buffer_device_address(device, &info);
}

// Experimental fixed-Quality context. Caller submits frames in recording order
// on one queue, and completes all submissions before destroying this object.
class QualityContext {
    VkPhysicalDevice physical_device;
    VkDevice device;
    PFN_vkGetDeviceProcAddr get_device_proc_addr;
    fsr4vk::DeviceDispatch dispatch;
    VkSampler sampler{};
    VkPipelineLayout pipeline_layout{};
    std::array<VkDescriptorSetLayout,4> set_layouts{};
    std::vector<VkShaderModule> modules;
    std::vector<VkPipeline> pipelines;
    std::vector<Buffer> buffers;
    std::vector<Image> images;
    std::vector<uint8_t> weights;
    bool initialized = false;
    bool nms_inputs = false;
    bool native_mixed_dot = false;
    VkQueryPool profile_queries{};
    double timestamp_period = 0;
    std::string profile_path;
    float previous_exposure = 0;
    struct Frame {
        std::array<Buffer,3> descriptors{};
        Buffer constants{};
        VkEvent done{};
        bool used = false;
        std::vector<VkImageView> views;
    };
    std::array<Frame,8> frames{};
    size_t next_frame = 0;
    void release() {
        if (profile_queries) {
            // Caller has completed GPU work before destruction. Read only the
            // final frame; repeated resets are ordered on the context's queue.
            try {
                std::array<uint64_t,30> values{};
                const uint32_t count=nms_inputs ? 30 : 28;
                if (initialized && vkGetQueryPoolResults(device,profile_queries,0,count,sizeof(values),
                    values.data(),sizeof(uint64_t),VK_QUERY_RESULT_64_BIT)==VK_SUCCESS) {
                    std::ofstream out(profile_path);
                    out << "pass,gpu_ms\n";
                    for (uint32_t i=0;i<count/2;++i) {
                        const char* name=nms_inputs && i==0 ? "auto_exposure" : kPasses[i-(nms_inputs ? 1 : 0)].name;
                        out << name << ',' << double(values[i*2+1]-values[i*2])*timestamp_period/1e6 << '\n';
                    }
                }
            } catch (...) { }
            vkDestroyQueryPool(device,profile_queries,nullptr);
        }
        for (auto& f : frames) {
            for (auto view : f.views) vkDestroyImageView(device,view,nullptr);
            if (f.done) vkDestroyEvent(device,f.done,nullptr);
            for (auto& b : f.descriptors) destroy_buffer(device,b);
            destroy_buffer(device,f.constants);
        }
        for (auto p : pipelines) vkDestroyPipeline(device,p,nullptr);
        for (auto m : modules) vkDestroyShaderModule(device,m,nullptr);
        if (pipeline_layout) vkDestroyPipelineLayout(device,pipeline_layout,nullptr);
        for (auto l : set_layouts) if (l) vkDestroyDescriptorSetLayout(device,l,nullptr);
        if (sampler) vkDestroySampler(device,sampler,nullptr);
        for (auto& i : images) destroy_image(device,i);
        for (auto& b : buffers) destroy_buffer(device,b);
    }
public:
    const char* shader_backend() const {
        return native_mixed_dot ? "native-mixed-dot" : "portable-int8";
    }
    QualityContext(const QualityContext&) = delete;
    QualityContext& operator=(const QualityContext&) = delete;
    QualityContext(VkPhysicalDevice physical, VkDevice logical, const fs::path& shader_dir,
                   const fs::path& initializer_path, const fs::path& weights_path,
                   bool nms = false,
                   PFN_vkGetDeviceProcAddr get_device_proc_addr = vkGetDeviceProcAddr,
                   uint32_t max_input_width=fsr4experiment::render_width,
                   uint32_t max_input_height=fsr4experiment::render_height,
                   uint32_t max_output_width=1920,uint32_t max_output_height=1080,
                   uint32_t api_version=VK_API_VERSION_1_3)
        : physical_device(physical), device(logical),
          get_device_proc_addr(get_device_proc_addr),
          dispatch(logical, get_device_proc_addr, api_version), nms_inputs(nms) {
      try {
        native_mixed_dot = fsr4vk::supportsNativeMixedDot(
            physical, vkGetPhysicalDeviceFeatures2, vkEnumerateDeviceExtensionProperties);
        if (const char* path=std::getenv("FSR4_VK_PROFILE_PATH")) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(physical_device,&properties);
            if (!properties.limits.timestampComputeAndGraphics)
                throw std::runtime_error("compute timestamps unavailable");
            timestamp_period=properties.limits.timestampPeriod;
            profile_path=path;
            VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            query.queryType=VK_QUERY_TYPE_TIMESTAMP; query.queryCount=30;
            check(vkCreateQueryPool(device,&query,nullptr,&profile_queries),"profile query pool");
        }
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
        std::vector<fsr4::Pass> selected_passes(kPasses.begin(), kPasses.end());
        if (nms_inputs) {
            // Descriptor table and constants below follow the captured SPD root
            // layout. Mip scratch allocation matches the provider's ceil(size/16).
            selected_passes[0].hash = "c853540cef1e8d64";
            selected_passes.push_back({"auto_exposure", "996136f00380aba8", 20, 12, {90,93}, 2});
        }
        const bool general_bundle=fsr4assets::is_embedded(shader_dir) || fs::exists(shader_dir/"pass-00.spv");
        for (const auto& pass : selected_passes) {
            const auto index=unsigned(&pass-selected_passes.data());
            const auto canonical=index==14 ? 0u : index+1;
            auto name=general_bundle ? "pass-"+std::string(canonical<10 ? "0" : "")+std::to_string(canonical)+".spv"
                                           : std::string(pass.hash)+".spv";
            if (!native_mixed_dot)
                name.insert(name.size() - 4, ".portable");
            const auto code = read_spirv(shader_dir / name);
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


        auto get_layout_size = reinterpret_cast<PFN_vkGetDescriptorSetLayoutSizeEXT>(
            get_device_proc_addr(device, "vkGetDescriptorSetLayoutSizeEXT"));
        auto get_binding_offset =
            reinterpret_cast<PFN_vkGetDescriptorSetLayoutBindingOffsetEXT>(
                get_device_proc_addr(device,
                                    "vkGetDescriptorSetLayoutBindingOffsetEXT"));
        auto get_descriptor = reinterpret_cast<PFN_vkGetDescriptorEXT>(
            get_device_proc_addr(device, "vkGetDescriptorEXT"));
        auto bind_descriptor_buffers =
            reinterpret_cast<PFN_vkCmdBindDescriptorBuffersEXT>(
                get_device_proc_addr(device, "vkCmdBindDescriptorBuffersEXT"));
        auto set_descriptor_offsets =
            reinterpret_cast<PFN_vkCmdSetDescriptorBufferOffsetsEXT>(
                get_device_proc_addr(device,
                                    "vkCmdSetDescriptorBufferOffsetsEXT"));
        auto bind_embedded_samplers =
            reinterpret_cast<PFN_vkCmdBindDescriptorBufferEmbeddedSamplersEXT>(
                get_device_proc_addr(
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
            physical_device, device, fsr4::scratch_capacity(max_output_width,max_output_height),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true));
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

        // Immutable template descriptors are prepared per frame below.
        frames[0].constants = constants;
        buffers.back() = {};
        weights = weights_bytes;
        for (auto& f : frames) {
            for (size_t i=0;i<3;++i)
                f.descriptors[i] = create_buffer(physical_device,device,descriptor_layout_sizes[i],
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                    (i==0 ? VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT :
                            VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT),
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,true);
            if (!f.constants.buffer)
                f.constants = create_buffer(physical_device,device,2048,
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,true);
            VkEventCreateInfo ei{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
            check(vkCreateEvent(device,&ei,nullptr,&f.done),"vkCreateEvent");
        }
        images.reserve(3);
        for (auto format : {VK_FORMAT_R16G16B16A16_SFLOAT,
                            VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R8G8B8A8_UNORM})
            images.push_back(create_image(physical_device,device,fsr4::round_up(max_output_width,8),fsr4::round_up(max_output_height,8),format,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));
        if (nms_inputs) {
            const auto usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            images.push_back(create_image(physical_device,device,2,1,VK_FORMAT_R32_SFLOAT,usage));
            images.push_back(create_image(physical_device,device,1,1,VK_FORMAT_R32_UINT,usage));
            images.push_back(create_image(physical_device,device,fsr4::divide_up(max_input_width,16),fsr4::divide_up(max_input_height,16),VK_FORMAT_R32_SFLOAT,usage));
        }
      } catch (...) { release(); throw; }
    }
    ~QualityContext() { release(); }
    // External views must outlive GPU completion. Sampled inputs are READ_ONLY,
    // output is GENERAL. All inputs have fixed dimensions as in the smoke test.
    void record(VkCommandBuffer command_buffer, Image color, Image depth,
                Image motion, Image exposure_image, Image output,
                OptimizedConstants main_constants) {
        auto& frame = frames[next_frame];
        if (frame.used) {
            auto status = vkGetEventStatus(device,frame.done);
            if (status != VK_EVENT_SET)
                throw std::runtime_error("Quality context has eight frames still in flight");
            check(vkResetEvent(device,frame.done),"vkResetEvent");
            frame.used = false;
        }
        for (auto view : frame.views) vkDestroyImageView(device,view,nullptr);
        frame.views.clear();
        frame.views.reserve(5);
        if (nms_inputs) exposure_image = images[3];
        for (auto* image : {&color,&depth,&motion,&exposure_image,&output}) {
            if (!image->view) {
                VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                vi.image=image->image; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=image->format;
                const bool depth_format=image->format==VK_FORMAT_D32_SFLOAT_S8_UINT;
                vi.subresourceRange={depth_format ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
                check(vkCreateImageView(device,&vi,nullptr,&image->view),"external image view");
                frame.views.push_back(image->view);
            }
        }
        auto& history = images[0];
        auto& reprojected = images[1];
        auto& recurrent = images[2];
        auto& scratch = buffers[3];
        auto& initializer = buffers[4];
        auto get_layout_size = reinterpret_cast<PFN_vkGetDescriptorSetLayoutSizeEXT>(
            get_device_proc_addr(device, "vkGetDescriptorSetLayoutSizeEXT"));
        auto get_binding_offset =
            reinterpret_cast<PFN_vkGetDescriptorSetLayoutBindingOffsetEXT>(
                get_device_proc_addr(device,
                                    "vkGetDescriptorSetLayoutBindingOffsetEXT"));
        auto get_descriptor = reinterpret_cast<PFN_vkGetDescriptorEXT>(
            get_device_proc_addr(device, "vkGetDescriptorEXT"));
        auto bind_descriptor_buffers =
            reinterpret_cast<PFN_vkCmdBindDescriptorBuffersEXT>(
                get_device_proc_addr(device, "vkCmdBindDescriptorBuffersEXT"));
        auto set_descriptor_offsets =
            reinterpret_cast<PFN_vkCmdSetDescriptorBufferOffsetsEXT>(
                get_device_proc_addr(device,
                                    "vkCmdSetDescriptorBufferOffsetsEXT"));
        auto bind_embedded_samplers =
            reinterpret_cast<PFN_vkCmdBindDescriptorBufferEmbeddedSamplersEXT>(
                get_device_proc_addr(
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



        // Redirect writes to this frame's descriptor storage.
        const auto write_image_descriptor_buffer = [&](uint32_t index, VkDescriptorType type, VkImageView view) {
            VkDescriptorImageInfo ii{};
            ii.imageView=view;
            ii.imageLayout=type==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ?
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
            VkDescriptorGetInfoEXT di{VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
            di.type=type;
            size_t size;
            if (type==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
                di.data.pSampledImage=&ii; size=descriptor_properties.sampledImageDescriptorSize;
            } else { di.data.pStorageImage=&ii; size=descriptor_properties.storageImageDescriptorSize; }
            get_descriptor(device,&di,size,static_cast<uint8_t*>(frame.descriptors[1].mapped)+
                descriptor_binding_offsets[1]+index*mutable_descriptor_stride);
        };
        const auto write_buffer_descriptor_buffer = [&](uint32_t index,const Buffer& buffer) {
            VkDescriptorAddressInfoEXT ai{VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT};
            ai.address=device_address(dispatch.get_buffer_device_address, device,buffer.buffer); ai.range=buffer.size;
            VkDescriptorGetInfoEXT di{VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
            di.type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; di.data.pStorageBuffer=&ai;
            get_descriptor(device,&di,descriptor_properties.storageBufferDescriptorSize,
                static_cast<uint8_t*>(frame.descriptors[2].mapped)+
                descriptor_binding_offsets[2]+index*mutable_descriptor_stride);
        };
        for (auto& b:frame.descriptors) std::memset(b.mapped,0,b.size);
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
        if (nms_inputs) {
            write_image_descriptor_buffer(90,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,images[4].view);
            write_image_descriptor_buffer(91,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,images[5].view);
            write_image_descriptor_buffer(92,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,images[3].view);
            write_image_descriptor_buffer(93,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,color.view);
        }

        for (const auto index : {
                 11u, 20u, 22u, 24u, 26u, 28u, 30u,
                 34u, 38u, 42u, 46u, 48u, 50u, 62u}) {
            write_buffer_descriptor_buffer(index, scratch);
        }
        for (const auto index : {32u, 36u, 40u, 44u}) {
            write_buffer_descriptor_buffer(index, initializer);
        }
        write_buffer_descriptor_buffer(62, scratch);  // post u11


        main_constants.previous_pre_exposure = previous_exposure;
        main_constants.reset = main_constants.reset || !initialized;
        std::memset(frame.constants.mapped,0,2048);
        std::memcpy(frame.constants.mapped,&main_constants,sizeof(main_constants));
        if (nms_inputs) {
            struct SpdConstants { uint32_t mips, groups, offset[2]; float inverse_size[2], pre_exposure, pad; };
            const auto rw=main_constants.width_lr, rh=main_constants.height_lr;
            const auto gx=(rw+63)/64, gy=(rh+63)/64;
            unsigned mips=0;for(auto size=std::max(rw,rh);size>1;size>>=1)++mips;
            mips=std::min(mips,12u);
            const SpdConstants spd{mips,gx*gy,{0,0},{1.0f/rw,1.0f/rh},main_constants.pre_exposure,0};
            static_assert(sizeof(spd)==32);
            std::memcpy(static_cast<uint8_t*>(frame.constants.mapped)+512,&spd,sizeof(spd));
        }
        std::memcpy(static_cast<uint8_t*>(frame.constants.mapped)+1024,weights.data(),weights.size());
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        if (!initialized) {
            VkClearColorValue zero{};
            for (auto& i:images) {
                image_barrier(dispatch.cmd_pipeline_barrier2,command_buffer,i.image,VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_2_NONE,0,
                    VK_PIPELINE_STAGE_2_CLEAR_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT);
                vkCmdClearColorImage(command_buffer,i.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&zero,1,&range);
                image_barrier(dispatch.cmd_pipeline_barrier2,command_buffer,i.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    i.image!=history.image && i.image!=recurrent.image ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_CLEAR_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_READ_BIT|VK_ACCESS_2_SHADER_WRITE_BIT);
            }
        } else {
            for (auto* i : {&history,&recurrent})
                image_barrier(dispatch.cmd_pipeline_barrier2,command_buffer,i->image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_READ_BIT);
            image_barrier(dispatch.cmd_pipeline_barrier2,command_buffer,reprojected.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_WRITE_BIT);
        }
        // Retain the zero-initialized scratch behavior of the reference runner.
        VkMemoryBarrier2 before{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        before.srcStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        before.srcAccessMask=VK_ACCESS_2_MEMORY_READ_BIT|VK_ACCESS_2_MEMORY_WRITE_BIT;
        before.dstStageMask=VK_PIPELINE_STAGE_2_CLEAR_BIT;
        before.dstAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.memoryBarrierCount=1; dep.pMemoryBarriers=&before;
        dispatch.cmd_pipeline_barrier2(command_buffer,&dep);
        vkCmdFillBuffer(command_buffer,scratch.buffer,0,VK_WHOLE_SIZE,0);
        before.srcStageMask=VK_PIPELINE_STAGE_2_CLEAR_BIT|VK_PIPELINE_STAGE_2_HOST_BIT;
        before.srcAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT|VK_ACCESS_2_HOST_WRITE_BIT;
        before.dstStageMask=VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        before.dstAccessMask=VK_ACCESS_2_SHADER_READ_BIT|VK_ACCESS_2_SHADER_WRITE_BIT|VK_ACCESS_2_DESCRIPTOR_BUFFER_READ_BIT_EXT;
        dispatch.cmd_pipeline_barrier2(command_buffer,&dep);
        std::array<VkDeviceAddress,3> addresses{};
        for(size_t i=0;i<3;++i) addresses[i]=device_address(
            dispatch.get_buffer_device_address,device,frame.descriptors[i].buffer);
        if (nms_inputs) {
            if (initialized)
                image_barrier(dispatch.cmd_pipeline_barrier2,command_buffer,images[3].image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_READ_BIT|VK_ACCESS_2_SHADER_WRITE_BIT);
            fsr4::compute_barrier(dispatch.cmd_pipeline_barrier2,command_buffer);
            std::array<VkDescriptorBufferBindingInfoEXT,3> bindings{};
            for (size_t i=0;i<3;++i) {
                bindings[i].sType=VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
                bindings[i].address=addresses[i];
                bindings[i].usage=i==0 ? VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT :
                                       VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
            }
            bind_descriptor_buffers(command_buffer,3,bindings.data());
            const uint32_t indices[]{0,1,2}; const VkDeviceSize offsets[]{0,0,0};
            set_descriptor_offsets(command_buffer,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout,0,3,indices,offsets);
            const auto address=device_address(dispatch.get_buffer_device_address,device,frame.constants.buffer)+512;
            const uint32_t push[]{uint32_t(address),uint32_t(address>>32),90,93};
            vkCmdBindPipeline(command_buffer,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines.back());
            vkCmdPushConstants(command_buffer,pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),push);
            if (profile_queries) {
                vkCmdResetQueryPool(command_buffer,profile_queries,0,30);
                dispatch.cmd_write_timestamp2(command_buffer,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,profile_queries,0);
            }
            vkCmdDispatch(command_buffer,(main_constants.width_lr+63)/64,
                          (main_constants.height_lr+63)/64,1);
            if (profile_queries) dispatch.cmd_write_timestamp2(
                command_buffer,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,profile_queries,1);
            image_barrier(dispatch.cmd_pipeline_barrier2,command_buffer,images[3].image,VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_READ_BIT);
        }
        if (profile_queries && !nms_inputs) vkCmdResetQueryPool(command_buffer,profile_queries,0,30);
        fsr4::record_quality_frame(command_buffer,pipeline_layout,std::span<const VkPipeline>(pipelines).first(kPasses.size()),addresses,
            device_address(dispatch.get_buffer_device_address,device,frame.constants.buffer),history.image,recurrent.image,reprojected.image,
            bind_descriptor_buffers,set_descriptor_offsets,bind_embedded_samplers,profile_queries,nms_inputs ? 2 : 0,
            main_constants.width,main_constants.height,dispatch.cmd_pipeline_barrier2,
            dispatch.cmd_write_timestamp2);
        VkMemoryBarrier2 end{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        end.srcStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        end.srcAccessMask=VK_ACCESS_2_MEMORY_WRITE_BIT|VK_ACCESS_2_MEMORY_READ_BIT;
        VkDependencyInfo done{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        done.memoryBarrierCount=1; done.pMemoryBarriers=&end;
        dispatch.cmd_set_event2(command_buffer,frame.done,&done);
        frame.used=true;
        next_frame=(next_frame+1)%frames.size();
        initialized=true;
        previous_exposure=main_constants.pre_exposure;
    }
};
} // namespace fsr4core
