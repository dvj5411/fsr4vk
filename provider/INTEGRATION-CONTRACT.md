# Current provider integration contract

Updated 2026-10-01. This is the setup/ownership contract, not a claim that every
device/game in the audit's proposed validation matrix has been retested.

## Device creation

Minimum API: Vulkan 1.1 plus the required extension-backed functionality.
Enable before `vkCreateDevice`; physical support queries cannot recover logical
enablement after creation. Generic `ffxQuery` device-requirements prepare/release
preserves the caller's complete chain in owned storage. Unknown structures fail
closed with their sType; they are never silently dropped.

Mandatory unpromoted extensions:

- `VK_EXT_mutable_descriptor_type` / `mutableDescriptorType`.
- `VK_EXT_descriptor_buffer` / `descriptorBuffer`.
- `VK_KHR_compute_shader_derivatives` / `computeDerivativeGroupLinear`.

Mandatory core features: `shaderInt16`, `shaderStorageImageReadWithoutFormat`,
`shaderStorageImageWriteWithoutFormat`. Vulkan 1.2 features: `shaderFloat16`,
`shaderInt8`, `storageBuffer8BitAccess`, `runtimeDescriptorArray`,
`descriptorBindingVariableDescriptorCount`, `bufferDeviceAddress`. Below 1.2,
enable the corresponding `VK_KHR_shader_float16_int8`, `VK_KHR_8bit_storage`,
`VK_EXT_descriptor_indexing`, `VK_KHR_buffer_device_address`, and
`VK_KHR_shader_float_controls` extensions. Vulkan 1.3 features:
`synchronization2`, `shaderIntegerDotProduct`; below 1.3 enable
`VK_KHR_synchronization2` and `VK_KHR_shader_integer_dot_product`.

4.0.2 uses native mixed-dot only if its full optional bundle is usable:
`VK_VALVE_shader_mixed_float_dot_product` with
`shaderMixedFloatDotProductFloat16AccFloat32`, and float-controls2 support and
enablement. Otherwise it uses portable INT8. 4.1.1 uses the portable bundle.
No Cooperative Matrix path exists.

The optional version-1 `ffx_vk_create_device_features.h` query/create descriptor
copies flags from the *successful final* VkDeviceCreateInfo while its storage is
alive. It reports robust-buffer access and native-backend eligibility plus the
effective API and, only if known, recording queue family. Old/other providers
receive no descriptor unless they advertise it. Old hosts keep the established
path when robust/nonrobust descriptor sizes are unambiguous; ambiguous sizing
fails with a specific request for the snapshot rather than guessed addressing.
Unknown queue family disables optional profiling, not the upscaler.

## Input, API, and lifetime

Dimensions are bounded to `64..3840 x 64..2160`, input no larger than output,
with equal-size FSRAA accepted. Inputs match the active render extent exactly;
oversized DRS allocations/subrects are not yet supported. Resources are 2D,
single-layer, non-MSAA, base-mip/base-layer zero. FFX cannot fully attest original
image format, sample count, or subresource identity; the host must preserve that
contract. Original motion format can be supplied through [native inputs](NATIVE-INPUTS.md).
Representable multilayer metadata is rejected; zero depth/mip fields in legacy
host descriptions remain unspecified, not proof of a multilayer image.

Known 4.1.1 limitation: its captured post-pass does not write the unmatched last
row/column when an output dimension is odd. Acceptance of that dimension is not
a guarantee of fully defined edge pixels. Poisoned-output tests reproduced this
on 2026-10-01; 4.0.2 passed the same tests. The shader policy is deliberately
unchanged until an original-provider edge oracle establishes the intended
result. Prefer even output dimensions for 4.1.1 in the meantime. See the
[release verification report](../docs/RELEASE-0.4.3-VALIDATION.md)
for the failing cases; they are not passing regressions.

Input states are COMPUTE_READ; output is UNORDERED_ACCESS. Inverted depth and
low-resolution motion are required. Auto or external R16/R32 exposure is
supported. External exposure samples must be finite/positive; invalid samples
and invalid previous history become neutral 1.0/current exposure respectively.
Jitter and motion-vector scale must be finite. Optional masks are not consumed;
host RCAS is independent. Unsupported key/value configuration returns an error,
not silently successful application. Auto and forced presets 0-5 remain available.

FFX descriptor chains are limited to 64 nodes and must be acyclic. Exported API
exceptions are translated into return codes. Internal CPU calls serialize per
context. This does **not** synchronize GPU submissions: record/submit on one
ordered queue, do not abandon a recorded frame, and finish GPU work before
exclusive context destruction. Callbacks may query, but must not recursively
dispatch or destroy their own context.
Eight descriptor/constant/view slots remain independent; the ninth outstanding
frame is rejected. Preset cores stay alive until context destruction.

## Diagnostics and measurements

Logging remains optional and game-directory placement is unchanged. Negative
event VkResults retain their identity. Opt-in pipeline progress logs name the
pass and CPU creation duration. GPU-memory-usage v1 reports owned VkDeviceMemory
allocation bytes, excluding caller images and driver pipeline/cache internals.

`FSR4_VK_PROFILE_PATH` is now a filename prefix, not an overwritable final-frame
CSV. Each core/session writes `<prefix>.<identity>.jsonl`, with per-frame model,
backend, preset, color, dimensions, queue family, timestamp period/valid bits,
availability, and pass times. Each of eight slots has a separate query range;
collection occurs only after retirement, without WAIT_BIT or queue waits.
Unavailable data is explicit, never zero. `tools/optimization/profiles.py`
accepts these streams and historical CSVs. File I/O/profiling overhead is opt-in
and must be excluded or measured separately in production performance claims.

## Reproduction/builds

Run `python3 tools/test-codebase.py --vulkan-include <include> --opti-reader
<OptiScaler/upscalers/ffx>` for the owned CPU gate. It records source hashes and
retains logs in a fresh build directory; sanitizers are on by default. GPU and
Windows/Proton checks remain separate gates.

Windows builds freeze owned compiler inputs, Vulkan SDK headers and assets into
a unique stage, record compiler/resource/strip/compressor versions and executable
hashes, and lock only
their chosen output directory, validate PE exports/payload hashes and normal/debug
identity, then atomically replace individual output files. A final provenance
manifest identifies the complete verified set; do not consume a set during its
publication. Frozen stages are retained for diagnosis. This does not create a
GitHub release, change exact release notes/readme text, or sync the public repo.
The system compiler/sysroot is version-recorded, not a hermetically frozen
toolchain image. The Linux CPU CI also compiles the Windows-only research
collector with MinGW; a macOS run without --mingw-cxx records that check as
skipped instead of pretending Windows headers are available.
