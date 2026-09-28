#include "../provider/resource-diagnostics.hpp"
#include "../provider/device-feature-diagnostics.hpp"
#include "../provider/input-formats.hpp"
#include "vulkan-compat.hpp"
#include "vulkan-result-diagnostics.hpp"
#include <iostream>
#include <stdexcept>

void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
void contains(const std::string& text,const char* part) { require(text.find(part)!=std::string::npos,part); }
PFN_vkVoidFunction VKAPI_CALL missing_command(VkDevice,const char*) { return nullptr; }
int main() {
    require(fsr4vk::depth_view_format(0)==VK_FORMAT_R32_SFLOAT,"R32 depth mapping");
    require(fsr4vk::depth_view_format(6)==VK_FORMAT_D32_SFLOAT,"depth-only mapping");
    require(fsr4vk::depth_view_format(38)==VK_FORMAT_D32_SFLOAT_S8_UINT,"depth-stencil mapping");
    bool invalid_depth=false;
    try { (void)fsr4vk::depth_view_format(32); } catch(const std::invalid_argument&) { invalid_depth=true; }
    require(invalid_depth,"stencil-only accepted");
    require(fsr4vk::motion_format_override(nullptr)==VK_FORMAT_UNDEFINED,"override must default off");
    require(fsr4vk::motion_format_override("")==VK_FORMAT_UNDEFINED,"empty override must be off");
    require(fsr4vk::motion_format_override("R16G16_SNORM")==VK_FORMAT_R16G16_SNORM,"RG16 SNORM override");
    bool invalid_override=false;
    try { (void)fsr4vk::motion_format_override("AUTO"); } catch(const std::invalid_argument&) { invalid_override=true; }
    require(invalid_override,"unknown override accepted");
    contains(fsr4vk::result_description(VK_ERROR_DEVICE_LOST),"VK_ERROR_DEVICE_LOST(-4)");
    contains(fsr4vk::result_description(VK_ERROR_OUT_OF_DEVICE_MEMORY),"VK_ERROR_OUT_OF_DEVICE_MEMORY(-2)");
    contains(fsr4vk::result_description(static_cast<VkResult>(-9999)),"UNKNOWN_VK_RESULT(-9999)");
    FfxApiResource r{};
    r.resource=reinterpret_cast<void*>(1);
    r.description.type=FFX_API_RESOURCE_TYPE_TEXTURE2D;
    r.description.width=1280; r.description.height=720;
    r.description.format=FFX_API_SURFACE_FORMAT_R32_FLOAT;
    r.state=FFX_API_RESOURCE_STATE_COMPUTE_READ;
    auto problem=[&](const FfxApiResource& value) {
        return fsr4vk::resource_problem("depth",value,1280,720,FFX_API_SURFACE_FORMAT_R32_FLOAT,
            FFX_API_RESOURCE_STATE_COMPUTE_READ,0,"R32_FLOAT");
    };
    require(problem(r).empty(),"valid depth rejected");
    auto bad=r; bad.resource=nullptr;
    contains(problem(bad),"missing depth resource"); contains(problem(bad),"handle is null");
    bad=r; bad.description.format=FFX_API_SURFACE_FORMAT_R16_UNORM;
    contains(problem(bad),"incompatible format"); contains(problem(bad),"current_format=FFX_API_SURFACE_FORMAT_R16_UNORM(23)");
    contains(problem(bad),"accepted_formats=[R32_FLOAT]");
    bad=r; bad.description.format=9999; contains(problem(bad),"UNRECOGNIZED_FFX_API_SURFACE_FORMAT(9999)");
    bad=r; bad.description.width=640; contains(problem(bad),"dimensions=640x720 expected=1280x720");
    bad=r; bad.description.type=FFX_API_RESOURCE_TYPE_BUFFER; contains(problem(bad),"expected=TEXTURE2D");
    bad=r; bad.state=FFX_API_RESOURCE_STATE_COPY_DEST; contains(problem(bad),"expected=COMPUTE_READ");
    bad=r; bad.description.usage=FFX_API_RESOURCE_USAGE_DEPTHTARGET; contains(problem(bad),"depth/stencil usage bits=4 expected=0");
    bad=r; bad.description.format=FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    auto motion=fsr4vk::resource_problem("motionVectors",bad,1280,720,FFX_API_SURFACE_FORMAT_R16G16_FLOAT,
        FFX_API_RESOURCE_STATE_COMPUTE_READ,0,"R16G16_FLOAT (VK_FORMAT_R16G16_SFLOAT)");
    contains(motion,"invalid motionVectors resource"); contains(motion,"R8G8B8A8_UNORM"); contains(motion,"VK_FORMAT_R16G16_SFLOAT");
    for(uint32_t fmt=0;fmt<=FFX_API_SURFACE_FORMAT_R32_TYPELESS;++fmt)
        require(fsr4vk::surface_format(fmt).find("UNRECOGNIZED")==std::string::npos,"unnamed SDK format");

    VkDeviceCreateInfo source{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    VkDeviceCreateInfo prepared=source;
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    v12.shaderFloat16=VK_TRUE; v12.bufferDeviceAddress=VK_TRUE;
    VkPhysicalDeviceShaderFloat16Int8Features floats{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
    floats.shaderFloat16=VK_TRUE; source.pNext=&floats; prepared.pNext=&v12;
    const char* ext="VK_KHR_buffer_device_address";
    prepared.enabledExtensionCount=1; prepared.ppEnabledExtensionNames=&ext;
    auto delta=fsr4vk::device_enablement_diagnostic(source,prepared);
    contains(delta,"bufferDeviceAddress=VK_TRUE"); contains(delta,ext);
    require(delta.find("shaderFloat16")==std::string::npos,"promoted alias incorrectly marked disabled");
    contains(fsr4vk::device_enablement_diagnostic(prepared,prepared),"features not enabled in host request=[none]");
    contains(fsr4vk::device_contract_diagnostic(prepared),"NOT a list of observed disabled bits");
    for(const auto& command : {"vkGetDescriptorEXT","vkGetBufferDeviceAddressKHR","vkCmdPipelineBarrier2KHR"}) {
        bool rejected=false;
        try { (void)fsr4vk::load_device_function<PFN_vkVoidFunction>(reinterpret_cast<VkDevice>(1),missing_command,{command}); }
        catch(const std::runtime_error& error) {
            rejected=true; contains(error.what(),command); contains(error.what(),"required feature=");
            contains(error.what(),"vkCreateDevice");
        }
        require(rejected,"missing command accepted");
    }
    std::cout << "Resource and feature diagnostics passed\n";
}
