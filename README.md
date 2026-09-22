# fsr4vk

`fsr4vk` is an experimental native Vulkan FFX upscaler provider for the
FSR 4.1.1 and FSR 4.0.2 INT8 models, together with the OptiScaler integration used to load them.

## AI Disclosure

This project has been heavily assisted by AI models such as GPT-5.6 Sol and 6 Astra. Most of the tedious work such as individual shader dumping from VKD3D-Proton were performed by AI. 
I'm not going to pretend it wasn't helped by it. However, many parts were still handled by a person.

## Requirements

The provider build currently uses a Unix-like host with:

- Python 3;
- MinGW-w64 (`x86_64-w64-mingw32-g++` and `windres`);
- the Zstandard CLI (tested with 1.5.7);
- a Vulkan SDK containing `include/vulkan`; and
- a Windows Vulkan import library (`vulkan-1`) visible to the linker.

Building the custom `OptiScaler.dll` requires Windows, Visual Studio 2022, and
the OptiScaler submodule's recursive dependencies. Release v0.4 includes the
repo-built `OptiScaler.dll` with the RDR2 fixes; no fallback DLL is bundled.

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

The script verifies both versions' shader/model bundles and builds two
self-contained providers:

```text
build/provider-windows/amd_fidelityfx_upscaler_vk.dll
build/provider-windows/amd_fidelityfx_upscaler_vk_debug.dll
```

Set `FSR4_PROVIDER_OUTPUT_DIR` to change the output directory or
`FSR4_EMBED_ASSETS` to build with another verified `assets/general` tree and
its sibling `assets/portable`, `assets/colors`, and `assets/fsr411` trees. The portable INT8 backend is selected
automatically when the VALVE mixed-dot feature is unavailable. Initial NVIDIA
validation covers the RTX 3070 Ti through Proton, not native Windows NVIDIA.

## Build OptiScaler

Open `optiscaler/OptiScaler.sln` in Visual Studio 2022 and build the `Release`
configuration for `x64`. The unsigned DLL is normally written to:

```text
optiscaler/x64/Release/a/OptiScaler.dll
```

## Install

Follow the installation steps in [readme.txt](readme.txt), also included verbatim
in the ZIP. Select **FSR 3.X** in the Upscalers tab; the FFX provider selector
offers **FSR 4.1.1 VK INT8** and **FSR 4.0.2 VK INT8**.

The current experimental path requires Vulkan 1.1 or newer and the provider's
required Vulkan device features. The validated hardware baseline is an RDNA2
Radeon PRO W6400; support for other GPU/driver combinations is not implied.
OptiScaler routes its sharpening control through RCAS because internal provider
sharpening is not supported.

## Licensing and provenance

This mixed-source repository does not have a single repository-wide license.


AMD-derived SDK headers and model/shader payloads retain AMD's MIT license; see
[`LICENSES/AMD-FidelityFX-SDK-MIT.md`](LICENSES/AMD-FidelityFX-SDK-MIT.md).


OptiScaler and the Native Vulkan Provider, alongside any other code that is original is GPLv3; see
[`LICENSES/GPL-3.0.txt`](LICENSES/GPL-3.0.txt) and the
pinned source in the `optiscaler` submodule. 

Zstandard retains the BSD license; see [`/LICENSES/Zstandard-BSD.txt`](/LICENSES/Zstandard-BSD.txt).

See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for any other licensing notices.


