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

## Requirements

The provider build currently uses a Unix-like host with:

- Python 3;
- MinGW-w64 (`x86_64-w64-mingw32-g++` and `windres`);
- a Vulkan SDK containing `include/vulkan`; and
- a Windows Vulkan import library (`vulkan-1`) visible to the linker.

Building `OptiScaler.dll` requires Windows, Visual Studio 2022, and the
OptiScaler submodule's recursive dependencies.

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
writes the self-contained provider to:

```text
build/provider-windows/amd_fidelityfx_upscaler_vk.dll
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

Start with an existing working OptiScaler 10.0 nightly installation.

1. Close the game and back up its existing DLLs.
2. Replace its `OptiScaler.dll` with the matching build from this project.
3. Put `amd_fidelityfx_upscaler_vk.dll` in the game's `OptiScaler/` directory.
4. Add `FSR4_VK_ENABLE_DEVICE_FEATURES=1` before `%command%` in the launch
   arguments used by Proton/Steam.
5. Select the FSR 3.X/FFX backend and then the FSR 4.0.2c Vulkan provider.

The current experimental path requires Vulkan 1.1 or newer and the provider's
required Vulkan device features. The validated hardware baseline is an RDNA2
Radeon PRO W6400; support for other GPU/driver combinations is not implied.

## Licensing and provenance

This mixed-source repository does not have a single repository-wide license.
AMD-derived SDK headers and model/shader payloads retain AMD's MIT license; see
[`LICENSES/AMD-FidelityFX-SDK-MIT.md`](LICENSES/AMD-FidelityFX-SDK-MIT.md).
OptiScaler is GPLv3; see
[`LICENSES/OptiScaler-GPL-3.0.txt`](LICENSES/OptiScaler-GPL-3.0.txt) and the
pinned source in the `optiscaler` submodule. See
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) and
[`SOURCE_PROVENANCE.md`](SOURCE_PROVENANCE.md) for exact revisions.
