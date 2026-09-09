# fsr4vk

`fsr4vk` is an experimental native Vulkan FFX upscaler provider for the
FSR 4.0.2c INT8 model, together with the OptiScaler integration used to load it.
It is an independent research project and is not an AMD product or an official
OptiScaler release.

The repository is intentionally small: it contains the provider source, the
verified runtime payloads embedded into the provider DLL, the minimum AMD FFX
API headers needed by that source, and a pinned OptiScaler source submodule.

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
the OptiScaler submodule's recursive dependencies. The custom DLL is optional,
but recommended for the best provider selection, preset reporting, and
diagnostics.

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

The script verifies all ten bundled model/preset payloads, embeds them, and
writes the self-contained provider under both supported discovery names:

```text
build/provider-windows/amd_fidelityfx_upscaler_vk.dll
build/provider-windows/amd_fidelityfx_vk.dll
```

Set `FSR4_PROVIDER_OUTPUT_DIR` to change the output directory or
`FSR4_EMBED_ASSETS` to build with another verified `assets/general` tree.

## Build OptiScaler

Open `optiscaler/OptiScaler.sln` in Visual Studio 2022 and build the `Release`
configuration for `x64`. The unsigned DLL is normally written to:

```text
optiscaler/x64/Release/a/OptiScaler.dll
```

## Install

Start with an existing working OptiScaler 10.0 nightly installation. The custom
OptiScaler build included in releases is optional, but recommended.

1. Close the game and back up its existing DLLs.
2. Recommended: replace its `OptiScaler.dll` with the matching custom build.
3. Put `amd_fidelityfx_upscaler_vk.dll` in the game's `OptiScaler/` directory.
4. Remove `FSR4_VK_ENABLE_DEVICE_FEATURES=1` from the launch arguments if it was
   added for an older build. It is redundant and is not read by the current
   provider-owned device negotiation path.
5. Select the FSR 3.X/FFX backend and then the FSR 4.0.2c Vulkan provider.

A theoretical direct provider drop-in can instead use the
`amd_fidelityfx_vk.dll` filename expected by a game or existing loader. This
path is new and should still be treated as experimental.

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

Public distribution remains experimental. Review the recorded provenance and
license boundaries before reusing or redistributing the model/shader material.
