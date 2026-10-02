# Native motion format handoff

The optional version-1 query in `ffx_vk_native_inputs.h` lets a host attach the
original motion-vector `VkFormat` to each dispatch. It is not cached. Hosts
that do not receive a successful capability response must leave their existing
FFX chain unchanged.

OptiScaler's FFX mapping collapses RG16 SNORM and RGBA8 SNORM to one FFX enum;
an existing VkImage's creation format cannot be queried by the provider.
The native handoff resolves that ambiguity. RG16 float and RG16 SNORM are
sampled directly; incompatible or contradictory metadata is rejected with the
current and accepted formats. Converting the input cannot recover lost format
information. Do not relabel an incompatible image as a supported format.

Old hosts retain the explicit `FSR4_VK_MOTION_FORMAT=R16G16_SNORM` development
override. Prefer native per-dispatch metadata whenever available.

Cooperative Matrix remains removed. Its old argument does not activate any
backend. Historical experiment results remain in dated validation directories;
they are not current setup instructions or performance claims.
