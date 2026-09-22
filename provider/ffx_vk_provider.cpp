#if !defined(_WIN32)
#define __declspec(x) __attribute__((visibility("default")))
#endif
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "../amd-fidelityfx-sdk/Kits/FidelityFX/upscalers/include/ffx_upscale.h"
#include <vulkan/vulkan.h>

#include "ffx_vk_device_requirements.h"
#include "ffx_vk_preset_query.h"
#include "vulkan_device_features.hpp"
#include "diagnostic-options.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <memory>
#include <map>
#include <atomic>
#include <chrono>
#include "../native/quality-context.hpp"

namespace {
constexpr ffxStructType_t kBackendVkDescType = 3u;
constexpr ffxStructType_t kFsr4VulkanApiVersionDescType =
    0x46535234564b4150ull;
struct CreateBackendVkDesc {
    ffxCreateContextDescHeader header;
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr;
};

struct Fsr4VulkanApiVersionDesc {
    ffxApiHeader header;
    std::uint32_t apiVersion;
};

constexpr std::uint64_t kVersionId =
    (0xF5A5CA1Eull << 32) | ((4ull << 22) | (0ull << 12) | 2ull);
constexpr std::uint64_t kFsr314CompatibilityVersionId =
    (0xF5A5CA1Eull << 32) | ((3ull << 22) | (1ull << 12) | 4ull);
constexpr std::uint64_t kVersion411Id =
    (0xF5A5CA1Eull << 32) | ((4ull << 22) | (1ull << 12) | 1ull);
// The installed VK INT8 selector prefixes these verbatim with "FSR".
// Keep the leading space here; version IDs, not labels, select the model.
constexpr const char* kVersionName = " 4.0.2 VK INT8";
constexpr const char* kVersion411Name = " 4.1.1 VK INT8";

struct ProviderContext {
    inline static std::atomic<std::uint64_t> next_id{0};
    const std::uint64_t diagnostic_id=++next_id;
    std::uint64_t version = kVersionId;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
    std::uint32_t apiVersion = VK_API_VERSION_1_1;
    std::uint32_t flags = 0;
    FfxApiDimensions2D maxRenderSize{};
    FfxApiDimensions2D maxUpscaleSize{};
    ffxApiMessage message = nullptr;
    std::filesystem::path assets;
    std::map<std::string,std::unique_ptr<fsr4core::QualityContext>> cores;
    std::string active_preset;
    std::uint32_t forced_preset = FSR4VK_PRESET_AUTO;
    fsr4core::QualityContext* active_core = nullptr;
    uint32_t last_output_width=0,last_output_height=0;
    std::filesystem::path diagnostic_path;
    bool first_dispatch = true;
    bool device_support_checked = false;
    bool sharpening_ignored_logged = false;
    bool optional_masks_ignored_logged = false;
    bool pending_color_reset = false;
};

constexpr const char* presetName(std::uint32_t id) noexcept {
    switch (id) {
    case 0: return "native";
    case 1: return "quality";
    case 2: return "balanced";
    case 3: return "performance";
    case 4: return "drs";
    case 5: return "ultraperf";
    default: return nullptr;
    }
}

const char* selectedPreset(const ProviderContext& context, uint32_t rw, uint32_t ow) {
    return context.forced_preset == FSR4VK_PRESET_AUTO ? fsr4::resolution_preset(rw, ow,
               (context.flags & FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION) != 0)
                                                     : presetName(context.forced_preset);
}

void diagnostic(const ProviderContext& context, const std::string& text) noexcept {
    try {
        if (!context.diagnostic_path.empty()) {
            std::ofstream log(context.diagnostic_path,std::ios::app);
            const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            log << "[time_ms=" << ms << " context=" << context.diagnostic_id << "] " << text << '\n';
        }
    } catch (...) { }
}

void provider_message(const ProviderContext& context, uint32_t type,
                      const std::string& text) noexcept {
    diagnostic(context,text);
    if (!context.message) return;
    try {
        const std::wstring wide(text.begin(),text.end());
        context.message(type,wide.c_str());
    } catch (...) { }
}

std::filesystem::path asset_root() {
    if (const char* root=std::getenv("FSR4_VK_ASSET_ROOT")) return root;
#if defined(FSR4_EMBEDDED_ASSETS)
    return fsr4assets::root;
#endif
#if defined(_WIN32)
    HMODULE module{};
    wchar_t path[32768]{};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&ffxCreateContext),&module)) {
        const auto length=GetModuleFileNameW(module,path,32768);
        if (length && length<32768)
            return std::filesystem::path(path).parent_path()/"fsr4-vulkan-assets";
    }
#endif
    throw std::invalid_argument("FSR4_VK_ASSET_ROOT must name the shader/model sidecar directory");
}

bool validAllocator(const ffxAllocationCallbacks* callbacks) {
    return callbacks == nullptr || (callbacks->alloc != nullptr && callbacks->dealloc != nullptr);
}

void* allocate(const ffxAllocationCallbacks* callbacks, std::size_t size) {
    return callbacks ? callbacks->alloc(callbacks->pUserData, size) : std::malloc(size);
}

void deallocate(const ffxAllocationCallbacks* callbacks, void* memory) {
    if (callbacks) callbacks->dealloc(callbacks->pUserData, memory);
    else std::free(memory);
}

const CreateBackendVkDesc* findBackend(const ffxApiHeader* header) {
    for (auto* current = header; current; current = current->pNext) {
        if (current->type == kBackendVkDescType) {
            return reinterpret_cast<const CreateBackendVkDesc*>(current);
        }
    }
    return nullptr;
}

std::uint32_t findVulkanApiVersion(const ffxApiHeader* header) {
    for (auto* current = header; current; current = current->pNext) {
        if (current->type == kFsr4VulkanApiVersionDescType) {
            return reinterpret_cast<const Fsr4VulkanApiVersionDesc*>(current)->apiVersion;
        }
    }
    // Standard FFX Vulkan backend descriptors do not carry the instance API
    // version. Unknown callers take the conservative compatibility path.
    return VK_API_VERSION_1_1;
}

std::uint64_t findVersion(const ffxApiHeader* header) {
    for (auto* current = header; current; current = current->pNext) {
        if (current->type == FFX_API_DESC_TYPE_OVERRIDE_VERSION) {
            return reinterpret_cast<const ffxOverrideVersion*>(current)->versionId;
        }
    }
    return 0;
}

float upscaleRatio(std::uint32_t qualityMode) {
    constexpr float ratios[] = {1.0f, 1.5f, 1.7f, 2.0f, 3.0f};
    return qualityMode < std::size(ratios) ? ratios[qualityMode] : 0.0f;
}

float radicalInverse(std::uint32_t index, std::uint32_t base) {
    float value = 0.0f;
    const float inverseBase = 1.0f / static_cast<float>(base);
    float fraction = inverseBase;
    while (index) {
        value += static_cast<float>(index % base) * fraction;
        index /= base;
        fraction *= inverseBase;
    }
    return value;
}

ffxReturnCode_t prepareVulkanDevice(ffxQueryDescVkPrepareDevice* query) noexcept {
    if (!query || !query->physicalDevice || !query->sourceCreateInfo) {
        return FFX_API_RETURN_ERROR_PARAMETER;
    }

    query->outputCreateInfo = nullptr;
    query->token = nullptr;
    query->errorMessage = nullptr;
    static thread_local std::string error;

    try {
        auto requirements = std::make_unique<fsr4vk::DeviceFeatures>(
            query->physicalDevice, *query->sourceCreateInfo, query->apiVersion,
            query->getPhysicalDeviceFeatures2,
            query->enumerateDeviceExtensionProperties);
        query->outputCreateInfo = requirements->get();
        query->token = requirements.release();
        return FFX_API_RETURN_OK;
    } catch (const std::exception& exception) {
        error = exception.what();
        error += " (device feature chain:";
        auto* node = static_cast<const VkBaseInStructure*>(query->sourceCreateInfo->pNext);
        for (unsigned i=0; node && i<64; ++i, node=node->pNext)
            error += " " + std::to_string(node->sType);
        error += ")";
        query->errorMessage = error.c_str();
        return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    } catch (...) {
        error = "unknown Vulkan device-requirements failure";
        query->errorMessage = error.c_str();
        return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    }
}

ffxReturnCode_t releaseVulkanDevice(ffxQueryDescVkReleaseDevice* query) noexcept {
    if (!query || !query->token) {
        return FFX_API_RETURN_ERROR_PARAMETER;
    }
    delete static_cast<fsr4vk::DeviceFeatures*>(query->token);
    query->token = nullptr;
    return FFX_API_RETURN_OK;
}

ffxReturnCode_t queryUpscale(ffxContext* context, ffxQueryDescHeader* header) {
    switch (header->type) {
    case FSR4VK_QUERY_DESC_TYPE_PRESET_CAPABILITIES: {
        auto* query = reinterpret_cast<Fsr4VkQueryPresetCapabilities*>(header);
        query->forcedPresetMask = 0;
        if (!context || !*context) return FFX_API_RETURN_ERROR_PARAMETER;
        query->forcedPresetMask = 0x3fu; // Presets 0 through 5, including DRS.
        return FFX_API_RETURN_OK;
    }
    case FSR4VK_QUERY_DESC_TYPE_ACTIVE_PRESET: {
        auto* query = reinterpret_cast<Fsr4VkQueryActivePreset*>(header);
        query->activePreset = FSR4VK_PRESET_UNKNOWN;
        if (!context || !*context) return FFX_API_RETURN_ERROR_PARAMETER;
        const auto& preset = static_cast<ProviderContext*>(*context)->active_preset;
        if (preset == "native") query->activePreset = 0;
        else if (preset == "quality") query->activePreset = 1;
        else if (preset == "balanced") query->activePreset = 2;
        else if (preset == "performance") query->activePreset = 3;
        else if (preset == "drs") query->activePreset = 4;
        else if (preset == "ultraperf") query->activePreset = 5;
        return FFX_API_RETURN_OK;
    }
    case FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION: {
        auto* query = reinterpret_cast<ffxQueryGetProviderVersion*>(header);
        if (!context || !*context) return FFX_API_RETURN_ERROR_PARAMETER;
        query->versionId = static_cast<ProviderContext*>(*context)->version;
        query->versionName = query->versionId==kVersion411Id ? kVersion411Name : kVersionName;
        return FFX_API_RETURN_OK;
    }
    case FFX_API_QUERY_DESC_TYPE_UPSCALE_GETUPSCALERATIOFROMQUALITYMODE: {
        auto* query = reinterpret_cast<ffxQueryDescUpscaleGetUpscaleRatioFromQualityMode*>(header);
        const float ratio = upscaleRatio(query->qualityMode);
        if (!query->pOutUpscaleRatio || ratio == 0.0f) return FFX_API_RETURN_ERROR_PARAMETER;
        *query->pOutUpscaleRatio = ratio;
        return FFX_API_RETURN_OK;
    }
    case FFX_API_QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE: {
        auto* query = reinterpret_cast<ffxQueryDescUpscaleGetRenderResolutionFromQualityMode*>(header);
        const float ratio = upscaleRatio(query->qualityMode);
        if (!query->pOutRenderWidth || !query->pOutRenderHeight || ratio == 0.0f) {
            return FFX_API_RETURN_ERROR_PARAMETER;
        }
        *query->pOutRenderWidth = static_cast<std::uint32_t>(
            std::ceil(static_cast<float>(query->displayWidth) / ratio));
        *query->pOutRenderHeight = static_cast<std::uint32_t>(
            std::ceil(static_cast<float>(query->displayHeight) / ratio));
        return FFX_API_RETURN_OK;
    }
    case FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT: {
        auto* query = reinterpret_cast<ffxQueryDescUpscaleGetJitterPhaseCount*>(header);
        if (!query->pOutPhaseCount || !query->renderWidth) return FFX_API_RETURN_ERROR_PARAMETER;
        const float ratio = static_cast<float>(query->displayWidth) /
                            static_cast<float>(query->renderWidth);
        *query->pOutPhaseCount = static_cast<std::int32_t>(8.0f * ratio * ratio);
        return FFX_API_RETURN_OK;
    }
    case FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET: {
        auto* query = reinterpret_cast<ffxQueryDescUpscaleGetJitterOffset*>(header);
        if (!query->pOutX || !query->pOutY || query->phaseCount <= 0 || query->index < 0) {
            return FFX_API_RETURN_ERROR_PARAMETER;
        }
        const std::uint32_t sample = static_cast<std::uint32_t>(query->index % query->phaseCount) + 1;
        *query->pOutX = radicalInverse(sample, 2) - 0.5f;
        *query->pOutY = radicalInverse(sample, 3) - 0.5f;
        return FFX_API_RETURN_OK;
    }
    case FFX_API_QUERY_DESC_TYPE_UPSCALE_GET_RESOURCE_REQUIREMENTS: {
        auto* query = reinterpret_cast<ffxQueryDescUpscaleGetResourceRequirements*>(header);
        query->required_resources = FFX_API_QUERY_RESOURCE_INPUT_COLOR |
                                    FFX_API_QUERY_RESOURCE_INPUT_DEPTH |
                                    FFX_API_QUERY_RESOURCE_INPUT_MV |
                                    FFX_API_QUERY_RESOURCE_INPUT_EXPOSURE;
        query->optional_resources = 0;
        if (context && *context && (static_cast<ProviderContext*>(*context)->flags & FFX_UPSCALE_ENABLE_AUTO_EXPOSURE))
            query->required_resources &= ~FFX_API_QUERY_RESOURCE_INPUT_EXPOSURE;
        return FFX_API_RETURN_OK;
    }
    case FFX_API_QUERY_DESC_TYPE_UPSCALE_GPU_MEMORY_USAGE: {
        auto* query = reinterpret_cast<ffxQueryDescUpscaleGetGPUMemoryUsage*>(header);
        if (!context || !*context || !query->gpuMemoryUsageUpscaler) {
            return FFX_API_RETURN_ERROR_PARAMETER;
        }
        return FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE;
    }
    case FFX_API_QUERY_DESC_TYPE_UPSCALE_GPU_MEMORY_USAGE_V2: {
        auto* query = reinterpret_cast<ffxQueryDescUpscaleGetGPUMemoryUsageV2*>(header);
        if (!query->gpuMemoryUsageUpscaler) return FFX_API_RETURN_ERROR_PARAMETER;
        return FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE;
    }
    default:
        return FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE;
    }
}

}  // namespace

extern "C" FFX_API_ENTRY ffxReturnCode_t ffxCreateContext(
    ffxContext* context,
    ffxCreateContextDescHeader* header,
    const ffxAllocationCallbacks* callbacks) {
    if (!context || !header || !validAllocator(callbacks)) return FFX_API_RETURN_ERROR_PARAMETER;
    *context = nullptr;
    if (header->type != FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE) {
        return FFX_API_RETURN_NO_PROVIDER;
    }
    const auto version = findVersion(header);
    // Well-behaved callers query this DLL and request kVersionId. Some games
    // loading amd_fidelityfx_vk.dll pin the stock FSR 3.1.4 provider ID
    // instead; accept that ABI-compatible selection as a drop-in alias.
    if (version != 0 && version != kVersionId && version != kVersion411Id &&
        version != kFsr314CompatibilityVersionId) {
        return FFX_API_RETURN_NO_PROVIDER;
    }
    const auto* backend = findBackend(header);
    if (!backend || !backend->device || !backend->physicalDevice ||
        !backend->getDeviceProcAddr) {
        return FFX_API_RETURN_ERROR_PARAMETER;
    }
    const auto* create = reinterpret_cast<const ffxCreateContextDescUpscale*>(header);
    void* storage = allocate(callbacks, sizeof(ProviderContext));
    if (!storage) return FFX_API_RETURN_ERROR_MEMORY;
    auto* provider = new (storage) ProviderContext{};
    provider->version = version==kVersion411Id ? kVersion411Id : kVersionId;
    provider->device = backend->device;
    provider->physicalDevice = backend->physicalDevice;
    provider->getDeviceProcAddr = backend->getDeviceProcAddr;
    provider->apiVersion = findVulkanApiVersion(header);
    provider->flags = create->flags;
    provider->maxRenderSize = create->maxRenderSize;
    provider->maxUpscaleSize = create->maxUpscaleSize;
    provider->message = create->fpMessage;
    try {
        const auto assets=version==kVersion411Id ? asset_root()/"fsr411" : asset_root();
        provider->assets=assets;
        const bool embedded=fsr4assets::is_embedded(assets);
        if (!embedded) {
            if (!std::filesystem::is_directory(assets/"general"))
                throw std::invalid_argument("general-resolution asset bundles are missing");
        }
        const char* log=std::getenv("FSR4_VK_LOG_PATH");
        if (fsr4vk::provider_log_requested(std::getenv("FSR4_VK_LOG"), log)) {
            if (log && *log) provider->diagnostic_path=log;
#if defined(_WIN32)
            else {
                // nullptr selects the host EXE even when this DLL lives under
                // OptiScaler/. Failure to open a log must not fail the upscaler.
                try {
                    wchar_t executable[32768]{};
                    const auto length=GetModuleFileNameW(nullptr,executable,32768);
                    const auto host=(length && length<32768)
                        ? std::filesystem::path(executable) : std::filesystem::path{};
                    std::error_code ec;
                    auto temp=std::filesystem::temp_directory_path(ec);
                    if(ec) temp.clear();
                    provider->diagnostic_path=fsr4vk::default_provider_log_path(
                        host,temp,GetCurrentProcessId(),[](const std::filesystem::path& path) {
                            std::ofstream stream(path,std::ios::app);
                            return stream.good();
                        });
                } catch (...) { }
            }
#else
            else provider->diagnostic_path=assets/"provider.log";
#endif
        }
        diagnostic(*provider,"context request flags="+std::to_string(create->flags)+
                   " max_render="+std::to_string(create->maxRenderSize.width)+"x"+
                   std::to_string(create->maxRenderSize.height)+" max_output="+
                   std::to_string(create->maxUpscaleSize.width)+"x"+
                   std::to_string(create->maxUpscaleSize.height));
        diagnostic(*provider,std::string("version=")+
                   (provider->version==kVersion411Id ? kVersion411Name : kVersionName));
        const uint32_t required_flags = FFX_UPSCALE_ENABLE_DEPTH_INVERTED;
        const uint32_t permutation = create->flags & ~(FFX_UPSCALE_ENABLE_DEBUG_CHECKING | FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE |
                                                        FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE |
                                                        FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE);
        fsr4::validate_dimensions(create->maxRenderSize.width,create->maxRenderSize.height,
                                 create->maxUpscaleSize.width,create->maxUpscaleSize.height);
        if(permutation!=required_flags)
            throw std::invalid_argument("provider requires inverted depth and low-resolution non-jittered motion vectors");
        fsr4vk::validate_descriptor_commands(backend->device,backend->getDeviceProcAddr);
        (void)fsr4vk::DeviceDispatch(backend->device,backend->getDeviceProcAddr,provider->apiVersion);
        diagnostic(*provider,"context created flags="+std::to_string(create->flags)+
                   " api="+std::to_string(VK_API_VERSION_MAJOR(provider->apiVersion))+"."+
                   std::to_string(VK_API_VERSION_MINOR(provider->apiVersion)));
    } catch (const std::exception& error) {
        provider_message(*provider,FFX_API_MESSAGE_TYPE_ERROR,std::string("context rejected: ")+error.what());
        provider->~ProviderContext();
        deallocate(callbacks,provider);
        return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    }
    *context = provider;
    return FFX_API_RETURN_OK;
}

extern "C" FFX_API_ENTRY ffxReturnCode_t ffxDestroyContext(
    ffxContext* context,
    const ffxAllocationCallbacks* callbacks) {
    if (!context || !*context || !validAllocator(callbacks)) return FFX_API_RETURN_ERROR_PARAMETER;
    auto* provider = reinterpret_cast<ProviderContext*>(*context);
    diagnostic(*provider,"context destroyed cached_models="+std::to_string(provider->cores.size()));
    provider->~ProviderContext();
    deallocate(callbacks, provider);
    *context = nullptr;
    return FFX_API_RETURN_OK;
}

extern "C" FFX_API_ENTRY ffxReturnCode_t ffxConfigure(
    ffxContext* context,
    const ffxConfigureDescHeader* header) {
    if (!context || !*context || !header) return FFX_API_RETURN_ERROR_PARAMETER;
    if (header->type == FSR4VK_CONFIGURE_DESC_TYPE_PRESET) {
        const auto preset = reinterpret_cast<const Fsr4VkConfigurePreset*>(header)->preset;
        if (preset != FSR4VK_PRESET_AUTO && !presetName(preset))
            return FFX_API_RETURN_ERROR_PARAMETER;
        // No allocations or resource destruction: cached cores may still be in
        // flight. Dispatch lazily creates the chosen model and resets history
        // when active_preset changes. Reporting stays tied to actual dispatch.
        static_cast<ProviderContext*>(*context)->forced_preset = preset;
        return FFX_API_RETURN_OK;
    }
    if (header->type == FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE) return FFX_API_RETURN_OK;
    return FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE;
}

extern "C" FFX_API_ENTRY ffxReturnCode_t ffxQuery(
    ffxContext* context,
    ffxQueryDescHeader* header) {
    if (!header) return FFX_API_RETURN_ERROR_PARAMETER;
    if (!context && header->type == FFX_API_QUERY_DESC_TYPE_VK_PREPARE_DEVICE) {
        return prepareVulkanDevice(reinterpret_cast<ffxQueryDescVkPrepareDevice*>(header));
    }
    if (!context && header->type == FFX_API_QUERY_DESC_TYPE_VK_RELEASE_DEVICE) {
        return releaseVulkanDevice(reinterpret_cast<ffxQueryDescVkReleaseDevice*>(header));
    }
    if (!context && header->type == FFX_API_QUERY_DESC_TYPE_GET_VERSIONS) {
        auto* query = reinterpret_cast<ffxQueryDescGetVersions*>(header);
        if (!query->outputCount) return FFX_API_RETURN_ERROR_PARAMETER;
        if (query->createDescType != FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE) {
            *query->outputCount = 0;
            return FFX_API_RETURN_OK;
        }
        const std::uint64_t capacity = *query->outputCount;
        if (capacity > 0) {
            if (query->versionIds) query->versionIds[0] = kVersionId;
            if (query->versionNames) query->versionNames[0] = kVersionName;
        }
        if (capacity > 1) {
            if (query->versionIds) query->versionIds[1] = kVersion411Id;
            if (query->versionNames) query->versionNames[1] = kVersion411Name;
        }
        *query->outputCount = 2;
        return FFX_API_RETURN_OK;
    }
    return queryUpscale(context, header);
}

extern "C" FFX_API_ENTRY ffxReturnCode_t ffxDispatch(
    ffxContext* context,
    const ffxDispatchDescHeader* header) {
    if (!context || !*context || !header) return FFX_API_RETURN_ERROR_PARAMETER;
    if (header->type != FFX_API_DISPATCH_DESC_TYPE_UPSCALE) {
        return FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE;
    }
    auto* provider = static_cast<ProviderContext*>(*context);
    const auto& d = *reinterpret_cast<const ffxDispatchDescUpscale*>(header);
    const auto rw=d.renderSize.width,rh=d.renderSize.height;
    const auto ow=d.upscaleSize.width ? d.upscaleSize.width : provider->maxUpscaleSize.width;
    const auto oh=d.upscaleSize.height ? d.upscaleSize.height : provider->maxUpscaleSize.height;
    const auto reject = [provider](const char* reason) {
        provider_message(*provider,FFX_API_MESSAGE_TYPE_ERROR,
                         std::string("FSR4 Vulkan dispatch rejected: ")+reason);
        return FFX_API_RETURN_ERROR_PARAMETER;
    };
    if (!d.commandList) return reject("null command list");
    if (d.enableSharpening && !provider->sharpening_ignored_logged) {
        provider_message(*provider, FFX_API_MESSAGE_TYPE_WARNING,
                         "FSR4 Vulkan internal sharpening is unavailable; use host RCAS if desired");
        provider->sharpening_ignored_logged = true;
    }
    if (d.flags & ~(FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB | FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_PQ))
        return reject("dispatch flags other than sRGB/PQ color selection are unsupported");
    const auto color_space=fsr4core::select_color_space(
        (provider->flags & FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE)!=0,
        (d.flags & FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB)!=0,
        (d.flags & FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_PQ)!=0);
    // FSR2/3-compatible hosts may supply a reactive/composition mask (including
    // a converted DLSS bias mask). The current FSR4 model path has no external
    // mask inputs. Accept their presence without reading, binding or changing
    // the caller's resources; optional inputs must not prevent upscaling.
    if ((d.reactive.resource || d.transparencyAndComposition.resource) &&
        !provider->optional_masks_ignored_logged) {
        provider_message(*provider, FFX_API_MESSAGE_TYPE_WARNING,
                         "FSR4 Vulkan ignores optional reactive/transparency masks; "
                         "the current model path does not consume them");
        provider->optional_masks_ignored_logged = true;
    }
    const auto resource = [](const char* name, const FfxApiResource& r, uint32_t width, uint32_t height,
                             uint32_t format, VkFormat vkformat, uint32_t state,
                             uint32_t depth_usage = 0) {
        if (!r.resource || r.description.type != FFX_API_RESOURCE_TYPE_TEXTURE2D ||
            (r.description.usage & (FFX_API_RESOURCE_USAGE_DEPTHTARGET | FFX_API_RESOURCE_USAGE_STENCILTARGET)) != depth_usage ||
            r.description.width != width || r.description.height != height ||
            r.description.format != format || r.state != state)
            throw std::invalid_argument(std::string("unsupported ")+name+" resource");
        return fsr4core::Image{reinterpret_cast<VkImage>(r.resource),VK_NULL_HANDLE,
                              VK_NULL_HANDLE,vkformat,width,height};
    };
    try {
        fsr4::validate_dimensions(rw,rh,ow,oh);
        if(rw>provider->maxRenderSize.width || rh>provider->maxRenderSize.height ||
           ow>provider->maxUpscaleSize.width || oh>provider->maxUpscaleSize.height)
            throw std::invalid_argument("dispatch dimensions exceed context capacity");
        const std::string selected_preset=selectedPreset(*provider,rw,ow);
        // Repeated dispatches use the same owned core without a map lookup.
        // std::map keeps the other cores alive; switching never frees in-flight work.
        auto* core=provider->active_preset==selected_preset ? provider->active_core : nullptr;
        if (!core) core=provider->cores[selected_preset].get();
        if(!core) {
            if (!provider->device_support_checked) {
                // This queries physical support, NOT the bits enabled on the
                // application's already-created VkDevice. Vulkan exposes no
                // general post-creation query for those bits.
                VkPhysicalDeviceProperties properties{};
                vkGetPhysicalDeviceProperties(provider->physicalDevice,&properties);
                VkDeviceCreateInfo empty{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
                try {
                    fsr4vk::DeviceFeatures supported(provider->physicalDevice,empty,properties.apiVersion,
                        vkGetPhysicalDeviceFeatures2,vkEnumerateDeviceExtensionProperties);
                } catch(const std::exception& error) {
                    throw std::runtime_error(std::string("FSR4VK_ERROR_DEVICE_CAPABILITY_CHECK: ")+error.what()+
                        "; check GPU/driver support; unsupported capabilities cannot be enabled by the provider");
                }
                diagnostic(*provider,"physical capability check passed; enabled feature bits cannot be queried after vkCreateDevice. "
                    "Host must enable descriptorBindingVariableDescriptorCount, mutableDescriptorType, computeDerivativeGroupLinear, "
                    "shaderIntegerDotProduct and other required shader features; vulkanMemoryModel requires device scope when enabled.");
                provider->device_support_checked=true;
            }
            diagnostic(*provider,"creating model="+selected_preset+
                       " exposure="+((provider->flags & FFX_UPSCALE_ENABLE_AUTO_EXPOSURE) ? "auto" : "external"));
            const auto build_started=std::chrono::steady_clock::now();
            const auto bucket=provider->maxUpscaleSize.width>1920 || provider->maxUpscaleSize.height>1080 ? "2160" : "1080";
            const auto bundle=provider->assets/"general"/bucket/selected_preset;
            auto created=std::make_unique<fsr4core::QualityContext>(provider->physicalDevice,provider->device,
                bundle,bundle/"initializers.bin",bundle/"weights.bin",true,
                provider->getDeviceProcAddr,
                provider->maxRenderSize.width,provider->maxRenderSize.height,
                provider->maxUpscaleSize.width,provider->maxUpscaleSize.height,
                provider->apiVersion,
                (provider->flags & FFX_UPSCALE_ENABLE_AUTO_EXPOSURE)==0,provider->version==kVersion411Id);
            core=created.get();
            provider->cores[selected_preset]=std::move(created);
            if(!provider->diagnostic_path.empty()) {
                const auto& allocation=core->initializer_allocation();
                diagnostic(*provider,"initializer_memory bytes="+std::to_string(allocation.size)+
                    " type="+std::to_string(allocation.memory_type_index)+
                    " flags="+std::to_string(allocation.memory_properties)+
                    " device_local="+std::to_string(bool(allocation.memory_properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))+
                    " fallback="+std::to_string(allocation.preferred_memory_fallback));
            }
            diagnostic(*provider,"model ready="+selected_preset+" cpu_build_ms="+
                std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now()-build_started).count()));
        }
        const bool color_changed=core->set_color_space(color_space);
        provider->pending_color_reset |= color_changed;
        if(color_changed || provider->first_dispatch)
            diagnostic(*provider,std::string("color_space=")+fsr4core::color_space_name(color_space)+
                       " history_reset=1");
        const bool force_reset=provider->pending_color_reset || provider->active_preset!=selected_preset ||
                               provider->last_output_width!=ow ||
                               provider->last_output_height!=oh;
        const bool packed_color=d.color.description.format==FFX_API_SURFACE_FORMAT_R11G11B10_FLOAT;
        const bool shared_exponent_color=
            d.color.description.format==FFX_API_SURFACE_FORMAT_R9G9B9E5_SHAREDEXP;
        const bool packed_output=d.output.description.format==FFX_API_SURFACE_FORMAT_R11G11B10_FLOAT;
        const uint32_t depth_stencil=FFX_API_RESOURCE_USAGE_DEPTHTARGET | FFX_API_RESOURCE_USAGE_STENCILTARGET;
        const bool game_depth=(d.depth.description.usage & depth_stencil)==depth_stencil;
        const auto color_format=shared_exponent_color ? FFX_API_SURFACE_FORMAT_R9G9B9E5_SHAREDEXP :
            (packed_color ? FFX_API_SURFACE_FORMAT_R11G11B10_FLOAT : FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT);
        const auto color_vk_format=shared_exponent_color ? VK_FORMAT_E5B9G9R9_UFLOAT_PACK32 :
            (packed_color ? VK_FORMAT_B10G11R11_UFLOAT_PACK32 : VK_FORMAT_R16G16B16A16_SFLOAT);
        const auto color = resource("color",d.color,rw,rh,color_format,color_vk_format,
            FFX_API_RESOURCE_STATE_COMPUTE_READ);
        const auto depth = resource("depth",d.depth,rw,rh,FFX_API_SURFACE_FORMAT_R32_FLOAT,
            game_depth ? VK_FORMAT_D32_SFLOAT_S8_UINT : VK_FORMAT_R32_SFLOAT,FFX_API_RESOURCE_STATE_COMPUTE_READ,
            game_depth ? depth_stencil : 0);
        const auto motion = resource("motion",d.motionVectors,rw,rh,FFX_API_SURFACE_FORMAT_R16G16_FLOAT,
            VK_FORMAT_R16G16_SFLOAT,FFX_API_RESOURCE_STATE_COMPUTE_READ);
        // The adapter samples floating-point exposure and writes our internal
        // R32 history. Preserve the caller's real format: R16 sampling expands
        // the value to float without reinterpreting its bits or adding a pass.
        const bool half_exposure=d.exposure.description.format==FFX_API_SURFACE_FORMAT_R16_FLOAT;
        const auto exposure = (provider->flags & FFX_UPSCALE_ENABLE_AUTO_EXPOSURE) ? fsr4core::Image{} : resource("exposure",d.exposure,1,1,
            half_exposure ? FFX_API_SURFACE_FORMAT_R16_FLOAT : FFX_API_SURFACE_FORMAT_R32_FLOAT,
            half_exposure ? VK_FORMAT_R16_SFLOAT : VK_FORMAT_R32_SFLOAT,FFX_API_RESOURCE_STATE_COMPUTE_READ);
        const auto output = resource("output",d.output,ow,oh,packed_output ? FFX_API_SURFACE_FORMAT_R11G11B10_FLOAT : FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT,
            packed_output ? VK_FORMAT_B10G11R11_UFLOAT_PACK32 : VK_FORMAT_R16G16B16A16_SFLOAT,FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        fsr4core::OptimizedConstants c{};
        const bool fsr411=provider->version==kVersion411Id;
        // Unlike 4.0.2, 4.1.1 keeps image sampling at the exact output size.
        // Its recorder separately pads the neural tensors to eight pixels.
        const auto padded_w=fsr411 ? ow : fsr4::round_up(ow,8);
        const auto padded_h=fsr411 ? oh : fsr4::round_up(oh,8);
        c.inv_size[0]=1.f/padded_w; c.inv_size[1]=1.f/padded_h;
        c.scale[0]=float(padded_w)/rw; c.scale[1]=float(padded_h)/rh;
        c.inv_scale[0]=1.f/c.scale[0]; c.inv_scale[1]=1.f/c.scale[1];
        c.jitter[0]=d.jitterOffset.x; c.jitter[1]=d.jitterOffset.y;
        // FFX API scales convert input vectors to pixels; the optimized shader
        // consumes normalized UV displacement. Only low-resolution MV is supported.
        c.mv_scale[0]=d.motionVectorScale.x/d.renderSize.width;
        c.mv_scale[1]=d.motionVectorScale.y/d.renderSize.height;
        c.tex_size[0]=fsr411 ? provider->maxUpscaleSize.width : fsr4::round_up(provider->maxUpscaleSize.width,8);
        c.tex_size[1]=fsr411 ? provider->maxUpscaleSize.height : fsr4::round_up(provider->maxUpscaleSize.height,8);
        c.max_render_size[0]=provider->maxRenderSize.width;
        c.max_render_size[1]=provider->maxRenderSize.height;
        c.width=padded_w; c.height=padded_h; c.width_lr=rw; c.height_lr=rh;
        c.reset=d.reset || force_reset;
        c.pre_exposure=fsr4::sanitize_pre_exposure(d.preExposure);
        if (c.pre_exposure!=d.preExposure && provider->first_dispatch)
            provider_message(*provider,FFX_API_MESSAGE_TYPE_WARNING,
                             "FSR4 Vulkan replaced invalid pre-exposure with 1.0");
        core->record(static_cast<VkCommandBuffer>(d.commandList),color,depth,motion,exposure,output,c);
        provider->pending_color_reset=false;
        provider->active_preset=selected_preset;
        provider->active_core=core;
        provider->last_output_width=ow;provider->last_output_height=oh;
        if (provider->first_dispatch) {
            diagnostic(*provider,"preset="+selected_preset);
            diagnostic(*provider, std::string("shader_backend=") + core->shader_backend());
            diagnostic(*provider,"first dispatch recorded: "+std::to_string(rw)+"x"+
                std::to_string(rh)+" -> "+std::to_string(ow)+"x"+std::to_string(oh));
            diagnostic(*provider,"motion scale API="+std::to_string(d.motionVectorScale.x)+","+
                std::to_string(d.motionVectorScale.y)+" shader="+std::to_string(c.mv_scale[0])+","+
                std::to_string(c.mv_scale[1]));
            provider->first_dispatch=false;
        }
        return FFX_API_RETURN_OK;
    } catch (const std::invalid_argument& error) {
        provider_message(*provider,FFX_API_MESSAGE_TYPE_ERROR,
                         std::string("FSR4 Vulkan dispatch rejected: ")+error.what());
        return FFX_API_RETURN_ERROR_PARAMETER;
    } catch (const std::exception& error) {
        provider_message(*provider,FFX_API_MESSAGE_TYPE_ERROR,
                         std::string("FSR4 Vulkan dispatch failed: ")+error.what());
        return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    } catch (...) {
        provider_message(*provider,FFX_API_MESSAGE_TYPE_ERROR,
                         "FSR4 Vulkan dispatch failed: unknown exception");
        return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    }
}
