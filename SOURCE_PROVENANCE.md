# Source provenance

## Current release: v0.4

Production provider code is synced from private `fsr4-vulkan-translation`
commit `7824864` (FSR 4.1.1 integration, dual compressed/debug builds, and
the RDR2 external-exposure history fix). Private captures, research tools and
compile-time research-capture hooks are excluded. The public build defaults
to embedding its checked-in assets. The historical notes below describe older
releases and do not identify the current binaries.

4.1.1 INT8 shaders and initializers are translated from the supplied official
FFX 2.3 SDK DLL with SHA-256
`d0dcccc74a43c44ba435b7a369b456e0970d8a4464e4bd683119b374f2c9fb46`.
The `assets/fsr411/` manifests retain capture identities and payload hashes.
The existing 4.0.2 shader/model payloads are unchanged.

The bundled OptiScaler.dll is the repository's Windows Release build from
source commit `59ac04da5bd54bc8ddd84a56059a0a6283c7c198`, pinned by the public
submodule. Build run: https://github.com/dvj5411/OptiScaler/actions/runs/35549757414
(artifact 10617639643). Its SHA-256 is
`59859ac4ff2284c49ad98cf162325e28da542224007566d27f4069a1c8e91668`.
It is byte-identical to the OptiScaler.asi used for the successful W6400 RDR2
test. Neither the earlier PR-author binary nor the fallback DLL is included.

Public-source provider builds:

- Normal: `d8f32376b4cefde8172b41208dc102c61f9e1ec4861f3d6d660d2ba8596bee3c`.
  Lossless Zstandard resources, dead-section removal and stripped symbols.
- Debug: `294651576ae89769257efc4d4290252eef2b6d5edb097698863fce9867bdf0cb`.
  Raw resources, compiler debug information, no section cleanup or stripping.

Both retain all 948 logical assets (553 unique resources); the embedded payloads
were decoded and checked against their manifests. The Zstandard 1.5.7 decoder
is statically linked only into the normal provider; its BSD notice is included.

The user confirmed the prior equivalent uncompressed fix resolves RDR2's
4.1.1 visual corruption, and reported no visual problems in BG3 or NMS.
Those gameplay tests are distinct from final public-build synthetic validation.
Both final public DLLs passed the 11-case W6400 exposure/regression suite:
eight 4.1.1 external-exposure cases matched the original DLL byte-for-byte;
4.1.1 automatic exposure and 4.0.2 automatic/external exposure matched the
previous provider byte-for-byte. No Vulkan validation errors were reported.
CPU decoder corruption/bounds checks, deterministic embedding, 12 captured
dispatch plans, device negotiation, memory-allocation fallback, preset reporting
and ZIP packaging checks also passed. No new native-Windows GPU test is claimed.

## Historical releases

The provider source and build inputs were assembled from the private research
repository `dvj5411/fsr4-vulkan-translation` at commit
`0ef895f3421427c3dd63dd465d3cb0a46489d9da` (production initializer locality for
0.3.2.6, on top of the selected functional changes for 0.3.2.5).
Research capture hooks, throughput experiments, memory-placement experiments,
and private optimization tooling/evidence are excluded; the validated production
initializer allocation policy and its CPU tests are included. Base shader/model payloads
are unchanged; color overlays and the runtime row-bounds guard are included.

This integrates the Deck fix (`f956eb0`) and the public NVIDIA development
branch at `da7efe5` (implementation `763a35e`). Portable shader capture identities
are recorded in `assets/portable/provenance.json`. Original payloads are unchanged.
Preset 4 DRS preparation comes from `bae0829`, with matched native, portable and
Windows/Proton evidence in `native/validation-data/drs-20260915`. Release 0.3.2.2
adds six-preset control/reporting and a CPU active-core map-lookup bypass.
Combined functional checks are in `native/validation-data/release-0322`.
Release 0.3.2.3 adds external-exposure input support and identifiable missing
Vulkan device-feature diagnostics. Synthetic native and Windows/Proton checks,
including the expected missing-command rejection, are summarized in
`provider/WINDOWS-FIELD-FIXES.md`. Raw evidence remains in the private research
repository's `native/validation-data/windows-field-20260916` directory.
The scalar-spill optimization at private research commit `7aa4a0e` is paused and
excluded from this release.

The minimum AMD FFX API headers and the model/shader material derive from AMD's
FidelityFX SDK commit `01446e6a74888bf349652fcf2cbf5f642d30c2bf`
(`AMD FidelityFX SDK 2.0.0`). The license from that snapshot is preserved at
`LICENSES/AMD-FidelityFX-SDK-MIT.md`.

The OptiScaler submodule is pinned to modified-source commit
`f52646c3e440d3c7dc1a05ce0a77f30ab68f6dfa`. Its GPLv3 license is preserved at
`LICENSES/OptiScaler-GPL-3.0.txt`.

The v0.3.2.4 archive contains the exact embedded provider tested on W6400 through
GE-Proton11-6, with optional-mask acceptance and opt-in file logging. The runtime
shader/model resources are unchanged. Ten synthetic GPU cases and six logging
integration cases passed; the user subsequently confirmed RDR2 launches. No new
Deck, NVIDIA or native-Windows validation is claimed.

The v0.3.2.4 OptiScaler DLL was supplied prebuilt by the PR #1161 author. The
project maintainer reports permission to redistribute it. It is byte-identical
to the author's ASI build used in the RDR2 test. Exact corresponding source and
commit identification have not been supplied yet. The older pinned submodule
above is retained for historical integration work and **does not reproduce this
binary**. No source correspondence or reproducible-build claim is made for it.
The parked local RDR2 BDA workaround is not included in this binary.
See `provider/RDR2-COMPATIBILITY.md` for validation boundaries.

- `amd_fidelityfx_upscaler_vk.dll`:
  `9d8e5489370ffd5f593136b89957c0761a22be7697dd8075f67ffac98d2d6120`
- `OptiScaler.dll`:
  `96b9fcf18bbeea3a14d970cafceb00efb91670b374fd1a05b86c74d20299af2a`

## Release 0.3.2.5

`OptiScaler_fallback.dll` restores the custom binary from 0.3.2.2/0.3.2.3,
SHA-256 `0547cf39a65d9eff108ff3efb519d53cf07d186387cefb7de68fe01c62dd9fb6`.
Its matching source is the pinned OptiScaler submodule commit
`f52646c3e440d3c7dc1a05ce0a77f30ab68f6dfa`.
The PR #1161 author's binary from 0.3.2.4 is not bundled in 0.3.2.5.

The provider is packaged at `OptiScaler/amd_fidelityfx_upscaler_vk.dll`.
Color-overlay manifests record hashes and their relationship to the unchanged
linear shaders. The normal capability-selected build is used; the test-only
forced-portable option is disabled. See `provider/COLOR-SPACES.md` for retained
GPU evidence, numerical exceptions, and hardware-validation limits.

0.3.2.5 provider SHA-256:
`39733a9f6b4d3e99485eb8de570e779e02112d794afdbe9f5a7d870ca27e0da2`.

## Release 0.3.2.6

Adds automatic device-local initializer preference and safe allocation/mapping
fallback. See `provider/INITIALIZER-LOCALITY.md` for validation and measured scope.
The normal capability-selected provider is used, not the forced-portable test DLL.
Shader/model assets and the packaged `readme.txt` remain unchanged from 0.3.2.5.

Both OptiScaler binaries are bundled unchanged:

- `OptiScaler.dll`: the PR-author binary previously shipped in 0.3.2.4,
  SHA-256 `96b9fcf18bbeea3a14d970cafceb00efb91670b374fd1a05b86c74d20299af2a`.
  The corresponding-source limitation stated above still applies.
- `OptiScaler_fallback.dll`: the custom fallback from 0.3.2.5,
  SHA-256 `0547cf39a65d9eff108ff3efb519d53cf07d186387cefb7de68fe01c62dd9fb6`.
  Corresponding source remains the pinned OptiScaler submodule.

Provider SHA-256:
`0693a041de57d81dd491a6a4a580ab278c3afbc81f7be3a206247206fc2fe85c`.
This public-source rebuild matches every runtime PE section of the NMS-tested
private DLL after normalizing image-base relocations and the export timestamp;
embedded asset manifests are identical. Private tested DLL SHA-256:
`b9e4f6ce1485ba100c4efb5a6d8095213683dac7191dfb6ea0a26aafeca29ad5`.
