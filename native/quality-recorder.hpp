#pragma once
#include "resolution-plan.hpp"
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace fsr4 {
struct Pass {
    const char* name;
    const char* hash;
    std::uint32_t groups_x;
    std::uint32_t groups_y;
    std::array<std::uint32_t, 2> table_bases;
    std::uint32_t table_base_count;
};

constexpr std::array<Pass, 14> kPasses{{
    {"pre", "9e5877bf41984816", 120, 68, {0, 12}, 2},
    {"model_1", "3d9ec844637ea028", 15, 540, {19, 0}, 1},
    {"model_2", "c23238ca713f8f78", 15, 540, {21, 0}, 1},
    {"model_3", "5dab395eb925383e", 8, 270, {23, 0}, 1},
    {"model_4", "6df946114912c931", 8, 270, {25, 0}, 1},
    {"model_5", "32ada0046ef27f1b", 8, 270, {27, 0}, 1},
    {"model_6", "0531325fe892e91e", 4, 135, {29, 31}, 2},
    {"model_7", "85eaf87348d33113", 4, 135, {33, 35}, 2},
    {"model_8", "a2411c43d0d109fe", 4, 135, {37, 39}, 2},
    {"model_9", "bdd6574e0d73e2f0", 30, 17, {41, 43}, 2},
    {"model_10", "1e12c37876ceeb66", 8, 270, {45, 0}, 1},
    {"model_11", "607b5889b24360d4", 8, 270, {47, 0}, 1},
    {"model_12", "2436e9ea16dc2fb4", 15, 540, {49, 0}, 1},
    {"post", "c008d8a012f99497", 120, 68, {51, 63}, 2},
}};


inline void image_barrier(PFN_vkCmdPipelineBarrier2 pipeline_barrier2,
                   VkCommandBuffer command_buffer,
                   VkImage image,
                   VkImageLayout old_layout,
                   VkImageLayout new_layout,
                   VkPipelineStageFlags2 source_stage,
                   VkAccessFlags2 source_access,
                   VkPipelineStageFlags2 destination_stage,
                   VkAccessFlags2 destination_access,
                   VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = source_stage;
    barrier.srcAccessMask = source_access;
    barrier.dstStageMask = destination_stage;
    barrier.dstAccessMask = destination_access;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    pipeline_barrier2(command_buffer, &dependency);
}

inline void image_barrier(VkCommandBuffer command_buffer,
                   VkImage image,
                   VkImageLayout old_layout,
                   VkImageLayout new_layout,
                   VkPipelineStageFlags2 source_stage,
                   VkAccessFlags2 source_access,
                   VkPipelineStageFlags2 destination_stage,
                   VkAccessFlags2 destination_access,
                   VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
    image_barrier(vkCmdPipelineBarrier2, command_buffer, image, old_layout,
                  new_layout, source_stage, source_access, destination_stage,
                  destination_access, aspect);
}

inline void compute_barrier(PFN_vkCmdPipelineBarrier2 pipeline_barrier2,
                            VkCommandBuffer command_buffer) {
    VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    pipeline_barrier2(command_buffer, &dependency);
}

inline void compute_barrier(VkCommandBuffer command_buffer) {
    compute_barrier(vkCmdPipelineBarrier2, command_buffer);
}


// Records exactly the validated Quality 1280x720 -> 1920x1080 pass sequence.
// Caller owns all resources, submission and lifetime. Inputs/history/recurrent
// must be SHADER_READ_ONLY_OPTIMAL; reprojected/output must be GENERAL.
// On return history/recurrent are GENERAL; reprojected is SHADER_READ_ONLY_OPTIMAL.
// Descriptor and constant contents must remain stable until GPU completion.
inline void record_quality_frame(
    VkCommandBuffer command_buffer, VkPipelineLayout pipeline_layout,
    std::span<const VkPipeline> pipelines,
    const std::array<VkDeviceAddress, 3>& descriptor_addresses,
    VkDeviceAddress constants_address,
    VkImage history, VkImage recurrent, VkImage reprojected,
    PFN_vkCmdBindDescriptorBuffersEXT bind_descriptor_buffers,
    PFN_vkCmdSetDescriptorBufferOffsetsEXT set_descriptor_offsets,
    PFN_vkCmdBindDescriptorBufferEmbeddedSamplersEXT bind_embedded_samplers,
    VkQueryPool timestamps = VK_NULL_HANDLE, uint32_t timestamp_base = 0,
    uint32_t output_width=1920,uint32_t output_height=1080,
    PFN_vkCmdPipelineBarrier2 pipeline_barrier2=vkCmdPipelineBarrier2,
    PFN_vkCmdWriteTimestamp2 write_timestamp2=vkCmdWriteTimestamp2) {
    if (!command_buffer || !pipeline_layout || pipelines.size() != kPasses.size() ||
        !constants_address || !history || !recurrent || !reprojected ||
        !bind_descriptor_buffers || !set_descriptor_offsets || !bind_embedded_samplers ||
        !pipeline_barrier2 || (timestamps && !write_timestamp2))
        throw std::invalid_argument("incomplete Quality frame recording arguments");
    for (auto pipeline : pipelines)
        if (!pipeline) throw std::invalid_argument("null Quality pipeline");
    for (auto address : descriptor_addresses)
        if (!address) throw std::invalid_argument("null descriptor buffer address");
    std::array<VkDescriptorBufferBindingInfoEXT, 3> descriptor_bindings_info{};
    for (std::size_t index = 0; index < descriptor_bindings_info.size(); ++index) {
        descriptor_bindings_info[index].sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
        descriptor_bindings_info[index].address =
            descriptor_addresses[index];
        descriptor_bindings_info[index].usage =
            index == 0
                ? VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT
                : VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
    }
    bind_descriptor_buffers(command_buffer, descriptor_bindings_info.size(),
                            descriptor_bindings_info.data());
    constexpr std::array<std::uint32_t, 3> descriptor_buffer_indices{{0, 1, 2}};
    constexpr std::array<VkDeviceSize, 3> descriptor_buffer_offsets{{0, 0, 0}};
    set_descriptor_offsets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                           pipeline_layout, 0, descriptor_buffer_indices.size(),
                           descriptor_buffer_indices.data(),
                           descriptor_buffer_offsets.data());
    bind_embedded_samplers(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                           pipeline_layout, 3);
    const VkDeviceAddress weights_address = constants_address + 1024;
    for (std::size_t index = 0; index < kPasses.size(); ++index) {
        const auto& pass = kPasses[index];
        if (index == kPasses.size() - 1) {
            for (VkImage image : {history, recurrent}) {
                image_barrier(pipeline_barrier2, command_buffer, image,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_IMAGE_LAYOUT_GENERAL,
                              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                              VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                              VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
            }
            image_barrier(pipeline_barrier2, command_buffer, reprojected,
                          VK_IMAGE_LAYOUT_GENERAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }
        std::array<std::uint32_t, 6> push{};
        std::uint32_t push_size = 0;
        if (index == 0) {
            // The translated pre-pass declares pass weights first and the
            // main FSR constants second in its physical-address root block.
            push[0] = static_cast<std::uint32_t>(weights_address);
            push[1] = static_cast<std::uint32_t>(weights_address >> 32);
            push[2] = static_cast<std::uint32_t>(constants_address);
            push[3] = static_cast<std::uint32_t>(constants_address >> 32);
            push[4] = pass.table_bases[0];
            push[5] = pass.table_bases[1];
            push_size = 24;
        } else if (index == kPasses.size() - 1) {
            push[0] = static_cast<std::uint32_t>(constants_address);
            push[1] = static_cast<std::uint32_t>(constants_address >> 32);
            push[2] = pass.table_bases[0];
            push[3] = pass.table_bases[1];
            push_size = 16;
        } else {
            push[0] = pass.table_bases[0];
            push[1] = pass.table_bases[1];
            push_size = pass.table_base_count * 4;
        }
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          pipelines[index]);
        if (timestamps) write_timestamp2(command_buffer,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                         timestamps,timestamp_base+uint32_t(index)*2);
        vkCmdPushConstants(command_buffer, pipeline_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, push_size, push.data());
        const auto groups=quality_dispatch(uint32_t(index),output_width,output_height);
        vkCmdDispatch(command_buffer, groups[0], groups[1], 1);
        if (timestamps) write_timestamp2(command_buffer,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                         timestamps,timestamp_base+uint32_t(index)*2+1);
        if (index + 1 < kPasses.size()) compute_barrier(pipeline_barrier2, command_buffer);
    }


}
} // namespace fsr4
