# General-resolution research candidate

The provider uses capacity/preset-specific `general/` bundles instead of the
retired fixed 720p -> 1080p Quality-only runtime path.

## Supported contract

- Input and output dimensions are integers, at least 64 on each axis.
- Output is at most 3840x2160; input must not exceed output on either axis.
- Equal input/output is supported (FSRAA). Non-preset and odd dimensions are
  accepted; internal history and dispatch extents are rounded up to 8 pixels.
- Capacity-specific shader bundles cover up to 1920x1080 and up to 3840x2160.
  Presets are Native AA, Quality, Balanced, Performance, Ultra Performance, and
  preset 4 (DRS). Set `FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION` at context creation
  to select DRS and retain it across render-size changes. Without this flag,
  the existing ratio-based model selection applies.
- The current captured permutation requires inverted depth and auto exposure,
  low-resolution motion vectors, no sharpening, and no optional masks. Existing
  Vulkan device requirements and single-queue/lifetime rules still apply.
- Invalid or non-positive game pre-exposure values are treated as neutral 1.0;
  a preset change must not fail solely because that transient value is zero.
- Numerical validation uses RGBA16F color/output, R32F depth and RG16F motion.
  Existing packed game-format handling remains, but this new size matrix does
  not establish packed-format accuracy at every size.

The selector follows the observed original DLL: aligned output width divided
by input width, with FP32 thresholds 1.49, 1.69, 1.99 and 2.99. SDK source has
different Quality thresholds at creation and dispatch. A 900x500 -> 1344x752
capture identifies all 14 model/pre/post shaders as Quality on both frames;
using 1.50 instead caused RMSE approximately 0.0116. Shader arithmetic and the
numerical acceptance thresholds were not changed to conceal that failure.

Each preset is allocated lazily and retained until context destruction to avoid
destroying resources referenced by pending command buffers. Switching presets
or output extent resets history. Using all six presets in a large context can
consume substantial VRAM; this is not a memory/performance optimization release.
The DRS synthetic tests include two-frame changes of render size in one context.
They do not establish long-sequence stability or dynamic-resolution game support.
See `provider/DRS.md` for the preset-4 validation scope.

## Private asset layout

Set `FSR4_VK_ASSET_ROOT` to a directory containing:

```
general/
  1080/{native,quality,balanced,performance,ultraperf,drs}/
  2160/{native,quality,balanced,performance,ultraperf,drs}/
```

Every leaf contains `pass-00.spv` through `pass-14.spv`, `initializers.bin`,
`weights.bin`, and a SHA-256 manifest. Pass 00 is auto exposure; the other 14
are pre, twelve model passes, and post. The release DLL embeds these assets; this layout is for development builds.
A non-embedded DLL searches for
`fsr4-vulkan-assets/general/` beside itself.
Model/shader assets belong to this private research archive, not the independent
clean provider framework or the public OptiScaler fork.

## Reproduction

On the W6400 use `tools/capture-general-bundles.py` to capture the twelve coherent
bundles, `tools/collect-resolution-edges.py` to collect seeded original references,
and `tools/validate-general-matrix.py` with the standard and edge case manifests.
All GPU processes have timeouts and candidate outputs are not overwritten.
`native/resolution-plan-test.cpp` checks CPU dispatch geometry and bounds.

The acceptance gate is unchanged: no nonfinite pairs, RMSE <= 0.002, and at most
5% of component differences above 0.001. These are two-frame synthetic oracle
comparisons, not perceptual quality guarantees. Extreme magnification matching
the original does not mean it produces a useful image. The scene uses fixed
pixel geometry, so tiny inputs omit some features.
