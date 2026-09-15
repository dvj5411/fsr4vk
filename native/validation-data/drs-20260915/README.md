# Preset 4 (DRS) validation — 2026-09-15

Implementation branch: `codex/preset-4-drs`, based on `c1e0c7d`.
Capture/test host: AMD Radeon Pro W6400, RADV NAVI24, Mesa 26.2.2,
Vulkan SDK 1.4.357.1; Windows DLL executed through GE-Proton11-5.

## Matched-backend results

| Suite | Cases | Frames | Worst RMSE | Worst fraction above 0.001 |
| --- | ---: | ---: | ---: | ---: |
| Native mixed-dot DRS vs original mixed-dot DLL | 30 | 60 | 0.00162214 | 0.0170615 |
| Portable INT8 DRS vs original portable DLL | 30 | 60 | 0.00193283 | 0.0171732 |
| Embedded Windows DLL: DRS plus existing-preset regressions | 20 | 40 | 0.00166470 | 0.0165146 |

All cases pass the unchanged gate: no nonfinite pairs, whole-image RGBA RMSE
<= 0.002, and <= 5% of components differing by more than 0.001. Vulkan validation
was enabled, and candidate runs reject validation errors and VUID messages.
These are aggregate numerical gates, not claims of pixel identity or perceptual
quality. Several 4K cases are bit-exact on both frames.

Each 30-case DRS suite covers five ratios at 720p, 1080p, 1440p, and 2160p output,
four odd/boundary/minimum-size cases, and six render-size transitions. Transitions
include growth and shrinkage at 1080p and 4K, plus Quality/Balanced boundary
crossings. Each transition retains one upscaler context and uses reset=false on
the second frame. Input textures are recreated after GPU completion.

The Windows suite covers all five existing presets at both capacity tiers
(10 regression cases), plus two DRS fixed-size, two odd-size, and all six DRS
transition cases. The DLL had no asset-root override or adjacent shader assets;
its test harness reads independent fixtures passed on the command line. No
assets were extracted beside the DLL. Both DLL filenames are byte-identical.
The Windows ABI probe returned success. Native portable tests use the test-only
`native/portable-device-mask.cpp` preload shim; each saved diagnostic explicitly
records `preset=drs` and `shader_backend=portable-int8`.

## Cross-backend limit

`portable-vs-native.json` retains the earlier comparison of portable candidate
outputs to the mixed-dot original reference. **That comparison fails:** only
4/30 cases pass, with worst RMSE 0.00649351 and worst above-tolerance fraction
0.414928. Native and portable arithmetic are not numerically interchangeable.

The same retained portable candidate images pass all 30 cases against fresh
original-DLL references using the identical portable translation settings. This
is the backend-specific oracle method used for the existing NVIDIA workflow;
no threshold, image region, shader arithmetic, or candidate output was changed
to obtain the matched results. Passing does not establish cross-backend parity.

## Shader/model and packaging checks

- Both native DRS bundles contain 15 captured passes, DRS initializers, and the
  256-word DRS pass-weight buffer. Model data matches the local SDK byte-for-byte.
- All 30 native DRS modules pass SPIR-V validation for Vulkan 1.1 and 1.3.
- All 180 portable modules pass those same targets. New DRS modules retain the
  original DXIL identities and descriptor/push-constant/workgroup ABI, without
  mixed-dot, NVIDIA raw-access-chain, or float-controls2 capabilities.
- All ten existing model bundles remain unchanged.
- Packaging verifies 12 bundles, 204 original paths, and 180 portable paths,
  deduplicated into 323 PE resources. Missing DRS manifests and corrupted DRS
  weights fail packaging; generated indexes are deterministic.
- CPU resolution-selection and existing Vulkan 1.1 promoted-command,
  feature-chain, and shader-envelope tests pass.

## Deliverable and provenance

Prepared Windows provider: `build/drs-windows-embedded/amd_fidelityfx_upscaler_vk.dll`
and its `amd_fidelityfx_vk.dll` alias, relative to the project root.

SHA-256 (both names):
`171fc2f8675c0bf144239b6ab27954c25ddb25c8e28e592882592cf488548e37`

`build-provenance.json` records source hashes, compiler, artifact sizes/hashes,
and embedding counts. `native-oracle-provenance.json` and
`portable-oracle-provenance.json` identify the original DLL and reference probe;
`portable-shader-provenance.json` additionally pins the translator and settings.
Per-file native and portable shader hashes live in the asset manifests.

`native-matched.json`, `portable-matched.json`, and `windows-matched.json` contain
all per-frame metrics. Case manifests preserve exact dimensions and reference
paths; portable matched results also identify the retained candidate paths.
`native-provider.log`, `portable-logs/`, and `windows-logs/` retain dispatch and
backend evidence. Archived text logs normalize line endings and trailing whitespace;
raw logs remain on the host. Raw oracle/candidate images and GFXReconstruct streams remain
in the isolated `/home/admin/fsr4-drs-20260915` directory on the capture host.

## Scope

This establishes preset preparation and synthetic provider validation on the
W6400/Proton configuration. DRS was not tested in a game, on NVIDIA/Steam Deck,
or on native Windows in this run. Windows embedded tests exercise the native
mixed-dot path; the portable path was numerically tested with the native-linked
provider on the W6400 using capability masking. Long sequences, oversized input
texture subrects, output-size changes, and packed-format DRS were not tested.

Activate DRS with `FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION` at context creation.
The current OptiScaler Vulkan menu does not expose that flag; host/UI integration
is outside this preset-preparation change. See [provider/DRS.md](../../../provider/DRS.md)
for the contract and reproduction tools.
