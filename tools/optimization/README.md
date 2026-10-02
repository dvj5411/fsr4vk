# INT8 optimisation analysis tools

Python 3.9+ standard library only; runs locally on macOS, Linux or Windows.
The offline analysis tools do not load a DLL or execute contributor scripts.
The explicitly marked research runners below execute isolated GPU tests; the
model-11 guard builder creates a separate diagnostic shader bundle. None deploy
game files or establish a performance improvement by themselves. Outputs use
exclusive creation: choose a new output filename for each run.

## Audit exact shader inputs

```sh
mkdir -p build/optimization-tooling
python3 tools/optimization/audit.py /path/to/assets/general --output build/optimization-tooling/native.json
python3 tools/optimization/audit.py /path/to/assets/portable --output build/optimization-tooling/portable.json
```

Discovery follows the existing bundle manifests, not fixed preset counts,
resolution labels, filename-only joins or the contributor's FG pass numbering.
Every listed shader hash must match. The report includes relative paths,
preset/capacity metadata, workgroup dimensions, unpadded declared Workgroup
bytes and static barrier instruction counts. This is not a full SPIR-V validator.
Unsupported layouts produce `null` with a reason, never a guessed zero.
Driver padding, compiler-introduced LDS, runtime execution counts and register
allocation cannot be inferred from these declarations. External-exposure's
inline adapter is not in the bundle manifests and is not included in this audit.

### Fresh W6400 resource captures

`native/pipeline-resource-probe.cpp` is a compile-only research executable, not a
provider change. It uses current QualityContext construction, captures driver
statistics/IR, and retains the exact input module bytes when an IR directory is
supplied. A complete capture has 15 pipelines and zero GPU dispatches. Restrict
the loader to the intended GPU; the probe refuses multiple physical devices.

```sh
./resource-probe assets/general/1080/quality 0 resources-1080 > resources-1080.jsonl
python3 tools/optimization/captured_stats.py resources-1080.jsonl resources-1080 assets/general/1080/quality/manifest.json --output driver-1080.json
python3 tools/optimization/audit.py assets/general --driver-stats driver-1080.json --output audited-1080.json
```

Use a new directory/output for every capture. `captured_stats.py` verifies each
retained module against its manifest before making SHA-based joins. It rejects
incomplete captures, missing statistics and ambiguous executables; driver names,
units, descriptions and values are retained. Capture builds enable statistics
and internal representations and are **not timing builds**.

### Current-source temporal benchmark

The in-process `native/dispatch-smoke.cpp` harness accepts
`FSR4_TEST_PROFILE_FRAMES=2..120` in `--provider-temporal` mode when a nonempty
`FSR4_VK_PROFILE_PATH` is supplied. It writes `<output>.timings.jsonl` with one
complete context interval, named pass intervals and an output hash per frame.
It checks every output frame for nonfinite half-floats. Frames 0/1 preserve the
existing reference inputs; later frames are synthetic research inputs, not new
original-DLL oracle captures. Dynamic-size transitions and external-DLL mode
are deliberately excluded from this benchmark mode.

`run-w6400-baseline.py ROOT` runs a bounded isolated native-Linux baseline with
48 frames, eight discarded warmup frames and sampled telemetry. It checks for
the W6400 at card1 and refuses known running games. It creates a new result
directory, uses an executable at ROOT/baseline and assets at ROOT/assets, and
supports `--cases 1080`, `--label NAME`, `--executable NAME`, `--trials 1..3`.
Use `--assets /absolute/path/to/isolated/assets` for a diagnostic bundle without
changing the default asset tree. The selected executable is unchanged.
The total deadline is 480 seconds and each process is limited to 240 seconds;
split large batches if CPU-generated inputs exceed that budget. Reject partial
series: accepted summaries require all 48 records. No game files are changed.

Each frame is individually submitted and read back; CPU input generation leaves
GPU idle gaps. Warmup exclusion does not imply sustained GPU clocks or real-game
pacing. Do not attribute baseline-repeat noise to an optimisation candidate.

### Isolated constants-memory placement experiment

`prepare_memory_placement.py native/dispatch-smoke.cpp OUTPUT.cpp --cpu-timing
--cache-readback` creates a separate research translation unit. Put it beside
the copied native headers in an isolated source snapshot, along with
`research-memory-placement.hpp`. It wraps allocation calls only inside
`quality-context.hpp`, not the harness's own staging/readback allocations.
Neither the production provider source nor any shader is edited.

Build the same generated source twice with identical flags, setting
`FSR4_RESEARCH_PREFER_LOCAL_CONSTANTS=0` (control) or `=1` (candidate). Only the
eight exactly-2048-byte, shader-device-address-only frame buffers are eligible.
The candidate requires HOST_VISIBLE | HOST_COHERENT and prefers DEVICE_LOCAL,
with baseline fallback on device-memory exhaustion. No local type means no
placement change; reject that run as a placement experiment. Other allocation
errors propagate. Single-threaded isolated harness use only, not a general
allocator or a header to include in the released DLL.

Setup diagnostics record actual compatible type bits, selected type/heap/flags,
allocation sizes and fallback. `memory-placement-policy-test.cpp` tests selection
and error handling without GPU allocation. `--cpu-timing` adds CPU dispatch and
readback-analysis timings outside the GPU timestamp intervals.

Use `--cache-readback` for new timing runs: it copies the completed output to
ordinary CPU memory before finite checking/hashing. The original direct-mapped
scans took about 1.37 seconds per frame on the W6400; this option reduced that
analysis to about 71 ms without changing GPU commands or output hashes. It does
not remove all CPU gaps, and it is **not an upscaler speedup**. Omitting the flag
retains the direct-read diagnostic control.

Run three alternating pairs with labels `mode-0-a`, `mode-1-a`, `mode-1-b`,
`mode-0-b`, `mode-0-c`, `mode-1-c` using the existing baseline runner.
`summarize_memory_placement.py ROOT --case 1080 --output REPORT.json` verifies
complete 48-frame series, actual placement, common assets and output hashes.
Use a separate root and `--case 1440` for the larger capacity tier. Do not merge
different readback methods into one timing comparison. The SHA identities and
setup logs distinguish the generated harnesses from the installed game DLL.

### Follow-up throughput experiments (research branch)

`prepare_wave_size.py` generates a separate cached-readback harness. Copy
`research-wave-size.hpp` and `research-memory-placement.hpp` beside its native
headers. Vulkan 1.3 subgroup-size control is explicitly queried and enabled;
`FSR4_RESEARCH_WAVE32_MASK` selects pipeline indices (pre=0, model9=9, post=13).
Use 0 for the identical instrumented control. These are single-context native
research programs, not DLL features or game launch arguments. Production host
feature negotiation remains unchanged.

The memory wrapper now accepts independent compile-time preferences
`FSR4_RESEARCH_PREFER_LOCAL_CONSTANTS`, `..._DESCRIPTORS`, and
`..._INITIALIZERS` (all default to 0). It logs all three allocation roles and
preserves required host visibility/coherence. Scratch is excluded by usage;
harness allocations remain outside the hooks. Change one role at a time.
`summarize_initializer_placement.py ROOT --case 1080 --output NEW.json` verifies
three alternating pairs named `repeat-control-{a,b,c}` and
`repeat-initializers-{a,b,c}`. It requires exactly one 89,216-byte initializer
allocation to change, 27 descriptor and eight constant allocations unchanged,
complete frame series, common assets, and actual local-memory selection.

`prepare_capacity_experiment.py REPO OUTPUT` creates two modified source copies
that force the larger shader/scratch layout while leaving image dimensions
unchanged. This is a rejected diagnostic, not a production resolution policy.
Its input-bundle hash in the generic runner is not the forced provider bundle:
the actual provider uses `general/2160/quality`. Retain generated-source hashes.

`build_prepass_literals.py SOURCE_BUNDLE NEW_BUNDLE` replaces sixteen reviewed
Quality prepass weight reads with exact-bit constant-vector selections. It
accepts only the two reviewed native shader hashes and one immutable weight hash.
It does not reorder dot arithmetic or quad reductions. Validate generated SPIR-V
before execution and compare all frames; this candidate regressed performance
in screening and must not be embedded into the released provider.

See `docs/W6400-INITIALIZER-LOCALITY-20260918.md` for completed results and limits.

### Isolate repeated-run divergence (research only)

Compile the in-process harness with `-DFSR4_RESEARCH_CAPTURE` and set a fresh
`FSR4_CAPTURE_DIR`. `native/research-pass-capture.hpp` captures frame zero's
uploaded input bytes, actual constants, cleared scratch, scratch after each
dispatch, exposure and reprojected output. It requires a single serialized
context and a scratch allocation of at most 24 MiB. This adds readback storage
and full synchronization and must not be used for performance measurements.
It is compiled out of ordinary builds. Keep large raw captures on the test
machine's 1 TB drive.

```sh
python3 tools/optimization/compare_pass_captures.py capture-a capture-b --output capture-comparison.json
python3 tools/optimization/build_model11_guard.py assets/general/1080/quality guarded/general/1080/quality
spirv-val --target-env vulkan1.3 guarded/general/1080/quality/pass-12.spv
```

The guard tool accepts only the exact reviewed 1080 Quality model-11 shader
SHA, verifies all manifest hashes, and creates a new bundle with an early return
for global X >= 480. It preserves arithmetic/weights and other shader bytes.
This is a causal experiment, **not** a general resolution/preset fix or a release
asset. Do not inject it indiscriminately into portable or DRS variants. Test the
guard separately with the ordinary temporal harness (no research capture) to
exclude capture synchronization as the reason for repeatability. See
`docs/W6400-MODEL11-ROW-RACE-20260918.md` for evidence and remaining coverage.

The runtime fix is tested by `tools/test-model11-row-bounds.py`, using
`native/model11-row-bounds-test.cpp` compiled as a standalone C++20 probe.
The current provider consumes the original assets and applies its own guard;
do not feed it the already-guarded diagnostic bundle (double injection is
intentionally rejected). That bundle is for the retained pre-fix diagnostic
harness described in the investigation report.
`model11_case_sets.py` selects existing W6400 oracle matrices;
`model11_deployment_checks.py` checks the packaged DLL and temporal repeats
without modifying a game. See `native/validation-data/model11-row-bounds-20260918/`
for complete results, including the open Ultra Performance reference gate.

Optional `--driver-stats /path/to/stats.json` accepts one compilation configuration
per shader in this format:

```json
{
  "gpu": "actual GPU identity",
  "driver": "actual driver/compiler version",
  "shaders": [{
    "sha256": "64-lowercase-hex-digits-of-the-compiled-SPIR-V",
    "stats": {
      "SGPRs": 108,
      "Spilled SGPRs": 0,
      "actual_wave": 64
    }
  }]
}
```

That is a schema example, not a measurement. Use a separate report for each
wave/compiler configuration. Statistics remain named and typed as supplied;
no AMD-only assumptions are imposed on NVIDIA data. Missing statistics remain
unknown. Unmatched or duplicate shader hashes are rejected. Record hashes at
capture time: do not attach current shader hashes retrospectively to old dumps.
The older probe JSONL does not contain those identities, so it is not joined
automatically. `scratch > 0` is deliberately not converted into a spill count.

## Rank timings / compare candidates

### Offline pre-pass weight-access audit

`prepass_memory.py CAPTURE BUNDLE --output REPORT.json` inspects the retained
RADV NIR and ISA for the reviewed native Quality pre-pass. It verifies the
captured SPIR-V against the bundle and manifest, follows the reviewed physical
weight-pointer argument, and evaluates its integer address expressions over
all 256 local lanes. Unknown ABI, address operators, widths, lane mapping and
table sizes are rejected. It never executes GPU work or edits a shader.

```sh
python3 tools/optimization/prepass_memory.py build/optimization-warmed-20260918/resources-1080 assets/general/1080/quality --output build/prepass-memory-1080.json
```

The access count is **not PCIe/VRAM traffic**: cache hits and lane request
coalescing must be measured separately. Module identity checks do not make this
a general-purpose NIR verifier or extend the findings to uncaptured presets,
color modes, portable INT8 or other drivers. See
`docs/W6400-PREPASS-MEMORY-20260918.md` for the source-to-buffer mapping and the
next controlled experiments.

### Timing reports

```sh
python3 tools/optimization/profiles.py /path/to/profile.csv --output build/optimization-tooling/ranked.json
python3 tools/optimization/profiles.py /path/to/baseline.jsonl --candidate /path/to/candidate.jsonl --metadata /path/to/run-identities.json --warmup 8 --output build/optimization-tooling/comparison.json
```

Accepted inputs:

- Our provider's `FSR4_VK_PROFILE_PATH` CSV: `pass,gpu_ms`. This contains only
  the final frame. The report labels it as one sample, not a warm-run median.
- The isolated temporal harness JSONL: ordered `frame`, named `passes`, optional
  enclosing `context_gpu_ms` and `output_fnv1a64`. Warmup is excluded explicitly.

A comparison requires at least two equally sized series, matching frame IDs
and pass-name sets, plus a metadata JSON object with `baseline` and `candidate`
objects. Each must contain `build_id`, `gpu`, `driver`, `backend`, `render_size`,
`output_size`, `preset`, `input_sha256`, `exposure_mode`, and `timing_scope`.
All except build_id must match. These identities are caller-provided provenance,
not independently detected hardware settings. Also record wave size, clocks,
power/thermal state, instrumentation and run order for real experiments.

Pass matching is by name, never dispatch index or workgroup dimensions. Cross-API
FP8/INT8 or differing pass graphs require an independently reviewed semantic map;
this tool refuses to invent one. The sum of pass medians is not full frame time.
Pixel-hash comparison is tri-state (match / differ / unavailable), not an RMSE
gate; compare raw output frames with our existing numerical validation tools.
Timing changes are descriptive, not statistical proof of a speedup.

`auto_exposure` is the legacy profiler's pass-zero label even when the external
exposure adapter occupies that slot. Always retain exposure_mode in metadata.
Use the profiler only in the isolated, serialized harness: it retains only the
last frame, adds timestamp overhead, and multiple model contexts can overwrite
the same CSV path. It is not a complete game benchmark.

## Verify the imported tools and run CPU tests

```sh
python3 tools/optimization/catalog.py third_party/optiscaler-member-tools-20260916/original --output build/optimization-tooling/catalog.json
cmp third_party/optiscaler-member-tools-20260916/manifest.json build/optimization-tooling/catalog.json
python3 -m unittest discover -s tools/optimization -p 'test_*.py' -v
```

The catalog reads bytes only and rejects symlinks; it skips Finder metadata.
The imported EXEs remain inert reference files, not dependencies of these tools.
