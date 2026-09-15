// Compile against the consumer's FFX SDK, independently of the provider SDK.
#include <ffx_api.h>
#include <FFXVkPresetReporting.h>
#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout_v<Fsr4VkQueryActivePreset>);
static_assert(offsetof(Fsr4VkQueryActivePreset, header) == 0);
static_assert(offsetof(Fsr4VkQueryActivePreset, activePreset) == 16);
static_assert(sizeof(Fsr4VkQueryActivePreset) == 24);
static_assert(sizeof(Fsr4VkQueryPresetCapabilities) == 24);
static_assert(sizeof(Fsr4VkConfigurePreset) == 24);
static_assert(offsetof(Fsr4VkConfigurePreset, preset) == 16);
static_assert(sizeof(ffxStructType_t) == 8);
static_assert(sizeof(ffxReturnCode_t) == 4);
static_assert(sizeof(ffxContext) == 8);

void checkReader(PfnFfxQuery query, ffxContext* context, std::optional<uint32_t>& displayed) {
    bool supported = true;
    FFXVkPresetReporting::DispatchReport report{displayed};
    report.activePreset = FFXVkPresetReporting::Read(query, context, supported);
}
