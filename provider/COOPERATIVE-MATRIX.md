# Experimental cooperative INT8 path

Enable with `FSR4_VK_COOP_MAT=1`; leave it unset or use `0` for the normal path.
Both 4.0.2 and 4.1.1 transform eligible constant-weight convolution blocks.
Other blocks and partially active subgroups retain their original dot path.

Requires the updated OptiScaler integration included in v0.4.2. The optional
host patch is `optiscaler-native-inputs-cooperative-matrix.patch`, based on
OptiScaler commit `59ac04da`. Source for the bundled build is pinned by this
repository's OptiScaler submodule. The host also supplies the original motion
VkFormat so RG16 SNORM is sampled natively without guessing its FFX alias.

Additional Vulkan requirements:

- `VK_KHR_cooperative_matrix`, feature `cooperativeMatrix`.
- `subgroupSizeControl` and `computeFullSubgroups`: Vulkan 1.3 or
  `VK_EXT_subgroup_size_control`.
- `vulkanMemoryModel` and `vulkanMemoryModelDeviceScope`: Vulkan 1.2 or
  `VK_KHR_vulkan_memory_model`.
- Subgroup 16x16x16 SINT8 A/B, SINT32 C/result, non-saturating configuration.
- Compute wave32, subgroup ballot and 5,632 bytes shared memory.

Properties are queried and logical-device enablement must be confirmed by
the host. Unsupported requests fail explicitly; this flag cannot create
hardware support. Provider logs report the transformed block count.

Correctness was tested with software emulation on W6400: 20 cases covering
both models, all presets, DRS, native, minimum/odd and 4K dimensions; all
40 scalar/cooperative frame pairs were byte-identical. Shader validation
passed Vulkan 1.1 and 1.3. This is not hardware performance evidence.
RDNA3 gameplay and speedup remain unmeasured. Padding, data movement and
occupancy can offset any matrix-instruction benefit.

The ordinary path was separately verified in NMS on W6400. That run used
4.1.1, confirmed native motion metadata and recorded 1,646 dispatches with
zero rejected/failed dispatches. It did not run cooperative matrices.

Testing also found and fixed a pre-existing 4.1.1 odd-edge issue: prepass
quad shuffles now operate only on complete 2x2 quads. Incomplete edge tensors
retain their zero clear, matching the postpass's existing unmatched-edge
behavior. Even-size tested output remains unchanged.
