EXPERIMENTAL FSR4.0.2 VK IMPLEMENTATION — RELEASE 0.3.1
THINGS WILL BREAK! IF YOU FIND A PROBLEM, @de_w23 ON THE OPTISCALER DISCORD!

Requirements:
Vulkan 1.1 or newer
RDNA2-based GPU or later


Instructions on usage:
1. Extract a pre-existing OptiScaler 10.0 nightly build into your game as normal.
2. Drop in and overwrite the existing OptiScaler.dll within the game directory.
3. Put amd_fidelityfx_upscaler_vk.dll in the /<game folder location>/OptiScaler/ folder.
4. In the launch args, paste FSR4_VK_ENABLE_DEVICE_FEATURES=1 %command%.
5. Select the FSR 3.X/FFX backend and then the FSR 4.0.2c Vulkan provider.
