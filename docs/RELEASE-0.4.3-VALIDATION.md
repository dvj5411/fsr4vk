# v0.4.3 verification record

The provider/core sources and model payloads are synced from the tested private
implementation. The compressed provider in this release is the exact binary
installed for the completed No Man's Sky, Baldur's Gate 3 and Red Dead Redemption 2
checks, not a new untested rebuild. The runtime source revision is `62e8563546ecdd1e0bf709a67144a32b93b4348b`;
later documentation/build-metadata changes do not alter its compiled inputs.

The matching OptiScaler branch was fast-forwarded into the fork's `master` at
`613492a8909929dfd618b03df08e47b07055c0b0`. The pinned source and bundled DLL match
that revision. No upstream PR or conversation was modified.

## Fresh game checks

All three games were tested by the user on the Radeon Pro W6400 under Linux/Proton.
Installed provider/host hashes were checked and logs were restricted to sessions
after deployment, excluding older appended sessions.

| Game | Fresh evidence | Result / proof boundary |
| --- | --- | --- |
| No Man's Sky | New provider marker, Vulkan 1.3, 4.1.1 portable backend; 4,161 recorded dispatches, zero rejected/failed dispatches and normal destruction | No blocking runtime error; user completed the visual check. |
| Baldur's Gate 3 | Current host build and provider path loaded; successful Vulkan 1.1 device preparation; no host error entries | User completed the visual check. Provider logging was disabled, so this is not a per-dispatch provider-log proof. |
| Red Dead Redemption 2 | Both 4.0.2 native-mixed-dot and 4.1.1 portable contexts; Vulkan 1.1; no host error entries; 29,000 successful present returns | User completed the visual check. The final 4.1.1 context has no destruction marker, so its complete lifetime is not certified by that log. |

The once-per-context unsupported standard key/value configuration warning is
expected: the provider explicitly rejects controls it does not implement. The
separate preset API remains supported. Generic host driver capability messages
do not mean the removed Cooperative Matrix provider path was reintroduced.

These checks do not establish new native-Windows, NVIDIA, Steam Deck or RX 9070 XT
validation, nor a measured performance improvement.

## CPU, GPU and packaging gates

- Thirteen sanitized C++ suites cover descriptor layout, device features/state,
  safe host inputs, resolution arithmetic, resource adoption, API boundaries,
  preset controls, diagnostics, memory/color policy and timestamps.
- Seventeen hardening tests run both normally and under `python -O`; 49
  optimization-tool tests and four generated native compile variants pass. Linux
  CI additionally supplies MinGW for the Windows-only generated collector; the
  macOS run explicitly skips it.
- Reference input bytes remain unchanged: 29,491,216 bytes, SHA256
  `04448512d29b68a0ec6334a4391bfdec082045a6f6481f8359ab8a4b417d274b`.
- W6400 paired regression checks cover both models, matching backends, standard
  scale modes/FSRAA, arbitrary sizes, Vulkan 1.1, nonlinear color, motion inputs,
  exposure and normal/debug DLLs under Proton: 242 passing cases, 482 frame
  comparisons, maximum RMSE 0. These synthetic checks are not new captures from
  the original AMD DLL.
- Normal and independent raw-debug DLLs have the five expected AMD64 FFX exports
  and identical decoded model assets: 948 logical paths / 553 unique payloads,
  including 420 4.1.1 paths. Frozen-input checks bind their compiler/asset inputs;
  the production C++ decoder checks the compressed resources.
- The ZIP packager tests exact contents, byte identity, valid AMD64 provider/host
  exports and corruption checks. Its validation remains active under `python -O`.

## Known limitation retained, not hidden

A separate two-poison border experiment has 10 passes and **six existing 4.1.1
odd-output-edge failures**. All eight 4.0.2 cases pass. With 4.1.1, an unmatched
row/column can retain its initial output values. This predates the current fixes;
it is not included in the 242 passing paired cases. Prefer even output dimensions
with 4.1.1 until reference semantics establish a correct border policy. No
arbitrary edge fill or relaxed acceptance threshold was added.

## Release artifacts

| Bundled DLL | Bytes | SHA256 |
| --- | ---: | --- |
| `OptiScaler/amd_fidelityfx_upscaler_vk.dll` | 18,034,864 | `25025f9a6acef5636d49ca9ae8d87ebeb77f8891083d9657ca64deec1a12a440` |
| `OptiScaler.dll` | 25,711,104 | `a632c0dc4f3627e4cf109e62b87bee327565bcef415ed42bcb6b4e4600f4490d` |

The ZIP contains only those two DLLs, `readme.txt`, and the three required license
files. It contains no debug DLL, fallback DLL, logs, symbols or research captures.
Building with embedded assets still produces the separate uncompressed,
unstripped `amd_fidelityfx_upscaler_vk_debug.dll`; removing it from the ZIP does
not remove the debug build target. User-supplied release notes are kept verbatim.

`fsr4vk-v0.4.3.zip` SHA256:
`3dd2fe7a6b14a1d045c23aa51b63cb45a53576d64eba5f96cb0e334e55ce9827`.
