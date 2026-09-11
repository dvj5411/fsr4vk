# RTX 3070 Ti validation, 2026-09-11

Windows DLL SHA-256:
`2089da4a57dd77b8bc50100153373143853096e57eecf9e57e31c1abbbea679c`

GPU: NVIDIA GeForce RTX 3070 Ti, proprietary driver 610.57.04.
Host: Bazzite. Windows execution: Proton-CachyOS Latest x86_64_v3.
The device reports accelerated packed signed 4x8-bit integer dot products.

| Suite | Cases | Frames | Worst RMSE | Worst fraction above 0.001 |
| --- | ---: | ---: | ---: | ---: |
| Native Vulkan vs original DLL | 10 | 20 | 0.000643988 | 0.00527561 |
| Embedded Windows DLL vs original DLL | 10 | 20 | 0.00195624 | 0.00549057 |

Both suites pass every case with zero nonfinite pairs and no Vulkan validation
errors. Thresholds remain RMSE <= 0.002 and fraction above 0.001 <= 0.05.
Cases cover native AA, quality, balanced, performance and ultra-performance at
1920x1080 and 3840x2160 output. The JSON files retain all individual metrics,
including maximum errors; aggregate thresholds do not imply pixel identity.
Reference/candidate repeat variance at the left edge remains a known limitation
of the original workload. No pixels or channels were excluded.

The Windows DLL also passed the 19-frame NMS-format stress probe: eight pending
frames, clean rejection of a ninth pending frame, slot reuse, temporal history,
and a later reset. `depth_target=cleanly-rejected` and the corresponding provider
message are deliberate negative-test results. Output remained finite. The CSV
contains final-frame GPU timestamps from this validation run, not a benchmark
or a measurement of NMS frame rate.

The recorded reference uses the original 4.0.2c DLL, with normal NVIDIA driver
workarounds, physical constant-buffer addressing forced to match the provider
ABI, and NVIDIA-specific raw access chains disabled. Source identities and
capture settings are in `assets/portable/provenance.json` and its manifests.

NMS gameplay passed: the user loaded a save and confirmed FSR4 working.
Direct observation showed the rendered scene and the OptiScaler overlay
identifying RTX 3070 Ti and FSR 4.0.2. `nms-gameplay.provider.log` records
five fresh context/first-dispatch groups using `portable-int8` at
1280x720 -> 1920x1080. The installed DLL hash matched the value above.
The initialization log records provider discovery and successful device
preparation. See `NVIDIA-VALIDATION.md` for scope and launcher cleanup details.
