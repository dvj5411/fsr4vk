# Third-party notices

## AMD FidelityFX SDK

The FFX API headers under `amd-fidelityfx-sdk/` and the model/shader payloads
under `assets/general/` originate from the AMD FidelityFX SDK 2.0.0 snapshot
identified in `SOURCE_PROVENANCE.md`. They retain AMD's copyright notice and
MIT license, reproduced in `LICENSES/AMD-FidelityFX-SDK-MIT.md`.

AMD, FidelityFX, and FSR are trademarks of Advanced Micro Devices, Inc. This
project is independent and does not claim AMD sponsorship or endorsement.

## OptiScaler

The `optiscaler` Git submodule pins the modified source used to build the
distributed `OptiScaler.dll`. OptiScaler is licensed under GNU GPL version 3;
the complete license is reproduced in `LICENSES/OptiScaler-GPL-3.0.txt`.

The submodule has its own third-party dependencies and notices. Clone it with
`git submodule update --init --recursive` before building or redistributing it.
