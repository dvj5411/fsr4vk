# Source provenance

The provider source and build inputs were assembled from the private research
repository `dvj5411/fsr4-vulkan-translation` at commit
`764462671541c0f47d8080030ef752919069bf80`.

This integrates the Deck fix (`f956eb0`) and the public NVIDIA development
branch at `da7efe5` (implementation `763a35e`). Portable shader capture identities
are recorded in `assets/portable/provenance.json`. Original payloads are unchanged.
Preset 4 DRS preparation comes from `bae0829`, with matched native, portable and
Windows/Proton evidence in `native/validation-data/drs-20260915`. Release 0.3.2.2
adds six-preset control/reporting and a CPU active-core map-lookup bypass.
Combined functional checks are in `native/validation-data/release-0322`.
Release 0.3.2.3 adds external-exposure input support and identifiable missing
Vulkan device-feature diagnostics. Synthetic native and Windows/Proton checks,
including the expected missing-command rejection, are summarized in
`provider/WINDOWS-FIELD-FIXES.md`. Raw evidence remains in the private research
repository's `native/validation-data/windows-field-20260916` directory.
The scalar-spill optimization at private research commit `7aa4a0e` is paused and
excluded from this release.

The minimum AMD FFX API headers and the model/shader material derive from AMD's
FidelityFX SDK commit `01446e6a74888bf349652fcf2cbf5f642d30c2bf`
(`AMD FidelityFX SDK 2.0.0`). The license from that snapshot is preserved at
`LICENSES/AMD-FidelityFX-SDK-MIT.md`.

The OptiScaler submodule is pinned to modified-source commit
`f52646c3e440d3c7dc1a05ce0a77f30ab68f6dfa`. Its GPLv3 license is preserved at
`LICENSES/OptiScaler-GPL-3.0.txt`.

The v0.3.2.3 archive contains the updated embedded provider validated on W6400
through UMU/GE-Proton11-5 and the unchanged personal OptiScaler DLL from v0.3.2.2,
built by successful GitHub Actions
run `34933619764`, artifact `10383255334`, at the pinned source revision above.
The public source build also passes; PE build timestamps need not be identical.
No fresh game, Deck, NVIDIA or native-Windows validation is claimed for this release.
The separate PR #1161 device-feature patch is supplied for manual upstream
submission; the archive does not contain a new OptiScaler build of that PR.
See `provider/WINDOWS-FIELD-FIXES.md` for the diagnosis and validation limits.

- `amd_fidelityfx_upscaler_vk.dll`:
  `2554284e6544b819160b4998a6aa9d871aa283d7ae6ac976200d0b8236fa0e76`
- `OptiScaler.dll`:
  `0547cf39a65d9eff108ff3efb519d53cf07d186387cefb7de68fe01c62dd9fb6`
