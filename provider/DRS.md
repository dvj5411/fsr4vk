# Preset 4: dynamic resolution scaling

Preset 4 is the dedicated DRS model. It is distinct from selecting a standard
model using the render/output ratio.

## Activation and behavior

Set `FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION` in `ffxCreateContextDescUpscale.flags`,
together with the existing inverted-depth and auto-exposure flags. The context
then uses `general/<capacity>/drs` for every dispatch. Changing render size keeps
the same model and temporal history; a caller-requested reset or output-size
change still resets history. `maxRenderSize` must cover every dispatched input
size, and `maxUpscaleSize` selects the 1080p or 2160p capacity tier.

Without the dynamic-resolution flag, Native AA, Quality, Balanced, Performance,
and Ultra Performance retain their existing ratio-based selection. There is no
ratio threshold that automatically selects DRS.

The existing resource/flag restrictions still apply: low-resolution motion
vectors, inverted depth, and inputs whose
resource extents match the dispatched render extent. The tests change input
resources after GPU completion while retaining the upscaler context. They do
not establish subrect rendering into oversized input textures, changing output
resolution, or long-running game stability.

Auto and external exposure are supported. Optional reactive/transparency masks
are accepted for host compatibility but not consumed by the current model path;
their presence emits one warning per context instead of failing dispatch.

Preset 4 may also be forced through the optional preset-control descriptor,
independently of the DRS context flag. Auto restores the flag/ratio behavior above.
The matching personal OptiScaler build exposes all six presets. See
`PRESET-CONTROL.md` for the host contract and manual upstream patch.

## Assets and packaging

Both capacity tiers contain a captured 15-pass DRS bundle (auto exposure, pre,
twelve model stages, post), SDK DRS initializers, pass weights, and SHA-256
manifests. Initializers and weights were checked byte-for-byte against this
checkout's SDK source. All ten pre-existing model bundles are retained.

The DRS portable shaders use the same original DXIL identities and descriptor,
push-constant, and workgroup ABI as their native counterparts. They were
captured on the W6400 with `force_raw_va_cbv`, disabling VALVE mixed-dot,
float-controls2, and NVIDIA raw-access chains. Their individual manifests record
this provenance; the old root portable provenance still describes the earlier
five-model NVIDIA capture.

The complete package contains 204 original payload paths and 180 portable
shader paths, deduplicated to 323 embedded PE resources. Runtime shader assets
are read from the DLL resources without extracting files.

## Reproduction

Use an isolated project directory on the capture host, with the original DLL,
SDK, existing Proton prefix, and Vulkan SDK available. Every GPU subprocess is
bounded by the runner's timeout. Use fresh output directories to retain failures.

1. Build `tools/build-provider-probe.sh` and `tools/build-native-dispatch-smoke.sh`.
2. Run `tools/capture-general-bundles.py --presets drs` with the explicit project,
   probe, Vulkan SDK, and `assets/general` output paths.
3. Run `tools/collect-drs-matrix.py` to collect the 30-case original-DLL matrix.
4. Run `tools/validate-general-matrix.py --expected-backend native-mixed-dot`
   using those cases, the native probe, assets, metrics binary, and Vulkan SDK.
5. Capture `tools/capture-portable-matrix.py --presets drs` with the extension
   settings above, then import with `tools/import-portable-assets.py --presets drs`.
   Import into a fresh directory; copy only the two new DRS leaves into the
   existing portable asset tree.
6. Validate all shaders with `tools/test-portable-shaders.py` and test embedding
   with `tools/test-embed-general-assets.py`.
7. On a native-mixed-dot GPU, compile `native/portable-device-mask.cpp` into a
   test-only preload library to mask that capability for native portable tests.
   External test assets need `pass-NN.portable.spv` links in each general bundle
   pointing to the matching `assets/portable/<tier>/drs/pass-NN.spv`. These links
   are development fixtures; the embedded DLL already has those resource paths.
8. Repeat the original-DLL matrix with the portable capture settings. Compare
   retained portable candidate outputs to that matrix with
   `tools/compare-existing-matrix.py --expected-backend portable-int8`.
9. Build with `FSR4_EMBED_ASSETS=<assets/general>` and
   `tools/build-vulkan-provider-windows.sh`. Use
   `tools/validate-portable-provider.py --expected-backend native-mixed-dot`
   (or `portable-int8` on a corresponding device) for the embedded DLL matrix.

The oracle must use the same shader backend as the candidate. Native and
portable arithmetic are not interchangeable numerical references; retain any
cross-backend comparisons as separate evidence. All matched comparisons use
the unchanged gate: no nonfinite pairs, whole-image RGBA RMSE <= 0.002, and at
most 5% of components differing by more than 0.001. No region is excluded.

Measured results, artifact hashes, and hardware limits are in
[`native/validation-data/drs-20260915`](../native/validation-data/drs-20260915/README.md).
