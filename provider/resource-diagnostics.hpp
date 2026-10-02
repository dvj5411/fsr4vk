#pragma once
#include "../amd-fidelityfx-sdk/Kits/FidelityFX/api/include/ffx_api_types.h"
#include <cstdint>
#include <string>

namespace fsr4vk {
inline std::string surface_format(uint32_t value) {
    const char* name=nullptr;
#define FSR4_FORMAT(x) case FFX_API_SURFACE_FORMAT_##x: name="FFX_API_SURFACE_FORMAT_" #x; break
    switch(value) {
        FSR4_FORMAT(UNKNOWN);
        FSR4_FORMAT(R32G32B32A32_TYPELESS); FSR4_FORMAT(R32G32B32A32_UINT); FSR4_FORMAT(R32G32B32A32_FLOAT);
        FSR4_FORMAT(R16G16B16A16_FLOAT); FSR4_FORMAT(R32G32B32_FLOAT); FSR4_FORMAT(R32G32_FLOAT);
        FSR4_FORMAT(R8_UINT); FSR4_FORMAT(R32_UINT);
        FSR4_FORMAT(R8G8B8A8_TYPELESS); FSR4_FORMAT(R8G8B8A8_UNORM); FSR4_FORMAT(R8G8B8A8_SNORM); FSR4_FORMAT(R8G8B8A8_SRGB);
        FSR4_FORMAT(B8G8R8A8_TYPELESS); FSR4_FORMAT(B8G8R8A8_UNORM); FSR4_FORMAT(B8G8R8A8_SRGB);
        FSR4_FORMAT(R11G11B10_FLOAT); FSR4_FORMAT(R10G10B10A2_UNORM);
        FSR4_FORMAT(R16G16_FLOAT); FSR4_FORMAT(R16G16_UINT); FSR4_FORMAT(R16G16_SINT);
        FSR4_FORMAT(R16_FLOAT); FSR4_FORMAT(R16_UINT); FSR4_FORMAT(R16_UNORM); FSR4_FORMAT(R16_SNORM);
        FSR4_FORMAT(R8_UNORM); FSR4_FORMAT(R8G8_UNORM); FSR4_FORMAT(R8G8_UINT);
        FSR4_FORMAT(R32_FLOAT); FSR4_FORMAT(R9G9B9E5_SHAREDEXP);
        FSR4_FORMAT(R16G16B16A16_TYPELESS); FSR4_FORMAT(R32G32_TYPELESS); FSR4_FORMAT(R10G10B10A2_TYPELESS);
        FSR4_FORMAT(R16G16_TYPELESS); FSR4_FORMAT(R16_TYPELESS); FSR4_FORMAT(R8_TYPELESS);
        FSR4_FORMAT(R8G8_TYPELESS); FSR4_FORMAT(R32_TYPELESS);
    }
#undef FSR4_FORMAT
    return std::string(name ? name : "UNRECOGNIZED_FFX_API_SURFACE_FORMAT")+"("+std::to_string(value)+")";
}

inline std::string resource_snapshot(const char* name,const FfxApiResource& r) {
    return std::string(name)+": present="+(r.resource ? "yes" : "no")+
        " type="+std::to_string(r.description.type)+" format="+surface_format(r.description.format)+
        " size="+std::to_string(r.description.width)+"x"+std::to_string(r.description.height)+
        " depth="+std::to_string(r.description.depth)+" mips="+std::to_string(r.description.mipCount)+
        " usage="+std::to_string(r.description.usage)+" flags="+std::to_string(r.description.flags)+
        " state="+std::to_string(r.state);
}

// Only called on rejection: no string construction on the successful frame path.
inline std::string resource_problem(const char* name, const FfxApiResource& r,
    uint32_t width, uint32_t height, uint32_t format, uint32_t state,
    uint32_t depth_usage, const char* accepted) {
    constexpr uint32_t mask=FFX_API_RESOURCE_USAGE_DEPTHTARGET | FFX_API_RESOURCE_USAGE_STENCILTARGET;
    if(r.resource && r.description.type==FFX_API_RESOURCE_TYPE_TEXTURE2D &&
       (r.description.usage & mask)==depth_usage && r.description.width==width &&
       r.description.height==height && r.description.depth<=1 && r.description.format==format && r.state==state) return {};
    std::string reason=std::string(r.resource ? "invalid " : "missing ")+name+" resource: ";
    if(!r.resource) reason+="resource handle is null; ";
    if(r.description.type!=FFX_API_RESOURCE_TYPE_TEXTURE2D)
        reason+="type="+std::to_string(r.description.type)+" expected=TEXTURE2D("+
            std::to_string(FFX_API_RESOURCE_TYPE_TEXTURE2D)+"); ";
    if(r.description.width!=width || r.description.height!=height)
        reason+="dimensions="+std::to_string(r.description.width)+"x"+std::to_string(r.description.height)+
            " expected="+std::to_string(width)+"x"+std::to_string(height)+"; ";
    if(r.description.depth>1)
        reason+="depth/layers="+std::to_string(r.description.depth)+" expected=single-layer base-level 2D resource; ";
    if(r.description.format!=format) reason+="incompatible format; ";
    reason+="current_format="+surface_format(r.description.format)+"; accepted_formats=["+accepted+"]; ";
    if((r.description.usage & mask)!=depth_usage)
        reason+="depth/stencil usage bits="+std::to_string(r.description.usage & mask)+
            " expected="+std::to_string(depth_usage)+
            " (DEPTHTARGET=4, STENCILTARGET=32; depth accepts none, depth-only, or both; stencil-only is invalid); ";
    if(r.state!=state)
        reason+="state="+std::to_string(r.state)+" expected="+
            (state==FFX_API_RESOURCE_STATE_COMPUTE_READ ? std::string("COMPUTE_READ") : std::string("UNORDERED_ACCESS"))+
            "("+std::to_string(state)+"); ";
    reason+="formats above are host-supplied FFX metadata, not a queried VkImage format; host must supply compatible resources (convert if necessary; do not just relabel the format)";
    return reason;
}
}
