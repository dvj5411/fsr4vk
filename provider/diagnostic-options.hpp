#pragma once
#include <cstring>

namespace fsr4vk {
inline bool provider_log_requested(const char* enabled, const char* path) noexcept {
    // An explicit, nonempty path is itself an opt-in. An empty path must not
    // suppress the default location when FSR4_VK_LOG=1 is present.
    return (path && *path) || (enabled && std::strcmp(enabled, "1") == 0);
}
}
