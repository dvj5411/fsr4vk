# Initializer locality

Immutable model initializer buffers now prefer compatible memory with all three
properties: DEVICE_LOCAL, HOST_VISIBLE and HOST_COHERENT. The original compatible
host-visible/coherent allocation remains the fallback when no such local type
exists, or when the preferred allocation/mapping fails with device-memory
exhaustion or a mapping failure. Other errors still propagate. UMA devices may
already use a local type. Only initializers change placement, not descriptors,
frame constants, scratch, weights or images. No shader/model payloads change.

Mapping occurs before binding so a failed preferred mapping can be safely freed
and retried. Device-address flags and memory-type compatibility are preserved.
Uploads remain once per model-context creation, not per dispatch. No new Vulkan
extensions, enabled features, OptiScaler changes or launch arguments are needed.
Optional provider logs report `initializer_memory` with type, property flags,
device-local status and whether fallback occurred.

CPU policy tests: `VULKAN_SDK=/path/to/sdk bash tools/test-buffer-memory-policy.sh`.
The tests cover missing/incompatible types, UMA, allocation/map failure and
cleanup, fallback failure, BDA metadata and propagation of unrelated errors.

W6400 synthetic verification covered 96 cases across native/portable backends,
linear/nonlinear/sRGB/PQ, both capacity tiers and all six presets including
FSRAA/DRS. All 192 saved frames matched the corrected baseline exactly. Twelve
standalone Windows DLL preset/tier cases and four queued color-switching cases
passed under Proton; two injected preferred-allocation/mapping failure cases
recovered correctly. Existing original-oracle numerical exceptions are unchanged.
The user additionally confirmed the tested build functions in No Man's Sky.

Three alternating 48-frame timing pairs (eight warmups discarded) on W6400:

| Quality render -> output | Control median | Optimized median | Reduction |
| --- | ---: | ---: | ---: |
| 1280x720 -> 1920x1080 | 9.56584 ms | 9.14968 ms | 4.35% |
| 1706x960 -> 2560x1440 | 13.14990 ms | 12.21816 ms | 7.09% |

All 576 timed frame hashes matched. These are isolated native-Linux upscaler GPU
timings, not whole-game FPS measurements. A locality bottleneck is supported by
the per-pass response; no PCIe/cache traffic counters were collected. Gains on
other devices are not established. This patch has no new native-Windows, Deck
or physical NVIDIA validation.
