#!/usr/bin/env bash
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sdk_root="${VULKAN_SDK:-$repo_root/.runtime/vulkan-sdk/1.4.357.1/x86_64}"
output_dir="${FSR4_PROVIDER_OUTPUT_DIR:-$repo_root/build/provider-windows}"
mkdir -p "$output_dir"
# Serialize builds sharing an output directory. Distinct directories remain
# independent. Never remove another build's lock or overwrite its live outputs.
lock_dir="$output_dir/.provider-build-lock"
if ! mkdir "$lock_dir" 2>/dev/null; then
    echo "Provider build lock exists: $lock_dir (inspect the owner; no automatic deletion)" >&2
    exit 1
fi
trap 'rmdir "$lock_dir"' EXIT
stage_dir="$(mktemp -d "$output_dir/frozen-XXXXXXXX")"
freeze_args=(--vulkan-include "$sdk_root/include"
    --tool CXX=x86_64-w64-mingw32-g++ --tool CC=x86_64-w64-mingw32-gcc
    --tool RC=x86_64-w64-mingw32-windres --tool STRIP=x86_64-w64-mingw32-strip
    --tool "ZSTD=${ZSTD:-zstd}")
if [[ -n "${FSR4_EMBED_ASSETS:-}" ]]; then
    freeze_args+=(--assets "$FSR4_EMBED_ASSETS")
fi
python3 "$repo_root/tools/freeze-provider-inputs.py" "$repo_root" "$stage_dir" "${freeze_args[@]}"
source_root="$stage_dir"
frozen_assets=""
if [[ -n "${FSR4_EMBED_ASSETS:-}" ]]; then frozen_assets="$stage_dir/assets/general"; fi
build_dir="$stage_dir/artifacts"
mkdir "$build_dir"
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
        local embed_dir="$build_dir/$mode-embedded"
        python3 "$source_root/tools/embed-general-assets.py" "$frozen_assets" "$embed_dir" --compression "$compression"
        x86_64-w64-mingw32-windres "$embed_dir/embedded-assets.rc" "$build_dir/$mode-assets.o"
        embedded_args=(-DFSR4_EMBEDDED_ASSETS -I"$embed_dir" "$build_dir/$mode-assets.o")
        if [[ "$mode" == release ]]; then
            x86_64-w64-mingw32-gcc -O2 -DNDEBUG -ffunction-sections -fdata-sections \
                -c "$source_root/third_party/zstd/zstddeclib.c" -o "$build_dir/zstd-decoder.o"
            embedded_args+=(-DFSR4_COMPRESSED_ASSETS "$build_dir/zstd-decoder.o")
            mkdir -p "$build_dir/LICENSES"
            cp "$source_root/third_party/zstd/LICENSE" "$build_dir/LICENSES/Zstandard-BSD.txt"
        fi
    fi
    x86_64-w64-mingw32-g++ -std=c++20 -O2 -shared -static \
        -Wall -Wextra -Wno-missing-field-initializers "${mode_args[@]}" \
        -I"$stage_dir/sdk-include" "$source_root/provider/ffx_vk_provider.cpp" ${embedded_args[@]+"${embedded_args[@]}"} \
        -Wl,-Bdynamic "${vulkan_library[@]}" -Wl,-Bstatic -o "$build_dir/$name"
    if [[ "$mode" == release ]]; then
        x86_64-w64-mingw32-strip --strip-unneeded "$build_dir/$name"
    fi
}
# Independent builds: debug retains raw resources, compiler debug information
# and symbols. Never derive it by renaming a stripped/compressed binary.
build_provider debug amd_fidelityfx_upscaler_vk_debug.dll
build_provider release amd_fidelityfx_upscaler_vk.dll
cp "$build_dir/amd_fidelityfx_upscaler_vk.dll" "$build_dir/amd_fidelityfx_vk.dll"
x86_64-w64-mingw32-g++ -std=c++20 -O2 -static \
    -I"$stage_dir/sdk-include" "$source_root/probes/vulkan-provider-abi-probe.cpp" \
    -o "$build_dir/vulkan-provider-abi-probe.exe"
x86_64-w64-mingw32-g++ -std=c++20 -O2 -static \
    -I"$stage_dir/sdk-include" "$source_root/probes/vulkan-device-negotiation-probe.cpp" \
    -Wl,-Bdynamic "${vulkan_library[@]}" -Wl,-Bstatic \
    -o "$build_dir/vulkan-device-negotiation-probe.exe"
x86_64-w64-mingw32-g++ -std=c++20 -O2 -static -DFSR4_EXTERNAL_PROVIDER \
    -I"$stage_dir/sdk-include" "$source_root/native/dispatch-smoke.cpp" \
    -Wl,-Bdynamic "${vulkan_library[@]}" -Wl,-Bstatic -o "$build_dir/provider-dispatch-smoke.exe"
# Verify architecture/exports and bind binary/asset/source hashes before publish.
python3 "$source_root/tools/verify-provider-build.py" "$stage_dir" "$build_dir"
for artifact in "$build_dir"/*.dll "$build_dir"/*.exe "$build_dir"/build-provenance.json; do
    cp "$artifact" "$output_dir/$(basename "$artifact").new"
    mv "$output_dir/$(basename "$artifact").new" "$output_dir/$(basename "$artifact")"
done
if [[ -d "$build_dir/LICENSES" ]]; then cp -R "$build_dir/LICENSES" "$output_dir/"; fi
echo "$output_dir/amd_fidelityfx_upscaler_vk.dll"
echo "$output_dir/amd_fidelityfx_vk.dll"
