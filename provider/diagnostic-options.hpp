#pragma once
#include <cstring>
#include <filesystem>
#include <string>

namespace fsr4vk {
inline bool provider_log_requested(const char* enabled, const char* path) noexcept {
    // An explicit, nonempty path is itself an opt-in. An empty path must not
    // suppress the default location when FSR4_VK_LOG=1 is present.
    return (path && *path) || (enabled && std::strcmp(enabled, "1") == 0);
}

// The host executable identifies the game directory, not the provider DLL or
// current working directory. Keep PID-separated files and a writable fallback
// for protected installations. Call only after logging has been requested.
template<class CanAppend>
inline std::filesystem::path default_provider_log_path(
    const std::filesystem::path& executable, const std::filesystem::path& temporary,
    unsigned long process_id, CanAppend can_append) {
    const auto name="fsr4vk-provider-"+std::to_string(process_id)+".log";
    if(!executable.empty() && !executable.parent_path().empty()) {
        const auto path=executable.parent_path()/name;
        if(can_append(path)) return path;
    }
    if(!temporary.empty()) {
        const auto path=temporary/name;
        if(can_append(path)) return path;
    }
    return {};
}
}
