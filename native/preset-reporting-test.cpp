// CPU-only test of the actual provider query and the proposed OptiScaler reader.
// The reader include directory must point to the patched OptiScaler ffx folder.
#include "../provider/ffx_vk_provider.cpp"
#include <FFXVkPresetReporting.h>

#include <cassert>
#include <cstddef>
#include <iostream>
#include <type_traits>

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
    static_assert(std::is_standard_layout_v<Fsr4VkQueryActivePreset>);
    static_assert(offsetof(Fsr4VkQueryActivePreset, header) == 0);
    static_assert(offsetof(Fsr4VkQueryActivePreset, activePreset) == sizeof(ffxQueryDescHeader));
    static_assert(sizeof(void*) != 8 || sizeof(Fsr4VkQueryActivePreset) == 24);

    ProviderContext provider;
    ffxContext context = &provider;
    ffxContext empty = nullptr;
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
    std::cout << "Preset reporting: provider query and reader tests passed\n";
}
