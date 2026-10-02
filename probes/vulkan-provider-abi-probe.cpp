#if !defined(_WIN32)
#define __declspec(x)
#endif

#include "../amd-fidelityfx-sdk/Kits/FidelityFX/upscalers/include/ffx_upscale.h"
#include <vulkan/vulkan.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <cstdint>
#include <array>
#include <limits>
#include <iostream>
#include <string>

namespace {

struct CreateBackendVkDesc {
    ffxCreateContextDescHeader header;
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr;
};

PFN_vkVoidFunction VKAPI_PTR fakeGetDeviceProcAddr(VkDevice, const char*) {
    return nullptr;
}

template <typename T>
T loadSymbol(void* module, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<T>(GetProcAddress(static_cast<HMODULE>(module), name));
#else
    return reinterpret_cast<T>(dlsym(module, name));
#endif
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " PROVIDER_LIBRARY\n";
        return 2;
    }
#if defined(_WIN32)
    void* module = LoadLibraryA(argv[1]);
#else
    void* module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
#endif
    if (!module) {
        std::cerr << "load=failed\n";
        return 1;
    }
    auto query = loadSymbol<PfnFfxQuery>(module, "ffxQuery");
    auto create = loadSymbol<PfnFfxCreateContext>(module, "ffxCreateContext");
    auto destroy = loadSymbol<PfnFfxDestroyContext>(module, "ffxDestroyContext");
    auto configure = loadSymbol<PfnFfxConfigure>(module, "ffxConfigure");
    auto dispatch = loadSymbol<PfnFfxDispatch>(module, "ffxDispatch");
    std::cout << "exports=" << (query && create && destroy && configure && dispatch ? "ok" : "missing") << '\n';
    if (!query || !create || !destroy || !configure || !dispatch) return 1;

    std::uint64_t count = 0;
    ffxQueryDescGetVersions versions{};
    versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    versions.outputCount = &count;
    const auto countResult = query(nullptr, &versions.header);
    std::cout << "count_result=" << countResult << " count=" << count << '\n';
    if (countResult != FFX_API_RETURN_OK || count != 2) return 1;

    constexpr std::uint64_t canary = 0xa5a5a5a5a5a5a5a5ull;
    std::uint64_t boundedIds[2]{0,canary};
    const char* sentinel = "untouched";
    const char* boundedNames[2]{nullptr,sentinel};
    versions.versionIds = boundedIds;
    versions.versionNames = boundedNames;
    count = 1; // A bounded query must not write the second version here.
    const auto listResult = query(nullptr, &versions.header);
    if (boundedIds[1]!=canary || boundedNames[1]!=sentinel || count!=2) return 1;
    const auto id = boundedIds[0];
    const auto name = boundedNames[0];
    std::cout << std::hex << "version_id=0x" << id << std::dec
              << " version_name=" << (name ? name : "<null>")
              << " list_result=" << listResult << '\n';
    if (listResult != FFX_API_RETURN_OK || id == 0 || !name) return 1;
    std::uint64_t ids[2]{};
    const char* names[2]{};
    versions.versionIds = ids;
    versions.versionNames = names;
    count = 2;
    if (query(nullptr, &versions.header) != FFX_API_RETURN_OK || count != 2 ||
        ids[0] != id || ids[1] == ids[0] || !names[0] || !names[1] ||
        std::string("FSR")+names[0] != "FSR 4.0.2 VK INT8" ||
        std::string("FSR")+names[1] != "FSR 4.1.1 VK INT8") return 1;
    std::cout << "selector_names=FSR" << names[0] << ",FSR" << names[1] << '\n';
    std::cout << "version_list=both-models bounded_query=passed\n";

    uint32_t rw=0, rh=0;
    ffxQueryDescUpscaleGetRenderResolutionFromQualityMode resolution{};
    resolution.header.type=FFX_API_QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE;
    resolution.displayWidth=resolution.displayHeight=std::numeric_limits<uint32_t>::max();
    resolution.qualityMode=FFX_UPSCALE_QUALITY_MODE_NATIVEAA;
    resolution.pOutRenderWidth=&rw; resolution.pOutRenderHeight=&rh;
    if(query(nullptr,&resolution.header)!=FFX_API_RETURN_OK || rw!=resolution.displayWidth || rh!=resolution.displayHeight) return 1;
    resolution.displayWidth=0;
    if(query(nullptr,&resolution.header)!=FFX_API_RETURN_ERROR_PARAMETER) return 1;
    int32_t phases=107;
    ffxQueryDescUpscaleGetJitterPhaseCount phase{};
    phase.header.type=FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
    phase.renderWidth=1; phase.displayWidth=std::numeric_limits<uint32_t>::max(); phase.pOutPhaseCount=&phases;
    if(query(nullptr,&phase.header)!=FFX_API_RETURN_ERROR_PARAMETER || phases!=107) return 1;
    phase.renderWidth=0;
    if(query(nullptr,&phase.header)!=FFX_API_RETURN_ERROR_PARAMETER) return 1;
    float jx=1,jy=1;
    ffxQueryDescUpscaleGetJitterOffset jitter{};
    jitter.header.type=FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;
    jitter.index=-1; jitter.phaseCount=8; jitter.pOutX=&jx; jitter.pOutY=&jy;
    if(query(nullptr,&jitter.header)!=FFX_API_RETURN_ERROR_PARAMETER || jx!=1 || jy!=1) return 1;
    std::cout << "query_boundaries=uint32-native-resolution zero-size jitter-overflow negative-index-passed\n";

    ffxOverrideVersion overrideVersion{};
    overrideVersion.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
    overrideVersion.versionId = id;
    CreateBackendVkDesc backend{};
    backend.header.type = 3;
    backend.header.pNext = &overrideVersion.header;
    backend.device = reinterpret_cast<VkDevice>(static_cast<std::uintptr_t>(1));
    backend.physicalDevice = reinterpret_cast<VkPhysicalDevice>(static_cast<std::uintptr_t>(2));
    backend.getDeviceProcAddr = fakeGetDeviceProcAddr;
    ffxCreateContextDescUpscale createDesc{};
    createDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    createDesc.header.pNext = &backend.header;
    createDesc.maxRenderSize = {1920, 1080};
    createDesc.maxUpscaleSize = {1920, 1080};
    ffxContext providerContext = nullptr;
    const auto createResult = create(&providerContext, &createDesc.header, nullptr);
    std::cout << "create_result=" << createResult
              << " context=" << (providerContext ? "set" : "null") << '\n';
    // Dummy handles must no longer create an apparently usable GPU context.
    // Real create/dispatch/destroy is exercised by dispatch-smoke --provider.
    if (createResult != FFX_API_RETURN_ERROR_RUNTIME_ERROR || providerContext) return 1;
    std::cout << "invalid_backend=rejected\n";
    overrideVersion.versionId = ids[1];
    const auto newVersionResult = create(&providerContext, &createDesc.header, nullptr);
    if (newVersionResult != FFX_API_RETURN_ERROR_RUNTIME_ERROR || providerContext) return 1;

    constexpr std::uint64_t fsr314CompatibilityId =
        (0xF5A5CA1Eull << 32) | ((3ull << 22) | (1ull << 12) | 4ull);
    overrideVersion.versionId = fsr314CompatibilityId;
    const auto compatibilityResult = create(&providerContext, &createDesc.header, nullptr);
    std::cout << "fsr314_compatibility_result=" << compatibilityResult << '\n';
    if (compatibilityResult != FFX_API_RETURN_ERROR_RUNTIME_ERROR || providerContext) return 1;

    overrideVersion.versionId=ids[0];
    backend.header.pNext=&backend.header;
    if(create(&providerContext,&createDesc.header,nullptr)!=FFX_API_RETURN_ERROR_PARAMETER || providerContext) return 1;
    ffxOverrideVersion duplicate=overrideVersion;
    duplicate.header.pNext=nullptr;
    overrideVersion.header.pNext=&duplicate.header;
    backend.header.pNext=&overrideVersion.header;
    if(create(&providerContext,&createDesc.header,nullptr)!=FFX_API_RETURN_ERROR_PARAMETER || providerContext) return 1;
    overrideVersion.header.pNext=nullptr;
    backend.header.pNext=nullptr;
    std::array<ffxApiHeader,63> nodes{};
    for(size_t length : {size_t(62),size_t(63)}) {
        for(size_t i=0;i<length;++i) {
            nodes[i].type=0x7fffffff;
            nodes[i].pNext=i+1<length ? &nodes[i+1] : &backend.header;
        }
        createDesc.header.pNext=&nodes[0];
        const auto expected=length==62 ? FFX_API_RETURN_ERROR_RUNTIME_ERROR : FFX_API_RETURN_ERROR_PARAMETER;
        if(create(&providerContext,&createDesc.header,nullptr)!=expected || providerContext) return 1;
    }
    createDesc.header.pNext=&backend.header;
    backend.header.pNext=&overrideVersion.header;
    std::cout << "create_chain=cycle duplicate-version 64-node-boundary-passed\n";

    overrideVersion.versionId = 1;
    const auto unknownVersionResult = create(&providerContext, &createDesc.header, nullptr);
    std::cout << "unknown_version_result=" << unknownVersionResult << '\n';
    if (unknownVersionResult != FFX_API_RETURN_NO_PROVIDER || providerContext) return 1;
    return 0;
}
