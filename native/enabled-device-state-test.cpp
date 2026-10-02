#include "../provider/enabled-device-state.hpp"
#include "../provider/vulkan_device_features.hpp"
#include <iostream>
#include <stdexcept>
void require(bool value){if(!value)throw std::runtime_error("enabled-device snapshot regression");}
int main() {
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    VkPhysicalDeviceFeatures core{};core.robustBufferAccess=VK_TRUE;info.pEnabledFeatures=&core;
    require(fsr4vk::read_enabled_device_state(info,VK_API_VERSION_1_3).robust_buffer_access);
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};info.pEnabledFeatures=nullptr;info.pNext=&f2;
    f2.features.robustBufferAccess=VK_TRUE;
    VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE mixed{};
    mixed.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MIXED_FLOAT_DOT_PRODUCT_FEATURES_VALVE;
    mixed.shaderMixedFloatDotProductFloat16AccFloat32=VK_TRUE;f2.pNext=&mixed;
    VkPhysicalDeviceShaderFloatControls2FeaturesKHR controls{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT_CONTROLS_2_FEATURES_KHR};
    controls.shaderFloatControls2=VK_TRUE;mixed.pNext=&controls;
    const char* extensions[]={"VK_VALVE_shader_mixed_float_dot_product","VK_KHR_shader_float_controls2"};
    info.enabledExtensionCount=2;info.ppEnabledExtensionNames=extensions;
    auto enabled=fsr4vk::read_enabled_device_state(info,VK_API_VERSION_1_3);
    require(enabled.robust_buffer_access && enabled.native_mixed_dot);
    controls.shaderFloatControls2=VK_FALSE;
    require(!fsr4vk::read_enabled_device_state(info,VK_API_VERSION_1_3).native_mixed_dot);
    controls.pNext=&controls;
    bool rejected=false;try{(void)fsr4vk::read_enabled_device_state(info,VK_API_VERSION_1_3);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected);
    std::cout << "logical-device enablement snapshot tests passed\n";
}
