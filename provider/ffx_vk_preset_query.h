#ifndef FSR4VK_PRESET_QUERY_H
#define FSR4VK_PRESET_QUERY_H

#include <cstdint>

// Include ffx_api.h before this header. Optional fsr4vk-specific, read-only
// descriptor for the existing ffxQuery export; not an AMD SDK descriptor.
// Freeze this layout/type once published; use a new type for incompatible changes.
constexpr ffxStructType_t FSR4VK_QUERY_DESC_TYPE_ACTIVE_PRESET = 0x4653523450525354ull;
constexpr std::uint32_t FSR4VK_PRESET_UNKNOWN = UINT32_MAX;

struct Fsr4VkQueryActivePreset
{
    ffxQueryDescHeader header;
    // Last successfully recorded dispatch, not GPU completion. UINT32_MAX before
    // the first dispatch: 0=native, 1=quality, 2=balanced, 3=performance,
    // 4=DRS, 5=ultra performance.
    std::uint32_t activePreset;
};

// Optional, separately discoverable forcing contract. Keep the reporting query
// above backward compatible with reporting-only providers.
constexpr ffxStructType_t FSR4VK_QUERY_DESC_TYPE_PRESET_CAPABILITIES = 0x4653523450434150ull;
constexpr ffxStructType_t FSR4VK_CONFIGURE_DESC_TYPE_PRESET = 0x4653523450534554ull;
constexpr std::uint32_t FSR4VK_PRESET_AUTO = UINT32_MAX;

struct Fsr4VkQueryPresetCapabilities
{
    ffxQueryDescHeader header;
    // Bit N means preset N can be forced. Default/Auto is always supported
    // when this query is recognized. Zero-initialize before querying.
    std::uint32_t forcedPresetMask;
};

struct Fsr4VkConfigurePreset
{
    ffxConfigureDescHeader header;
    // UINT32_MAX restores automatic selection; otherwise a supported model ID.
    // Applied on the next dispatch without changing input/output dimensions.
    // Invalid IDs return ERROR_PARAMETER without changing the previous setting.
    // Serialize configuration, query, dispatch, and destruction per context.
    std::uint32_t preset;
};
#endif
