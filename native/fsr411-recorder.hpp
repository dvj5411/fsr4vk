#pragma once
#include "quality-recorder.hpp"

namespace fsr4 {
// 4.1.1 separates tensor-border clearing from each convolution. The root
// layouts below come from the selected INT8 provider in the official SDK DLL.
// Tables are rebased onto the same compact descriptor heaps as the 4.0.2 core.
inline constexpr std::array<uint32_t,17> k411TensorDivisors{
    2,1,2,2,4,4,4,8,8,8,4,4,2,2,1,1,1};
inline constexpr std::array<uint32_t,13> k411BorderDivisors{
    2,2,2,4,4,4,8,8,8,4,4,2,2};
inline constexpr std::array<const char*,27> k411PassNames{
    "pre", "border_0", "model_1", "border_1", "model_2", "border_2",
    "model_3", "border_3", "model_4", "border_4", "model_5", "border_5",
    "model_6", "border_6", "model_7", "border_7", "model_8", "border_8",
    "model_9", "border_9", "model_10", "border_10", "model_11", "border_11",
    "model_12", "border_12", "post"};

inline std::array<uint32_t,2> fsr411_dispatch(uint32_t pass, uint32_t width, uint32_t height,
                                            uint32_t capacity_width, uint32_t capacity_height) {
    if(pass>26 || width>capacity_width || height>capacity_height)
        throw std::invalid_argument("invalid 4.1.1 dispatch dimensions");
    if(pass==0 || pass==26)
        return {divide_up(width,pass==0 ? 16 : 32),divide_up(height,pass==0 ? 16 : 32)};
    // Neural tensors are padded to eight pixels; pre/post and their textures
    // retain the application's exact output dimensions in 4.1.1.
    width=round_up(width,8); height=round_up(height,8);
    if(pass%2==1) {
        const auto divisor=k411BorderDivisors[pass/2];
        const auto w=width/divisor,h=height/divisor;
        const auto px=std::min(capacity_width/divisor+1-w,5u);
        const auto py=std::min(capacity_height/divisor+1-h,1u);
        return {divide_up((w+1+px)*(1+py)+h*(1+px),32),1};
    }
    const auto model=pass/2;
    const auto divisor=(model<=2 || model==12) ? 2u : (model<=5 || model>=10) ? 4u : 8u;
    return {divide_up(width/divisor,64),height/divisor};
}

inline void record_fsr411_frame(
    VkCommandBuffer command_buffer, VkPipelineLayout pipeline_layout,
    std::span<const VkPipeline> pipelines,
    const std::array<VkDeviceAddress,3>& descriptor_addresses,
    VkDeviceAddress constants_address, VkImage history, VkImage recurrent,
    VkImage reprojected,
    PFN_vkCmdBindDescriptorBuffersEXT bind_descriptor_buffers,
    PFN_vkCmdSetDescriptorBufferOffsetsEXT set_descriptor_offsets,
    PFN_vkCmdBindDescriptorBufferEmbeddedSamplersEXT bind_embedded_samplers,
    VkQueryPool timestamps, uint32_t timestamp_base,
    uint32_t width, uint32_t height, uint32_t capacity_width, uint32_t capacity_height,
    PFN_vkCmdPipelineBarrier2 pipeline_barrier2,
    PFN_vkCmdWriteTimestamp2 write_timestamp2) {
    if (pipelines.size()!=27) throw std::invalid_argument("4.1.1 requires 27 model passes");
    std::array<VkDescriptorBufferBindingInfoEXT,3> bindings{};
    for (size_t i=0;i<3;++i) {
        bindings[i].sType=VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
        bindings[i].address=descriptor_addresses[i];
        bindings[i].usage=i==0 ? VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT :
                                VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
    }
    bind_descriptor_buffers(command_buffer,3,bindings.data());
    const uint32_t indices[]{0,1,2}; const VkDeviceSize offsets[]{0,0,0};
    set_descriptor_offsets(command_buffer,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout,0,3,indices,offsets);
    bind_embedded_samplers(command_buffer,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout,3);
    for (uint32_t i=0;i<27;++i) {
        if (i==26) {
            for (auto image : {history,recurrent})
                image_barrier(pipeline_barrier2,command_buffer,image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_STORAGE_READ_BIT|VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
            image_barrier(pipeline_barrier2,command_buffer,reprojected,VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }
        const bool border=i%2==1;
        const bool main=i==0 || i==26;
        const bool root=main || border || (i!=6 && i!=12 && i!=14 && i!=16);
        std::array<uint32_t,4> push{};
        uint32_t count=0;
        if(root) {
            auto address=constants_address+(main ? 0 : 1024);
            push[count++]=uint32_t(address); push[count++]=uint32_t(address>>32);
        }
        push[count++]=i==26 ? 51 : 0;
        if(!border) push[count++]=i==26 ? 63 : 12;
        const auto groups=fsr411_dispatch(i,width,height,capacity_width,capacity_height);
        vkCmdBindPipeline(command_buffer,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[i]);
        vkCmdPushConstants(command_buffer,pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,count*4,push.data());
        if(timestamps)write_timestamp2(command_buffer,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,timestamps,timestamp_base+i*2);
        vkCmdDispatch(command_buffer,groups[0],groups[1],1);
        if(timestamps)write_timestamp2(command_buffer,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,timestamps,timestamp_base+i*2+1);
        if(i!=26)compute_barrier(pipeline_barrier2,command_buffer);
    }
}
}
