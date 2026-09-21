# fsr4vk

`fsr4vk` is an experimental native Vulkan FFX upscaler provider for the
FSR 4.0.2c INT8 model, together with the OptiScaler integration used to load it.
It is an independent research project and is not an AMD product or an official
OptiScaler release.

The repository is intentionally small: it contains the provider source, the
verified runtime payloads embedded into the provider DLL, the minimum AMD FFX
API headers needed by that source, and a pinned OptiScaler source submodule.

This repository is mostly maintained by AI. It periodically syncs with the upstream private repository that I actually use for development.

> **Warning:** This is experimental software. Back up replaced files, do not use
> it with online or anti-cheat-protected games, and expect compatibility issues.

## AI Disclosure

This project has been heavily assisted by AI models such as GPT-5.6 Sol and 6 Astra. Most of the tedious work such as individual shader dumping from VKD3D-Proton were performed by AI. 
I'm not going to pretend it wasn't helped by it. However, many parts were still handled by a person.

## Requirements

The provider build currently uses a Unix-like host with:

- Python 3;
- MinGW-w64 (`x86_64-w64-mingw32-g++` and `windres`);
- a Vulkan SDK containing `include/vulkan`; and
- a Windows Vulkan import library (`vulkan-1`) visible to the linker.

Building the custom `OptiScaler.dll` requires Windows, Visual Studio 2022, and
the OptiScaler submodule's recursive dependencies. Releases include this custom
build as `OptiScaler_fallback.dll` for Doom TDA and preset selection.

## Clone

```sh
git clone --recurse-submodules https://github.com/dvj5411/fsr4vk.git
cd fsr4vk
```

If the repository was cloned without submodules:

```sh
git submodule update --init --recursive
```

## Build the Vulkan provider

Set `VULKAN_SDK` to the SDK root. If the Vulkan import library is not in the
compiler's search path, set `FSR4_VULKAN_IMPORT_LIBRARY` to its full path.

```sh
export VULKAN_SDK=/path/to/vulkan-sdk
export FSR4_VULKAN_IMPORT_LIBRARY=/path/to/libvulkan-1.a
./tools/build-vulkan-provider-windows.sh
```

The script verifies all twelve original and portable shader/model bundles, embeds them, and
writes the self-contained provider:

```text
build/provider-windows/amd_fidelityfx_upscaler_vk.dll
```

Set `FSR4_PROVIDER_OUTPUT_DIR` to change the output directory or
`FSR4_EMBED_ASSETS` to build with another verified `assets/general` tree and
its sibling `assets/portable` and `assets/colors` trees. The portable INT8 backend is selected
automatically when the VALVE mixed-dot feature is unavailable. Initial NVIDIA
validation covers the RTX 3070 Ti through Proton, not native Windows NVIDIA.

Release 0.3.2.1 also preserves NMS dynamic-rendering device features on Steam
Deck and removes the obsolete fixed-resolution provider fallback. The scalar-spill
optimization remains a separate research experiment and is not included.

Release 0.3.2.2 adds Preset 4 (DRS), optional forced selection of presets 0–5,
and reporting of the last successfully dispatched preset. Auto follows the
context's DRS flag or upscale ratio. The minor optimisation is a CPU-side
active-model lookup bypass; shader spill experiments remain excluded.
See [preset control](provider/PRESET-CONTROL.md) and the
[manual upstream OptiScaler patch](provider/optiscaler-preset-control-upstream.patch).

Release 0.3.2.3 accepts contexts with auto exposure disabled and consumes the
game's external exposure input. It also adds specific diagnostics for detectable
missing Vulkan device-feature/command failures. Shader-only enabled feature bits
cannot generally be queried after device creation: the host must still enable
the complete feature set. See the [Windows field diagnosis and validation
limits](provider/WINDOWS-FIELD-FIXES.md) and the separate
[manual PR #1161 device-feature patch](provider/optiscaler-pr1161-device-features.patch).
The bundled custom OptiScaler DLL is unchanged from 0.3.2.2; it is not a new build
of upstream PR #1161.

Release 0.3.2.4 adds initial RDR2 support: supplied optional reactive/composition
masks no longer abort dispatch. They are accepted but not consumed by the current
model path. Provider file logging is now opt-in with `FSR4_VK_LOG=1` or a nonempty
`FSR4_VK_LOG_PATH`; host error callbacks remain active. The release bundles the
PR #1161 author's supplied OptiScaler binary, not a build of our older submodule.
Its exact corresponding source has not been supplied yet. See
[RDR2 validation and limitations](provider/RDR2-COMPATIBILITY.md).

Release 0.3.2.6 adds automatic device-local initializer placement with safe
fallback. W6400 synthetic upscale-time reductions measured 4.35% at 1080p and
7.09% at 1440p; these are not whole-game FPS guarantees. No new launch argument
or OptiScaler change is needed. See [initializer locality](provider/INITIALIZER-LOCALITY.md).
The archive includes both the PR-author `OptiScaler.dll` previously shipped in
0.3.2.4 and the custom `OptiScaler_fallback.dll` from 0.3.2.5, unchanged.

Release 0.3.2.5 adds generic non-linear, PQ, and sRGB color modes, the model-11
row-bounds correctness fix, and game-local opt-in diagnostics. It restores the
custom OptiScaler as `OptiScaler_fallback.dll` and places the provider in
`OptiScaler/` inside the ZIP. See [color-space support and validation](provider/COLOR-SPACES.md).

## Build OptiScaler

Open `optiscaler/OptiScaler.sln` in Visual Studio 2022 and build the `Release`
configuration for `x64`. The unsigned DLL is normally written to:

```text
optiscaler/x64/Release/a/OptiScaler.dll
```

## Install

Start with an existing working OptiScaler 10.0 nightly installation.

1. Close the game and back up its existing DLLs.
2. Extract all ZIP contents into the root of the game files containing that
   OptiScaler installation. The provider is placed in `OptiScaler/` automatically.
3. If you need the fallback, rename `OptiScaler_fallback.dll` to your existing
   OptiScaler filename and replace it. For an ASI installation, use `OptiScaler.asi`
   and retain the working loader and overrides. Do not load both forms simultaneously.
4. Remove `FSR4_VK_ENABLE_DEVICE_FEATURES=1` from the launch arguments if it was
   added for an older build. It is redundant and is not read by the current
   provider. The host must enable the required features before device creation.
5. Select the FSR 3.X/FFX backend and then the FSR 4.0.2c Vulkan provider.

There is currently a known regression in Doom TDA and preset selector when using the up-to-date OptiScaler. If you need these features, use the fallback .dll instead.

The current experimental path requires Vulkan 1.1 or newer and the provider's
required Vulkan device features. The validated hardware baseline is an RDNA2
Radeon PRO W6400; support for other GPU/driver combinations is not implied.
OptiScaler routes its sharpening control through RCAS because internal provider
sharpening is not supported.

## Licensing and provenance

This mixed-source repository does not have a single repository-wide license.
AMD-derived SDK headers and model/shader payloads retain AMD's MIT license; see
[`LICENSES/AMD-FidelityFX-SDK-MIT.md`](LICENSES/AMD-FidelityFX-SDK-MIT.md).
OptiScaler is GPLv3; see
[`LICENSES/OptiScaler-GPL-3.0.txt`](LICENSES/OptiScaler-GPL-3.0.txt) and the
pinned source in the `optiscaler` submodule. See
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) and
[`SOURCE_PROVENANCE.md`](SOURCE_PROVENANCE.md) for exact revisions.
The v0.3.2.4 prebuilt OptiScaler DLL is an exception: its source revision is
unavailable, and the submodule must not be treated as its matching source.

Public distribution remains experimental. Review the recorded provenance and
license boundaries before reusing or redistributing the model/shader material.
