EXPERIMENTAL FSR4.0.2 VK IMPLEMENTATION — RELEASE 0.3.2.5
THINGS WILL BREAK! IF YOU FIND A PROBLEM, @de_w23 ON THE OPTISCALER DISCORD!

Requirements:
Vulkan 1.1 or newer
AMD RDNA2-based GPU or later; initial NVIDIA support tested on RTX 3070 Ti through Proton.
Other NVIDIA GPUs, Intel GPUs, and native Windows NVIDIA operation are not yet validated.


Instructions on usage:
1. Extract a pre-existing OptiScaler 10.0 nightly build into your game as normal.
2. Extract this archive into the root of the game files containing your existing OptiScaler installation. The archive places the provider in OptiScaler/ automatically.
3. To use the custom fallback, rename OptiScaler_fallback.dll to the filename used by your existing OptiScaler installation (for example OptiScaler.dll or OptiScaler.asi) and replace that file after backing it up. Do not load both forms simultaneously.
4. FSR4_VK_ENABLE_DEVICE_FEATURES=1 is redundant with this build and may be removed from the launch args.
5. Select the FSR 3.X/FFX backend and then the FSR 4.0.2c Vulkan provider.
6. With the included OptiScaler build, leave the preset at Auto or force presets 0–5. Preset 4 uses the DRS model. Preset selection does not change the game's render resolution.

Diagnostics: logging is opt-in with FSR4_VK_LOG=1 or a nonempty FSR4_VK_LOG_PATH. Logs default to fsr4vk-provider-<pid>.log beside the game executable, with a %TEMP% fallback if that directory is not writable.

extract all to root of game files

There is currently a known regression in Doom TDA and preset selector when using the up-to-date OptiScaler. If you need these features, use the fallback .dll instead.
