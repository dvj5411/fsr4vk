# Windows field-log fixes (0.3.2.3)

## Endfield: context creation

The supplied log creates an inverted-depth HDR context with AutoExposure=false,
1704x958 rendering into 2560x1440. The old exact-flags check required automatic
exposure and rejected this before dispatch. The provider now permits external
exposure and uses a one-invocation compute adapter to copy its first texel into
the existing model exposure input. As in AMD's source, zero becomes 1.0; nonzero
values are preserved. The existing model applies pre-exposure division once.
No automatic luminance exposure is substituted for the game's supplied value.

Other unsupported permutations are still rejected. External exposure currently
requires the existing 1x1 R32_FLOAT compute-read resource contract.

## NMS: upstream PR device features

The NMS log loads the DLL, creates a context, and stops inside its first dispatch
at 1712x960 -> 2560x1440. It does not contain a provider error, crash stack, or
proof of the exact driver fault. The logged device list omits
VK_KHR_compute_shader_derivatives, despite that extension being advertised by
the RX 7900 XT. The device is therefore not valid for our shader requirements.

Treat the DLL as a likely PR #1161 build, not as commit 8e50f8eb merely because
that stale version string is printed. The reference source used for the patch
is PR head b063b22f667b83aff24e02d60b233670add185dc.

`optiscaler-pr1161-device-features.patch` adds missing capabilities within the
PR's existing framework: mutable descriptors, linear compute derivatives,
variable descriptor counts, nonuniform descriptor indexing where supported,
integer dot products, formatless storage-image reads, and Vulkan memory-model
device scope. The optional native mixed-dot backend also enables VALVE mixed
dot and float-controls2. Existing core-version structures are reused, supported
bits are queried, and application-owned feature values are restored afterward.

Our first patched-PR GPU run exposed VUID-RuntimeSpirv-vulkanMemoryModel-06265:
the PR enables Vulkan memory model but omitted device scope. That was corrected
before acceptance; subsequent runs pass validation. No upstream PR/comment was
created. The two-file feature patch is independent of the preset-control patch.
It must be built into the PR host to fix its device setup; replacing the provider
after vkCreateDevice cannot enable missing features retroactively.

The release keeps the prior personal OptiScaler binary, whose provider-owned
negotiation is separate from this upstream proposal. It is not presented as a
new build of PR #1161. Native Windows Microsoft Store retesting remains necessary.

## Validation summary

Synthetic W6400/RADV tests pass for both logged dimensions: NMS automatic
exposure and Endfield external exposure values 0, 0.5, 1 and 2. Native and
self-contained Windows/Proton outputs match byte-for-byte; the portable INT8
backend also passes. All six model presets pass external-exposure checks at
1080p and 2160p capacity tiers with Vulkan validation enabled. The final release
DLL passes the five Windows/Proton cases again, plus an injected missing-command
test that produces the named error through the FFX callback. CPU tests cover
context acceptance, unsupported permutations and feature-chain preservation.
These are functional checks, not native Windows game runs or fresh DX12-oracle
RMSE measurements. Raw synthetic test logs remain in the private research repo;
the supplied third-party Windows logs are not included in this public repository.

## Diagnostics and preset evidence

Embedded Windows builds now default to `%TEMP%/fsr4vk-provider-<pid>.log`, avoiding
writes inside protected WindowsApps directories. FSR4_VK_LOG_PATH still overrides
this. Context requests, model creation boundaries, and rejection reasons are
recorded. Context errors also reach the FFX message callback when supplied.

`FSR4VK_ERROR_MISSING_DEVICE_FEATURES` identifies unavailable required device
commands and VK_ERROR_FEATURE_NOT_PRESENT / VK_ERROR_EXTENSION_NOT_PRESENT,
with instructions to enable capabilities before vkCreateDevice. OOM and other
runtime errors are not mislabeled. A first-use physical capability preflight
uses the separate `FSR4VK_ERROR_DEVICE_CAPABILITY_CHECK` diagnostic for missing
GPU/driver support. Neither a non-null command pointer nor successful physical
feature queries prove that all logical-device feature bits were enabled.
Vulkan has no general post-creation enabled-feature query, so missing shader-only
bits such as computeDerivativeGroupLinear cannot be reliably detected by a
drop-in provider without host cooperation. The PR patch fixes those at creation;
the provider logs this limitation rather than claiming a full enabled-bit audit.

AMD's 4.0.2 source in ffx_provider_fsr4_dx12.cpp selects modelPreset using
maxUpscaleSize.width / maxRenderSize.width (1.49, 1.69, 1.99, 2.99 thresholds),
and reevaluates from upscaleWidth / renderSize.width during dispatch, retaining
DRS when selected. Capacity tiers are a separate resolution-dependent choice.
No resolution-only model-selection change is justified by these logs.

AMD's sample documentation describes the named quality presets as scaling
factors: https://gpuopen.com/manuals/fidelityfx_sdk2/samples/super-resolution/
This is evidence about the 4.0.2 path, not a claim about every newer model version.
