#pragma once
// Isolated research builds only. Captures frame zero; changes synchronization.
#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fsr4capture {
inline VkDevice device{};
inline VkBuffer readback{}, scratch{};
inline VkDeviceMemory memory{};
inline void* mapped{};
inline VkDeviceSize scratch_size{}, image_bytes{}, total_size{};
inline VkImage exposure{}, reprojected{};
inline uint32_t width{}, height{};
inline unsigned pass_count=15;
inline bool active=false, finished=false, directory_ready=false;
inline std::filesystem::path directory;
inline void check(VkResult r) { if(r!=VK_SUCCESS) throw std::runtime_error("pass capture Vulkan error "+std::to_string(r)); }
inline bool enabled() { const auto* p=std::getenv("FSR4_CAPTURE_DIR");return p && *p; }
inline void initialize_directory() {
    if(directory_ready || !enabled()) return;
    directory=std::getenv("FSR4_CAPTURE_DIR");
    if(std::filesystem::exists(directory)) throw std::runtime_error("capture directory already exists");
    std::filesystem::create_directories(directory); directory_ready=true;
}
inline void blob(const std::string& name,const void* bytes,size_t size) {
    if(!enabled() || finished) return;
    initialize_directory();
    std::ofstream out(directory/name,std::ios::binary);
    out.write(static_cast<const char*>(bytes),size);
    if(!out) throw std::runtime_error("pass capture write failed");
}
inline void release() {
    if(mapped) vkUnmapMemory(device,memory);
    if(readback) vkDestroyBuffer(device,readback,nullptr);
    if(memory) vkFreeMemory(device,memory,nullptr);
    readback={};memory={};mapped=nullptr;active=false;
}
inline void begin(VkPhysicalDevice physical,VkDevice logical,VkBuffer source,VkDeviceSize size,
                  VkImage exp,VkImage reproj,uint32_t w,uint32_t h,const void* constants,unsigned passes=15) {
    if(!enabled() || finished || active) return;
    initialize_directory();
    if(size>24*1024*1024) throw std::runtime_error("frame capture restricted to small tier");
    device=logical;scratch=source;scratch_size=size;exposure=exp;reprojected=reproj;width=w;height=h;
    pass_count=passes;
    image_bytes=VkDeviceSize(w)*h*8;total_size=size*(pass_count+1)+8+image_bytes;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=total_size;
    bi.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device,&bi,nullptr,&readback));
    VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,readback,&req);
    VkPhysicalDeviceMemoryProperties mp{};vkGetPhysicalDeviceMemoryProperties(physical,&mp);
    const auto needed=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t type=0;while(type<mp.memoryTypeCount &&
        (!(req.memoryTypeBits&(1u<<type)) || (mp.memoryTypes[type].propertyFlags&needed)!=needed)) ++type;
    if(type==mp.memoryTypeCount) throw std::runtime_error("no capture memory");
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=type;
    check(vkAllocateMemory(device,&ai,nullptr,&memory));check(vkBindBufferMemory(device,readback,memory,0));
    check(vkMapMemory(device,memory,0,total_size,0,&mapped));active=true;
    blob("constants.bin",constants,2048);
}
inline void after_pass(VkCommandBuffer cmd,int pass) {
    if(!active) return;
    if(pass < -1 || pass>=int(pass_count)) throw std::runtime_error("bad capture pass");
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    VkBufferCopy copy{0,scratch_size*VkDeviceSize(pass+1),scratch_size};
    vkCmdCopyBuffer(cmd,scratch,readback,1,&copy);
    if(pass==0 || pass==1) {
        VkBufferImageCopy image{};image.bufferOffset=scratch_size*(pass_count+1)+(pass==1 ? 8 : 0);
        image.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        image.imageExtent=pass==0 ? VkExtent3D{2,1,1} : VkExtent3D{width,height,1};
        vkCmdCopyImageToBuffer(cmd,pass==0 ? exposure : reprojected,VK_IMAGE_LAYOUT_GENERAL,readback,1,&image);
    }
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
}
inline void flush() {
    if(!active) return;
    const auto* bytes=static_cast<const uint8_t*>(mapped);
    for(int pass=-1;pass<int(pass_count);++pass)
        blob(pass<0 ? "cleared.scratch" : "pass-"+std::to_string(pass)+".scratch",
             bytes+scratch_size*(pass+1),scratch_size);
    blob("exposure.bin",bytes+scratch_size*(pass_count+1),8);
    blob("pre-reprojected.rgba16f",bytes+scratch_size*(pass_count+1)+8,image_bytes);
    finished=true;release();
}
}
