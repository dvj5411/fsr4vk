# Source provenance

The provider source and build inputs were assembled from the private research
repository `dvj5411/fsr4-vulkan-translation` at commit
`28e25918a266cb64b339c16bc54ada6751f0c8c5`.

This integrates the Deck fix (`f956eb0`) and the public NVIDIA development
branch at `da7efe5` (implementation `763a35e`). Portable shader capture identities
are recorded in `assets/portable/provenance.json`. Original payloads are unchanged.
Preset 4 DRS preparation comes from `bae0829`, with matched native, portable and
Windows/Proton evidence in `native/validation-data/drs-20260915`. Release 0.3.2.2
adds six-preset control/reporting and a CPU active-core map-lookup bypass.
Combined functional checks are in `native/validation-data/release-0322`.
The scalar-spill optimization at private research commit `7aa4a0e` is paused and
excluded from this release.

The minimum AMD FFX API headers and the model/shader material derive from AMD's
FidelityFX SDK commit `01446e6a74888bf349652fcf2cbf5f642d30c2bf`
(`AMD FidelityFX SDK 2.0.0`). The license from that snapshot is preserved at
`LICENSES/AMD-FidelityFX-SDK-MIT.md`.

The OptiScaler submodule is pinned to modified-source commit
`f52646c3e440d3c7dc1a05ce0a77f30ab68f6dfa`. Its GPLv3 license is preserved at
`LICENSES/OptiScaler-GPL-3.0.txt`.

The v0.3.2.2 archive contains the embedded provider validated on W6400 through
UMU/GE-Proton11-5 and the personal OptiScaler build from successful GitHub Actions
run `34933619764`, artifact `10383255334`, at the pinned source revision above.
The public source build also passes; PE build timestamps need not be identical.
No fresh game, Deck, NVIDIA or native-Windows validation is claimed for this release.

- `amd_fidelityfx_upscaler_vk.dll`:
  `e30aed4cb9b931836bba22593fb0aa2ae007d1b0eddcb64b506537b6599f0290`
- `OptiScaler.dll`:
  `0547cf39a65d9eff108ff3efb519d53cf07d186387cefb7de68fe01c62dd9fb6`
