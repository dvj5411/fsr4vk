#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sdk_root="${VULKAN_SDK:?Set VULKAN_SDK to a Vulkan SDK root containing include/vulkan}"
output_dir="${FSR4_PROVIDER_OUTPUT_DIR:-$repo_root/build/provider-windows}"
asset_root="${FSR4_EMBED_ASSETS:-$repo_root/assets/general}"
mkdir -p "$output_dir"
vulkan_library=(-lvulkan-1)
if [[ -n "${FSR4_VULKAN_IMPORT_LIBRARY:-}" ]]; then
    vulkan_library=("$FSR4_VULKAN_IMPORT_LIBRARY")
fi
python3 "$repo_root/tools/embed-general-assets.py" "$asset_root" "$output_dir/embedded"
x86_64-w64-mingw32-windres \
    "$output_dir/embedded/embedded-assets.rc" "$output_dir/embedded-assets.o"
embedded_args=(-DFSR4_EMBEDDED_ASSETS -I"$output_dir/embedded" "$output_dir/embedded-assets.o")
x86_64-w64-mingw32-g++ -std=c++20 -O2 -shared -static \
    -Wall -Wextra -Wno-missing-field-initializers \
    -I"$sdk_root/include" "$repo_root/provider/ffx_vk_provider.cpp" "${embedded_args[@]}" \
    -Wl,-Bdynamic "${vulkan_library[@]}" -Wl,-Bstatic -o "$output_dir/amd_fidelityfx_upscaler_vk.dll"
cp "$output_dir/amd_fidelityfx_upscaler_vk.dll" "$output_dir/amd_fidelityfx_vk.dll"
echo "$output_dir/amd_fidelityfx_upscaler_vk.dll"
echo "$output_dir/amd_fidelityfx_vk.dll"
