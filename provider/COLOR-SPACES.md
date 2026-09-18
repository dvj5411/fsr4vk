# Color-space support

Release 0.3.2.5 supports generic non-linear context input and dispatch-time
sRGB/PQ selection. PQ takes priority over sRGB; clearing dispatch flags restores
the context-selected mode. Unsupported flags remain errors.

Only pre/post shader permutations change. Each model caches up to four pipeline
pairs, retains pipelines until context destruction, and resets temporal history
when the color mode changes. A failed dispatch retains the pending reset.
The model-11 row-bounds guard fixes overlapping rows in 1080-capacity bundles;
2160-capacity model-11 shaders remain byte-identical. This correctness fix does
not include the separate throughput or memory-placement optimization experiments.

`assets/colors/<tier>/<preset>/<mode>/` contains verified native and portable
pre/post overlays for all six presets and both capacity tiers. Embedding checks
their hashes and resource ABI. Existing base shader/model assets are unchanged.

Opt-in diagnostics include context IDs, monotonic timestamps, model construction
CPU time, color mode/reset, and context destruction. Windows logs default beside
the host executable, with a temporary-directory fallback if necessary.

## Retained September 18 validation

W6400 / RADV, with Windows DLL checks through GE-Proton11-5:

- 72 two-frame color cases dispatched without validation errors or nonfinite
  output; 68 passed the original numerical gates. All 36 2160-capacity cases
  matched their backend-specific references byte-for-byte on both frames.
- Four 19-frame Windows sequences passed color switching, return to the context
  default, queued-frame reuse, overflow rejection, and finite-output checks.
  Each context retained one cached model; first and final images matched.
- The Windows Quality matrix dispatched all 12 cases; 11 passed numerical gates.
  All 24 saved Windows/native-harness images matched byte-for-byte.
- Seven optional logging cases passed, including a spaced executable directory,
  explicit override, and read-only-directory fallback.
- The user manually confirmed color modes working in NMS.

The unchanged gates are RMSE <= 0.002, at most 5% of components with absolute
error > 0.001, and no nonfinite output. These 1080-capacity frame-1 exceptions
remain open; no pixels were cropped and no thresholds relaxed:

| Backend | Preset | Color | RMSE |
| --- | --- | --- | --- |
| Native mixed-dot | Ultra Performance | sRGB | 0.0020573652513457 |
| Native mixed-dot | DRS | sRGB | 0.0020004827716992 |
| Portable INT8 | Ultra Performance | Generic non-linear | 0.0023897736926123 |
| Portable INT8 | Quality | sRGB | 0.0021056313502868 |

The earlier linear Ultra Performance reference exception also remains open.
These differences are confined to the left-edge region affected by the row
correction and subsequent temporal propagation; that does not waive the failures.
Forced-portable W6400 tests are not NVIDIA hardware validation. No new Steam Deck,
NVIDIA, or native-Windows validation is claimed. Retained logs do not independently
establish a fix for the separately reported NMS graphics-settings slowdown.

The source evidence is retained at private commit
`ae0162216b85610bf2b37a14f3ec3075adb2e28e`, under
`native/validation-data/color-spaces-20260918`,
`native/validation-data/model11-row-bounds-20260918`, and
`native/validation-data/log-location-20260918`.
