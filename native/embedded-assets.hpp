#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdint>
#if defined(FSR4_EMBEDDED_ASSETS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "embedded-asset-index.hpp"
#endif

namespace fsr4assets {
inline constexpr const char* root = "__fsr4_embedded__";
inline bool is_embedded(const std::filesystem::path& path) {
#if defined(FSR4_EMBEDDED_ASSETS)
    return !path.empty() && *path.begin() == root;
#else
    (void)path;
    return false;
#endif
}
inline std::vector<std::uint8_t> read(const std::filesystem::path& path) {
#if defined(FSR4_EMBEDDED_ASSETS)
    const auto key = path.lexically_relative(root).generic_string();
    for (const auto& entry : embedded_index) {
        if (key != entry.path) continue;
        HMODULE module{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&read), &module))
            throw std::runtime_error("cannot locate embedded asset module");
        const auto resource = FindResourceW(module, MAKEINTRESOURCEW(entry.id), MAKEINTRESOURCEW(10));
        if (!resource || SizeofResource(module, resource) != entry.size)
            throw std::runtime_error("missing or incorrectly sized embedded asset: " + key);
        const auto loaded = LoadResource(module, resource);
        const auto bytes = static_cast<const std::uint8_t*>(LockResource(loaded));
        if (!bytes) throw std::runtime_error("cannot load embedded asset: " + key);
        return {bytes, bytes + entry.size};
    }
#endif
    throw std::runtime_error("unknown embedded asset: " + path.generic_string());
}
}
