#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sdk_root="${VULKAN_SDK:?Set VULKAN_SDK to a Vulkan SDK root containing include/vulkan}"
output_dir="${FSR4_PROVIDER_OUTPUT_DIR:-$repo_root/build/provider-windows}"
export FSR4_EMBED_ASSETS="${FSR4_EMBED_ASSETS:-$repo_root/assets/general}"
mkdir -p "$output_dir"
vulkan_library=(-lvulkan-1)
if [[ -n "${FSR4_VULKAN_IMPORT_LIBRARY:-}" ]]; then
    vulkan_library=("$FSR4_VULKAN_IMPORT_LIBRARY")
fi
build_provider() {
    local mode="$1" name="$2" compression=none
    local -a embedded_args=() mode_args=(-g)
    if [[ "$mode" == release ]]; then
        compression=zstd
        mode_args=(-ffunction-sections -fdata-sections -Wl,--gc-sections)
    fi
    if [[ -n "${FSR4_EMBED_ASSETS:-}" ]]; then
        local embed_dir="$output_dir/$mode-embedded"
        python3 "$repo_root/tools/embed-general-assets.py" "$FSR4_EMBED_ASSETS" "$embed_dir" --compression "$compression"
        x86_64-w64-mingw32-windres "$embed_dir/embedded-assets.rc" "$output_dir/$mode-assets.o"
        embedded_args=(-DFSR4_EMBEDDED_ASSETS -I"$embed_dir" "$output_dir/$mode-assets.o")
        if [[ "$mode" == release ]]; then
            x86_64-w64-mingw32-gcc -O2 -DNDEBUG -ffunction-sections -fdata-sections \
                -c "$repo_root/third_party/zstd/zstddeclib.c" -o "$output_dir/zstd-decoder.o"
            embedded_args+=(-DFSR4_COMPRESSED_ASSETS "$output_dir/zstd-decoder.o")
            mkdir -p "$output_dir/LICENSES"
            cp "$repo_root/third_party/zstd/LICENSE" "$output_dir/LICENSES/Zstandard-BSD.txt"
        fi
    fi
    x86_64-w64-mingw32-g++ -std=c++20 -O2 -shared -static \
        -Wall -Wextra -Wno-missing-field-initializers "${mode_args[@]}" \
        -I"$sdk_root/include" "$repo_root/provider/ffx_vk_provider.cpp" ${embedded_args[@]+"${embedded_args[@]}"} \
        -Wl,-Bdynamic "${vulkan_library[@]}" -Wl,-Bstatic -o "$output_dir/$name"
    if [[ "$mode" == release ]]; then
        x86_64-w64-mingw32-strip --strip-unneeded "$output_dir/$name"
    fi
}
# Independent builds: debug retains raw resources, compiler debug information
# and symbols. Never derive it by renaming a stripped/compressed binary.
build_provider debug amd_fidelityfx_upscaler_vk_debug.dll
build_provider release amd_fidelityfx_upscaler_vk.dll
cp "$output_dir/amd_fidelityfx_upscaler_vk.dll" "$output_dir/amd_fidelityfx_vk.dll"
x86_64-w64-mingw32-g++ -std=c++20 -O2 -static \
    -I"$sdk_root/include" "$repo_root/probes/vulkan-provider-abi-probe.cpp" \
    -o "$output_dir/vulkan-provider-abi-probe.exe"
x86_64-w64-mingw32-g++ -std=c++20 -O2 -static \
    -I"$sdk_root/include" "$repo_root/probes/vulkan-device-negotiation-probe.cpp" \
    -Wl,-Bdynamic "${vulkan_library[@]}" -Wl,-Bstatic \
    -o "$output_dir/vulkan-device-negotiation-probe.exe"
echo "$output_dir/amd_fidelityfx_upscaler_vk.dll"
echo "$output_dir/amd_fidelityfx_vk.dll"
x86_64-w64-mingw32-g++ -std=c++20 -O2 -static -DFSR4_EXTERNAL_PROVIDER \
    -I"$sdk_root/include" "$repo_root/native/dispatch-smoke.cpp" \
    -Wl,-Bdynamic "${vulkan_library[@]}" -Wl,-Bstatic -o "$output_dir/provider-dispatch-smoke.exe"
