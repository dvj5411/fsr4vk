// CPU-only test of the actual provider query and the proposed OptiScaler reader.
// The reader include directory must point to the patched OptiScaler ffx folder.
#include "../provider/ffx_vk_provider.cpp"
#include <FFXVkPresetReporting.h>

#include <cassert>
#include <cstddef>
#include <iostream>
#include <type_traits>
#include <thread>
#include <limits>

// This CPU test creates no GPU resources. With dead stripping, only teardown
// symbols remain reachable through ProviderContext's empty core map. Fail hard
// if the test ever starts using Vulkan; no loader/driver is needed on the host.
#define FORBID_DESTROY(Name, Handle) \
    VKAPI_ATTR void VKAPI_CALL Name(VkDevice, Handle, const VkAllocationCallbacks*) { std::abort(); }
FORBID_DESTROY(vkDestroyBuffer, VkBuffer)
FORBID_DESTROY(vkDestroyDescriptorSetLayout, VkDescriptorSetLayout)
FORBID_DESTROY(vkDestroyEvent, VkEvent)
FORBID_DESTROY(vkDestroyImage, VkImage)
FORBID_DESTROY(vkDestroyImageView, VkImageView)
FORBID_DESTROY(vkDestroyPipeline, VkPipeline)
FORBID_DESTROY(vkDestroyPipelineCache, VkPipelineCache)
FORBID_DESTROY(vkDestroyPipelineLayout, VkPipelineLayout)
FORBID_DESTROY(vkDestroyQueryPool, VkQueryPool)
FORBID_DESTROY(vkDestroySampler, VkSampler)
FORBID_DESTROY(vkDestroyShaderModule, VkShaderModule)
FORBID_DESTROY(vkFreeMemory, VkDeviceMemory)
#undef FORBID_DESTROY
VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice, VkDeviceMemory) { std::abort(); }
VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(
    VkDevice, VkQueryPool, uint32_t, uint32_t, size_t, void*, VkDeviceSize, VkQueryResultFlags) {
    std::abort();
}

namespace {
const char* missingCommand=nullptr;
std::wstring lastMessage;
void messageCallback(uint32_t,const wchar_t* text){lastMessage=text;}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL mockDeviceProc(VkDevice,const char* name) {
    if(missingCommand && std::strcmp(name,missingCommand)==0)return nullptr;
    return reinterpret_cast<PFN_vkVoidFunction>(&mockDeviceProc);
}
unsigned query_calls = 0;
ffxReturnCode_t mock_result = FFX_API_RETURN_OK;
uint32_t mock_preset = FSR4VK_PRESET_UNKNOWN;
uint32_t mock_mask = 0;
ffxReturnCode_t mockCapabilities(ffxContext*, ffxQueryDescHeader* header) {
    assert(header->type == FSR4VK_QUERY_DESC_TYPE_PRESET_CAPABILITIES);
    auto* desc = reinterpret_cast<Fsr4VkQueryPresetCapabilities*>(header);
    assert(desc->forcedPresetMask == 0);
    desc->forcedPresetMask = mock_mask;
    return mock_result;
}
ffxReturnCode_t mockQuery(ffxContext*, ffxQueryDescHeader* header) {
    ++query_calls;
    assert(header->type == FSR4VK_QUERY_DESC_TYPE_ACTIVE_PRESET);
    assert(header->pNext == nullptr);
    auto* desc = reinterpret_cast<Fsr4VkQueryActivePreset*>(header);
    assert(desc->activePreset == FSR4VK_PRESET_UNKNOWN);
    desc->activePreset = mock_preset;
    return mock_result;
}
}

int main() {
    // Regression for the Endfield flags in the Windows field log: inverted
    // depth + HDR + external exposure must create without allocating GPU work.
    const auto test_assets=std::filesystem::current_path()/"build/preset-reporting-test/context-assets";
    std::filesystem::create_directories(test_assets/"general");
    setenv("FSR4_VK_ASSET_ROOT",test_assets.c_str(),1);
    CreateBackendVkDesc backend{{3,nullptr},reinterpret_cast<VkDevice>(1),
        reinterpret_cast<VkPhysicalDevice>(2),mockDeviceProc};
    ffxCreateContextDescUpscale create{};
    create.header={FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE,&backend.header};
    create.flags=FFX_UPSCALE_ENABLE_DEPTH_INVERTED|FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
    create.maxRenderSize={1704,958};create.maxUpscaleSize={2560,1440};
    ffxContext external{};
    assert(ffxCreateContext(&external,&create.header,nullptr)==FFX_API_RETURN_OK);
    assert((static_cast<ProviderContext*>(external)->flags & FFX_UPSCALE_ENABLE_AUTO_EXPOSURE)==0);
    assert(ffxDestroyContext(&external,nullptr)==FFX_API_RETURN_OK);
    create.flags|=FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
    assert(ffxCreateContext(&external,&create.header,nullptr)==FFX_API_RETURN_OK);
    assert((static_cast<ProviderContext*>(external)->flags & FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE)!=0);
    assert(ffxDestroyContext(&external,nullptr)==FFX_API_RETURN_OK);
    create.flags&=~FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
    create.header.pNext=&create.header;
    assert(ffxCreateContext(&external,&create.header,nullptr)==FFX_API_RETURN_ERROR_PARAMETER);
    assert(external==nullptr);
    create.header.pNext=&backend.header;
    ffxAllocationCallbacks noMemory{nullptr,[](void*,uint64_t)->void*{return nullptr;},[](void*,void*){std::abort();}};
    assert(ffxCreateContext(&external,&create.header,&noMemory)==FFX_API_RETURN_ERROR_MEMORY);
    ffxAllocationCallbacks throwing{nullptr,[](void*,uint64_t)->void*{throw std::bad_alloc();},[](void*,void*){std::abort();}};
    assert(ffxCreateContext(&external,&create.header,&throwing)==FFX_API_RETURN_ERROR_MEMORY);
    assert(external==nullptr);
    Fsr4VkCreateDeviceFeatures features{{FSR4VK_CREATE_DESC_TYPE_DEVICE_FEATURES,backend.header.pNext},FSR4VK_DEVICE_ROBUST_BUFFER_ACCESS,VK_API_VERSION_1_3};
    backend.header.pNext=&features.header;
    assert(ffxCreateContext(&external,&create.header,nullptr)==FFX_API_RETURN_OK);
    assert(static_cast<ProviderContext*>(external)->robust_buffer_access==true);
    assert(static_cast<ProviderContext*>(external)->native_mixed_dot_enabled==false);
    assert(static_cast<ProviderContext*>(external)->apiVersion==VK_API_VERSION_1_3);
    assert(ffxDestroyContext(&external,nullptr)==FFX_API_RETURN_OK);
    backend.header.pNext=features.header.pNext;
    create.fpMessage=messageCallback;
    missingCommand="vkCmdBindDescriptorBuffersEXT";
    assert(ffxCreateContext(&external,&create.header,nullptr)==FFX_API_RETURN_ERROR_RUNTIME_ERROR);
    assert(external==nullptr);
    assert(lastMessage.find(L"FSR4VK_ERROR_MISSING_DEVICE_FEATURES")!=std::wstring::npos);
    assert(lastMessage.find(L"vkCmdBindDescriptorBuffersEXT")!=std::wstring::npos);
    missingCommand=nullptr;
    try {fsr4core::check(VK_ERROR_FEATURE_NOT_PRESENT,"test pipeline");assert(false);}
    catch(const std::runtime_error& e){assert(std::string(e.what()).find("FSR4VK_ERROR_MISSING_DEVICE_FEATURES")!=std::string::npos);}
    try {fsr4core::check(VK_ERROR_OUT_OF_DEVICE_MEMORY,"test allocation");assert(false);}
    catch(const std::runtime_error& e){assert(std::string(e.what()).find("FSR4VK_ERROR_MISSING_DEVICE_FEATURES")==std::string::npos);}
    create.flags|=FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
    assert(ffxCreateContext(&external,&create.header,nullptr)==FFX_API_RETURN_OK);
    assert(ffxDestroyContext(&external,nullptr)==FFX_API_RETURN_OK);
    create.flags|=FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
    assert(ffxCreateContext(&external,&create.header,nullptr)==FFX_API_RETURN_ERROR_RUNTIME_ERROR);
    assert(external==nullptr);
    static_assert(std::is_standard_layout_v<Fsr4VkQueryActivePreset>);
    static_assert(offsetof(Fsr4VkQueryActivePreset, header) == 0);
    static_assert(offsetof(Fsr4VkQueryActivePreset, activePreset) == sizeof(ffxQueryDescHeader));
    static_assert(sizeof(void*) != 8 || sizeof(Fsr4VkQueryActivePreset) == 24);

    ProviderContext provider;
    ffxContext context = &provider;
    ffxContext empty = nullptr;
    int32_t phase=99;
    ffxQueryDescUpscaleGetJitterPhaseCount phaseQuery{{FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT,nullptr},1280,1920,&phase};
    assert(ffxQuery(nullptr,&phaseQuery.header)==FFX_API_RETURN_OK && phase==18);
    phaseQuery.renderWidth=1;phaseQuery.displayWidth=UINT32_MAX;
    assert(ffxQuery(nullptr,&phaseQuery.header)==FFX_API_RETURN_ERROR_PARAMETER && phase==18);
    phaseQuery.displayWidth=0;
    assert(ffxQuery(nullptr,&phaseQuery.header)==FFX_API_RETURN_ERROR_PARAMETER);
    uint32_t width{},height{};
    ffxQueryDescUpscaleGetRenderResolutionFromQualityMode resolutionQuery{{FFX_API_QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE,nullptr},UINT32_MAX,1080,0,&width,&height};
    assert(ffxQuery(nullptr,&resolutionQuery.header)==FFX_API_RETURN_OK && width==UINT32_MAX && height==1080);
    for(uint32_t mode=0;mode<5;++mode) for(uint32_t display=1;display<=3840;++display) {
        resolutionQuery.qualityMode=mode;resolutionQuery.displayWidth=display;
        assert(ffxQuery(nullptr,&resolutionQuery.header)==FFX_API_RETURN_OK);
        assert(width==uint32_t(std::ceil(float(display)/upscaleRatio(mode))));
    }
    ffxConfigureDescUpscaleKeyValue unsupported{{FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE,nullptr},UINT64_MAX,0,nullptr};
    assert(ffxConfigure(&context,&unsupported.header)==FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE);
    provider.message=messageCallback;
    ffxDispatchDescUpscale invalid{};
    invalid.header.type=FFX_API_DISPATCH_DESC_TYPE_UPSCALE;invalid.commandList=reinterpret_cast<void*>(1);
    invalid.jitterOffset.x=std::numeric_limits<float>::quiet_NaN();
    assert(std::string(dispatchScalarProblem(invalid))=="jitterOffset must contain finite values");
    invalid.jitterOffset.x=0;invalid.motionVectorScale.y=std::numeric_limits<float>::infinity();
    assert(std::string(dispatchScalarProblem(invalid))=="motionVectorScale must contain finite values");
    Fsr4VkQueryActivePreset desc{{FSR4VK_QUERY_DESC_TYPE_ACTIVE_PRESET, nullptr}, 42};
    assert(ffxQuery(&context, nullptr) == FFX_API_RETURN_ERROR_PARAMETER);
    assert(ffxQuery(nullptr, &desc.header) == FFX_API_RETURN_ERROR_PARAMETER);
    assert(desc.activePreset == FSR4VK_PRESET_UNKNOWN);
    assert(ffxQuery(&empty, &desc.header) == FFX_API_RETURN_ERROR_PARAMETER);
    assert(ffxQuery(&context, &desc.header) == FFX_API_RETURN_OK);
    assert(desc.activePreset == FSR4VK_PRESET_UNKNOWN);

    bool supported = true;
    assert(!FFXVkPresetReporting::Read(ffxQuery, &context, supported));
    for (const auto& [name, id] : {
            std::pair{"quality", 1u}, {"balanced", 2u}, {"performance", 3u},
            {"ultraperf", 5u}, {"drs", 4u}, {"native", 0u}, {"quality", 1u}}) {
        provider.active_preset = name;
        assert(ffxQuery(&context, &desc.header) == FFX_API_RETURN_OK);
        assert(desc.activePreset == id);
        assert(FFXVkPresetReporting::Read(ffxQuery, &context, supported) == id);
        assert(supported);
    }
    for (const auto* name : {"", "unexpected-model"}) {
        provider.active_preset = name;
        assert(ffxQuery(&context, &desc.header) == FFX_API_RETURN_OK);
        assert(desc.activePreset == FSR4VK_PRESET_UNKNOWN);
        assert(!FFXVkPresetReporting::Read(ffxQuery, &context, supported));
    }
    // Existing version query and unknown descriptors retain their behavior.
    ffxQueryGetProviderVersion version{};
    version.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
    assert(ffxQuery(&context, &version.header) == FFX_API_RETURN_OK);
    assert(version.versionId == kVersionId);
    const auto mask = FFXVkPresetReporting::SupportedPresets(ffxQuery, &context);
    assert(mask == 0x3fu);
    assert(!FFXVkPresetReporting::SupportedPresets(mockCapabilities, &context));
    mock_mask = 0x3f;
    assert(FFXVkPresetReporting::SupportedPresets(mockCapabilities, &context) == 0x3fu);
    mock_result = FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE;
    assert(!FFXVkPresetReporting::SupportedPresets(mockCapabilities, &context));
    mock_result = FFX_API_RETURN_OK;
    mock_mask = 1u << 31;
    assert(!FFXVkPresetReporting::SupportedPresets(mockCapabilities, &context));
    assert(!FFXVkPresetReporting::SupportedPresets(nullptr, &context));
    assert(!FFXVkPresetReporting::SupportedPresets(ffxQuery, &empty));
    Fsr4VkQueryPresetCapabilities caps{{FSR4VK_QUERY_DESC_TYPE_PRESET_CAPABILITIES, nullptr}, 42};
    assert(ffxQuery(nullptr, &caps.header) == FFX_API_RETURN_ERROR_PARAMETER);
    assert(caps.forcedPresetMask == 0);
    assert(provider.forced_preset == FSR4VK_PRESET_AUTO);
    assert(std::string(selectedPreset(provider, 1280, 1920)) == "quality");
    ProviderContext other;
    provider.active_preset = "balanced";
    for (const auto id : {0u, 1u, 2u, 3u, 4u, 5u}) {
        assert(FFXVkPresetReporting::Apply(ffxConfigure, &context, *mask, id) == FFX_API_RETURN_OK);
        assert(provider.forced_preset == id);
        // The forced model wins across scale ratios, including FSRAA and odd sizes.
        for (const auto rw : {640u, 960u, 1129u, 1280u, 1919u, 1920u})
            assert(std::string(selectedPreset(provider, rw, 1920)) == presetName(id));
        assert(FFXVkPresetReporting::Read(ffxQuery, &context, supported) == 2);
        assert(other.forced_preset == FSR4VK_PRESET_AUTO);
    }
    Fsr4VkConfigurePreset configure{{FSR4VK_CONFIGURE_DESC_TYPE_PRESET, nullptr}, 6};
    for (const auto id : {6u, 32u, UINT32_MAX - 1}) {
        configure.preset = id;
        assert(ffxConfigure(&context, &configure.header) == FFX_API_RETURN_ERROR_PARAMETER);
        assert(provider.forced_preset == 5); // Rejected changes are atomic.
        assert(!FFXVkPresetReporting::CanForce(*mask, id));
        assert(FFXVkPresetReporting::Apply(ffxConfigure, &context, *mask, id) == FFX_API_RETURN_ERROR_PARAMETER);
    }
    assert(ffxConfigure(nullptr, &configure.header) == FFX_API_RETURN_ERROR_PARAMETER);
    assert(ffxConfigure(&empty, &configure.header) == FFX_API_RETURN_ERROR_PARAMETER);
    assert(ffxConfigure(&context, nullptr) == FFX_API_RETURN_ERROR_PARAMETER);
    assert(FFXVkPresetReporting::Apply(nullptr, &context, *mask, 1) == FFX_API_RETURN_ERROR_PARAMETER);
    assert(FFXVkPresetReporting::Apply(ffxConfigure, &context, *mask, FSR4VK_PRESET_AUTO) == FFX_API_RETURN_OK);
    assert(provider.forced_preset == FSR4VK_PRESET_AUTO);
    assert(std::string(selectedPreset(provider, 1280, 1920)) == "quality");
    assert(std::string(selectedPreset(provider, 640, 1920)) == "ultraperf");
    assert(std::string(selectedPreset(provider, 1920, 1920)) == "native");
    provider.flags |= FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION;
    assert(std::string(selectedPreset(provider, 1280, 1920)) == "drs");
    assert(FFXVkPresetReporting::Apply(ffxConfigure, &context, *mask, 1) == FFX_API_RETURN_OK);
    assert(std::string(selectedPreset(provider, 1280, 1920)) == "quality");
    assert(FFXVkPresetReporting::Apply(ffxConfigure, &context, *mask, FSR4VK_PRESET_AUTO) == FFX_API_RETURN_OK);
    assert(std::string(selectedPreset(provider, 1280, 1920)) == "drs");
    ffxQueryDescHeader unknown{0xdeadbeef, nullptr};
    assert(ffxQuery(&context, &unknown) == FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE);

    assert(!FFXVkPresetReporting::Read(nullptr, &context, supported));
    assert(!FFXVkPresetReporting::Read(mockQuery, nullptr, supported));
    assert(!FFXVkPresetReporting::Read(mockQuery, &empty, supported));
    assert(query_calls == 0);
    for (const auto id : {0u, 1u, 2u, 3u, 4u, 5u}) {
        mock_preset = id;
        assert(FFXVkPresetReporting::Read(mockQuery, &context, supported) == id);
    }
    for (const auto id : {6u, 100u, FSR4VK_PRESET_UNKNOWN}) {
        mock_preset = id;
        assert(!FFXVkPresetReporting::Read(mockQuery, &context, supported));
    }
    mock_preset = 2; // Even plausible output is ignored when the query fails.
    mock_result = FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    assert(!FFXVkPresetReporting::Read(mockQuery, &context, supported));
    assert(supported);
    mock_result = FFX_API_RETURN_OK;
    assert(FFXVkPresetReporting::Read(mockQuery, &context, supported) == 2);
    for (const auto result : {FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE,
                             FFX_API_RETURN_PROVIDER_NO_SUPPORT_NEW_DESCTYPE}) {
        supported = true;
        mock_result = result;
        assert(!FFXVkPresetReporting::Read(mockQuery, &context, supported));
        assert(!supported);
        const auto calls = query_calls;
        assert(!FFXVkPresetReporting::Read(mockQuery, &context, supported));
        assert(query_calls == calls);
    }
    std::optional<uint32_t> displayed = 1;
    {
        FFXVkPresetReporting::DispatchReport report{displayed};
        assert(displayed == 1); // Keep the label stable during dispatch.
        report.activePreset = 2;
    }
    assert(displayed == 2);
    {
        FFXVkPresetReporting::DispatchReport report{displayed};
        // Simulate any early return: clear stale data, including failed dispatch.
    }
    assert(!displayed);
    std::thread configureThread([&]{for(unsigned i=0;i<1000;++i){Fsr4VkConfigurePreset value{{FSR4VK_CONFIGURE_DESC_TYPE_PRESET,nullptr},i%6};assert(ffxConfigure(&context,&value.header)==FFX_API_RETURN_OK);}});
    std::thread queryThread([&]{for(unsigned i=0;i<1000;++i){Fsr4VkQueryActivePreset value{{FSR4VK_QUERY_DESC_TYPE_ACTIVE_PRESET,nullptr},FSR4VK_PRESET_UNKNOWN};assert(ffxQuery(&context,&value.header)==FFX_API_RETURN_OK);}});
    configureThread.join();queryThread.join();
    std::cout << "Preset reporting: provider query and reader tests passed\n";
}
