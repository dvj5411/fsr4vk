#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>

namespace fsr4vk {
// Whole-set padding is allocation space, never an array-element stride.
struct DescriptorLayout {
    VkPhysicalDeviceDescriptorBufferPropertiesEXT properties{};
    std::array<VkDeviceSize,3> sizes{}, offsets{};
    VkDeviceSize mutable_stride=0;
    std::size_t storage_buffer_size=0;

    static VkDeviceSize mutable_size(const VkPhysicalDeviceDescriptorBufferPropertiesEXT& p,bool robust) {
        return std::max({robust ? p.robustUniformBufferDescriptorSize : p.uniformBufferDescriptorSize,
                         robust ? p.robustStorageBufferDescriptorSize : p.storageBufferDescriptorSize,
                         p.sampledImageDescriptorSize,p.storageImageDescriptorSize,
                         robust ? p.robustUniformTexelBufferDescriptorSize : p.uniformTexelBufferDescriptorSize,
                         robust ? p.robustStorageTexelBufferDescriptorSize : p.storageTexelBufferDescriptorSize});
    }
    DescriptorLayout()=default;
    DescriptorLayout(const VkPhysicalDeviceDescriptorBufferPropertiesEXT& p,
                     std::array<VkDeviceSize,3> layout_sizes,std::array<VkDeviceSize,3> binding_offsets,
                     std::optional<bool> robust=std::nullopt) : properties(p),sizes(layout_sizes),offsets(binding_offsets) {
        if(!robust && (mutable_size(p,false)!=mutable_size(p,true) ||
                      p.storageBufferDescriptorSize!=p.robustStorageBufferDescriptorSize))
            throw std::runtime_error("FSR4VK_ERROR_UNKNOWN_DEVICE_FEATURES: robustBufferAccess changes descriptor sizes; host must supply the successful VkDeviceCreateInfo feature snapshot (do not infer array strides from layout padding)");
        const bool enabled=robust.value_or(false);
        mutable_stride=mutable_size(p,enabled);
        storage_buffer_size=enabled ? p.robustStorageBufferDescriptorSize : p.storageBufferDescriptorSize;
        if(!mutable_stride || !storage_buffer_size || !p.samplerDescriptorSize ||
           !p.sampledImageDescriptorSize || !p.storageImageDescriptorSize)
            throw std::runtime_error("descriptor properties contain zero-sized required descriptors");
        for(std::size_t set=0;set<3;++set)
            (void)offset(set,127,set==0 ? p.samplerDescriptorSize : mutable_stride);
    }
    VkDeviceSize offset(std::size_t set,std::uint32_t index,std::size_t concrete_size) const {
        if(set>=sizes.size() || index>=128) throw std::out_of_range("descriptor set/index out of range");
        const VkDeviceSize stride=set==0 ? properties.samplerDescriptorSize : mutable_stride;
        if(!stride || concrete_size>stride || offsets[set]>sizes[set] ||
           index>(sizes[set]-offsets[set])/stride)
            throw std::runtime_error("descriptor binding exceeds queried layout size");
        const auto result=offsets[set]+VkDeviceSize(index)*stride;
        if(concrete_size>sizes[set]-result)
            throw std::runtime_error("descriptor write exceeds queried layout size");
        return result;
    }
};
}
