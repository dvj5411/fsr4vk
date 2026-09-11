# Portable INT8 provider validation

Branch: `codex/nvidia-portable-int8`, based on `63c5beb`.

Target: NVIDIA GeForce RTX 3070 Ti, proprietary driver 610.57.04, Bazzite,
Proton-CachyOS Latest x86_64_v3. The deliverable is a Windows x86-64 DLL,
executed through Proton on the user's NVIDIA machine. Native Windows and Intel
hardware have not been tested.

## Implementation

Devices with the VALVE FP16/FP32 mixed-dot extension and feature retain the
original shader bundles. Other devices select the complete portable INT8
bundle. Its pre-pass uses FP16 products and FP32 accumulation, matching the
working target translation. The remaining neural stages retain packed signed
INT8 dot products. This is not a Tensor Core/cooperative-matrix implementation.

The portable bundle avoids VALVE mixed-dot, NVIDIA raw-access-chain extensions,
and float-controls2. Device preparation therefore adds the VALVE and
float-controls2 requirements only for the original backend. Application-owned
feature chains are preserved, including NMS's dynamic-rendering feature node.
Errors report the rejected structure and the complete bounded feature chain.

All 170 original payloads remain unchanged. The DLL embeds 320 logical paths:
those original payloads plus 150 portable shaders, deduplicated into 291 PE
resources. No shader sidecars, compiler, SDK, environment override or separate
model download is required at runtime. A compatible OptiScaler Vulkan hook
must negotiate the provider requirements before creating the Vulkan device.
The tested installation uses the existing 0.3.2 OptiScaler build.

## Shader provenance and checks

The portable shaders were translated from the same original 4.0.2c DXIL
identities as the retained bundles. `assets/portable/provenance.json` identifies
the source DLL, reference probe and VKD3D binary by SHA-256. Each manifest records
both shader hashes and the corresponding source DXIL hash.

Capture uses `VKD3D_CONFIG=force_raw_va_cbv` to preserve the provider's physical
constant-buffer ABI and disables `VK_NV_raw_access_chains` to avoid a
vendor-specific instruction. These are capture settings, not game launch
requirements. NVIDIA's normal VKD3D float-controls workaround remains active.
See the [upstream driver workaround](https://github.com/HansKristian-Work/vkd3d-proton/blob/master/libs/vkd3d/device.c)
and [VKD3D environment documentation](https://github.com/HansKristian-Work/vkd3d-proton#environment-variables).

The importer verifies matching DXIL identities, descriptor types/bindings,
push-constant layouts, array strides and workgroup modes. Every portable module
passes current Khronos `spirv-val` for Vulkan 1.1 and 1.3. Packaging rechecks all
manifest hashes and ABI comparisons. Device-feature tests cover optional
extensions/features, preservation of the original path and NMS's feature chain.

## Numerical validation

The acceptance gates are unchanged: zero nonfinite pairs, whole-image RMSE
at most 0.002, and at most 5% of values with absolute error above 0.001. All four
channels are included; no image region is excluded. Two frames exercise reset
and moving temporal history for five presets at 1080p and 2160p capacity.

The initial FP32-only pre-pass replacement failed the temporal gate on NVIDIA.
Changing only product precision or deleting float-control annotations did not
resolve it. A complete target-translated bundle passes against the original
DLL with normal NVIDIA driver workarounds. The final DLL does not use the
failed shader-rewrite experiments.

Both the native and embedded Windows suites passed all ten cases (20 frames
each). Worst RMSE was 0.000643988 natively and 0.00195624 through the Windows
DLL. The Windows DLL also passed the 19-frame NMS-format/slot-reuse stress test.
Results and final DLL identity are recorded in
`native/validation-data/nvidia-20260911/`. A limited aggregate gate is not a
claim of pixel identity, long-sequence image quality or benchmark performance.

## NMS test bed

The original provider DLLs, OptiScaler DLL/configuration, graphics settings,
launcher options and saves were backed up before deployment. NMS previously
failed device preparation with `unhandled device pNext sType=1000044003`.
The added `VkPhysicalDeviceDynamicRenderingFeatures` handler allows preparation
to succeed.

Gameplay validation passed on 2026-09-11. The user loaded a save and confirmed
that FSR4 works. Direct observation showed a rendered in-game scene with the
OptiScaler overlay identifying NVIDIA GeForce RTX 3070 Ti and FSR 4.0.2.
Fresh provider logs independently recorded five context/first-dispatch groups
with `shader_backend=portable-int8` and `1280x720 -> 1920x1080`.
The installed DLL SHA-256 matches the reference-tested deliverable:
`2089da4a57dd77b8bc50100153373143853096e57eecf9e57e31c1abbbea679c`.
See `native/validation-data/nvidia-20260911/nms-gameplay.provider.log`.
This establishes successful live game rendering on the tested configuration;
it is not a long-duration stability or image-quality assessment.

The temporary launcher wrapper enabled the opt-in `FSR4_VK_LOG_PATH` diagnostic
for this session. That export has now been removed for subsequent launches,
without interrupting the running game. Steam still references the wrapper,
which preserves the original `PROTON_LOG=1` and
`FSR4_VK_ENABLE_DEVICE_FEATURES=1` launch environment. The backed-up original
launch options can replace the wrapper reference; normal DLL operation does
not require the wrapper or diagnostic logging.

## Reproduction

```sh
python3 tools/test-portable-shaders.py --spirv-val /path/to/spirv-val
c++ -std=c++20 -Wall -Wextra -Wno-missing-field-initializers \
  -I"$VULKAN_SDK/include" native/fsr4-device-features-test.cpp \
  -o /tmp/fsr4-device-features-test
/tmp/fsr4-device-features-test
bash tools/build-vulkan-provider-windows.sh
```

`tools/capture-portable-matrix.py --help` documents the explicit reference
probe, source DLL, Proton and isolated-prefix inputs. Import a completed matrix
with `tools/import-portable-assets.py CAPTURES assets/general NEW_OUTPUT
--spirv-val /path/to/spirv-val`; the importer refuses to overwrite existing
verified bundles. `tools/validate-portable-provider.py --help` documents the
Windows DLL test runner. Build its probe from `native/dispatch-smoke.cpp` with
`FSR4_EXTERNAL_PROVIDER`, link Vulkan, and use static MinGW runtimes.
