# Source provenance

The provider source and build inputs were assembled from the private research
repository `dvj5411/fsr4-vulkan-translation` at commit
`c1e0c7d2cb6b058d2244273f3b246b1f2c7e5cf1`.

This integrates the Deck fix (`f956eb0`) and the public NVIDIA development
branch at `da7efe5` (implementation `763a35e`). Portable shader capture identities
are recorded in `assets/portable/provenance.json`. Original payloads are unchanged.
The scalar-spill optimization at private research commit `7aa4a0e` is paused and
excluded from this release.

The minimum AMD FFX API headers and the model/shader material derive from AMD's
FidelityFX SDK commit `01446e6a74888bf349652fcf2cbf5f642d30c2bf`
(`AMD FidelityFX SDK 2.0.0`). The license from that snapshot is preserved at
`LICENSES/AMD-FidelityFX-SDK-MIT.md`.

The OptiScaler submodule is pinned to modified-source commit
`e7ee0b4fbd342a411ab4444f28ac9b371db38db2`. Its GPLv3 license is preserved at
`LICENSES/OptiScaler-GPL-3.0.txt`.

The v0.3.2.1 archive contains the newly built provider and the unchanged, verified
OptiScaler binary from v0.3.2 at the pinned source revision above:

- `amd_fidelityfx_upscaler_vk.dll`:
  `2db9ad3aa91f6b7f08d4913400712a3b70cb30bfd6b041241ef5863c9f51fc86`
- `OptiScaler.dll`:
  `9806721fed05f41d57665d398b1bbb897afa95d088e8b04d25e11c728a81831b`
