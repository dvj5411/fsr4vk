#!/usr/bin/env python3
"""Generate a Linux-testable adapter from the actual PR's feature implementation.

This is test instrumentation, not a replacement implementation or runtime shim.
Usage: script path/to/OptiScaler/spoofing output.hpp
"""
import re,sys
from pathlib import Path
source=Path(sys.argv[1]); output=Path(sys.argv[2])
text=(source/'Vulkan_Spoofing.cpp').read_text()
impl=text.split('struct VulkanDeviceFeatureState::Impl\n',1)[1].split('\nVulkanDeviceFeatureState::VulkanDeviceFeatureState(',1)[0]
extensions=re.search(r'ffxFeatureExtensions\[\]\s*=\s*\{(.*?)\};',text,re.S).group(1)
output.write_text('''#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstring>
#include <vulkan/vulkan.h>
#include "Vulkan_Fsr4_Compat.h"
static std::map<std::string,bool> vkDeviceExtensions;
struct VulkanDeviceFeatureState { struct Impl; };
struct VulkanDeviceFeatureState::Impl
'''+impl+'''
struct Pr1161DeviceFeatures {
    VkDeviceCreateInfo info;
    VulkanDeviceFeatureState::Impl state;
    std::vector<const char*> extensions;
    Pr1161DeviceFeatures(VkPhysicalDevice physical,const VkDeviceCreateInfo& original)
        :info(original),state(&info,vkGetPhysicalDeviceFeatures2) {
        uint32_t count=0;
        if(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,nullptr)!=VK_SUCCESS) std::abort();
        std::vector<VkExtensionProperties> available(count);
        if(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,available.data())!=VK_SUCCESS) std::abort();
        vkDeviceExtensions.clear();
        for(uint32_t i=0;i<count;++i) vkDeviceExtensions[available[i].extensionName]=true;
        const char* required[]={'''+extensions+'''
          VK_VALVE_SHADER_MIXED_FLOAT_DOT_PRODUCT_EXTENSION_NAME,
          VK_KHR_SHADER_FLOAT_CONTROLS_2_EXTENSION_NAME};
        for(auto e:required) if(vkDeviceExtensions.contains(e)) extensions.push_back(e);
        info.enabledExtensionCount=uint32_t(extensions.size());
        info.ppEnabledExtensionNames=extensions.data();
        state.EnableFeatures(physical);
    }
    const VkDeviceCreateInfo* get()const{return &info;}
};
''')
