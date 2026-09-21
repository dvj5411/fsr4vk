# Third-party notices

## AMD FidelityFX SDK

The FFX API headers under `amd-fidelityfx-sdk/` and the model/shader payloads
under `assets/general/`, `assets/portable/`, and `assets/colors/` originate from the AMD FidelityFX SDK 2.0.0 snapshot
identified in `SOURCE_PROVENANCE.md`. They retain AMD's copyright notice and
MIT license, reproduced in `LICENSES/AMD-FidelityFX-SDK-MIT.md`.

AMD, FidelityFX, and FSR are trademarks of Advanced Micro Devices, Inc. This
project is independent and does not claim AMD sponsorship or endorsement.

## OptiScaler

For v0.4 the `optiscaler` submodule pins the corresponding source of the bundled
repo-built `OptiScaler.dll` at `59ac04da5bd54bc8ddd84a56059a0a6283c7c198`.
Neither the older PR-author binary nor `OptiScaler_fallback.dll` is bundled.
The following notes describe earlier releases only.

The `optiscaler` Git submodule pins the modified source for the custom
`OptiScaler_fallback.dll` restored in v0.3.2.5 and used by older releases.
The v0.3.2.4 archive instead includes a prebuilt DLL supplied by the PR #1161
author, with redistribution permission reported by the project maintainer.
Its matching source is not available yet; the submodule is not its corresponding
source. The supplied binary's identity is recorded in `SOURCE_PROVENANCE.md`.
OptiScaler is licensed under GNU GPL version 3;
the complete license is reproduced in `LICENSES/OptiScaler-GPL-3.0.txt`.

The submodule has its own third-party dependencies and notices. Clone it with
`git submodule update --init --recursive` before building or redistributing it.

## FSR 4.1.1 runtime material

The `assets/fsr411/` manifests identify the original FFX 2.3 SDK 4.1.1 INT8
provider and its translated shader/model material by SHA-256. This is distinct
from the older SDK snapshot used by the 4.0.2 assets. See `SOURCE_PROVENANCE.md`.

## Zstandard

The compressed provider statically links the Zstandard 1.5.7 decoder under its
BSD license option. See `third_party/zstd/README.md` for pinned source hashes and
`LICENSES/Zstandard-BSD.txt` for the copyright and license notice. The raw debug
provider does not link this decoder.
