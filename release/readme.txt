EXPERIMENTAL FSR4.0.2 VK IMPLEMENTATION — RELEASE 0.3.2.1
THINGS WILL BREAK! IF YOU FIND A PROBLEM, @de_w23 ON THE OPTISCALER DISCORD!

Requirements:
Vulkan 1.1 or newer
AMD RDNA2-based GPU or later; initial NVIDIA support tested on RTX 3070 Ti through Proton.
Other NVIDIA GPUs, Intel GPUs, and native Windows NVIDIA operation are not yet validated.


Instructions on usage:
1. Extract a pre-existing OptiScaler 10.0 nightly build into your game as normal.
2. Optional but recommended: overwrite the existing OptiScaler.dll with the custom build included here.
3. Put amd_fidelityfx_upscaler_vk.dll in the /<game folder location>/OptiScaler/ folder.
4. FSR4_VK_ENABLE_DEVICE_FEATURES=1 is redundant with this build and may be removed from the launch args.
5. Select the FSR 3.X/FFX backend and then the FSR 4.0.2c Vulkan provider.

Experimental direct drop-in:
The self-contained upscaler DLL may theoretically be renamed to amd_fidelityfx_vk.dll and used without the custom OptiScaler build. Back up the original DLL first.
