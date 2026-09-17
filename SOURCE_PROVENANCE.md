# Source provenance

The provider source and build inputs were assembled from the private research
repository `dvj5411/fsr4-vulkan-translation` at commit
`9d324330c0eb835763de0fc6f6361bc92ddffa8c` (provider fix and opt-in logging).

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

The v0.3.2.4 archive contains the exact embedded provider tested on W6400 through
GE-Proton11-6, with optional-mask acceptance and opt-in file logging. The runtime
shader/model resources are unchanged. Ten synthetic GPU cases and six logging
integration cases passed; the user subsequently confirmed RDR2 launches. No new
Deck, NVIDIA or native-Windows validation is claimed.

The bundled OptiScaler DLL was supplied prebuilt by the PR #1161 author. The
project maintainer reports permission to redistribute it. It is byte-identical
to the author's ASI build used in the RDR2 test. Exact corresponding source and
commit identification have not been supplied yet. The older pinned submodule
above is retained for historical integration work and **does not reproduce this
binary**. No source correspondence or reproducible-build claim is made for it.
The parked local RDR2 BDA workaround is not included in this binary.
See `provider/RDR2-COMPATIBILITY.md` for validation boundaries.

- `amd_fidelityfx_upscaler_vk.dll`:
  `9d8e5489370ffd5f593136b89957c0761a22be7697dd8075f67ffac98d2d6120`
- `OptiScaler.dll`:
  `96b9fcf18bbeea3a14d970cafceb00efb91670b374fd1a05b86c74d20299af2a`
