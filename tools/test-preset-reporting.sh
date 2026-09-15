#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
reader_dir="${1:?Pass the patched OptiScaler/upscalers/ffx directory}"
sdk_root="${VULKAN_SDK:?Set VULKAN_SDK to a root containing include/vulkan}"
output_dir="$repo_root/build/preset-reporting-test"
mkdir -p "$output_dir"
cmp "$repo_root/provider/ffx_vk_preset_query.h" "$reader_dir/ffx_vk_preset_query.h"
case "$(uname -s)" in
    Darwin) link_flags=(-Wl,-dead_strip) ;;
    Linux) link_flags=(-Wl,--gc-sections) ;;
    *) echo "Run this CPU-only test on macOS or Linux" >&2; exit 1 ;;
esac
"${CXX:-c++}" -std=c++20 -O1 -g -ffunction-sections -fdata-sections \
    -Wall -Wextra -Wno-missing-field-initializers \
    -I"$sdk_root/include" -I"$reader_dir" \
    "$repo_root/native/preset-reporting-test.cpp" "${link_flags[@]}" \
    -o "$output_dir/preset-reporting-test"
"$output_dir/preset-reporting-test"
