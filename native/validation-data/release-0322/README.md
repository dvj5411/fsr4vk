# 0.3.2.2 combined provider validation

W6400 / RADV NAVI24, Mesa 26.2.2. Native: ten cases, twenty dispatches,
all six presets, Auto, transitions into/out of DRS, and 4K DRS. Windows DLL:
four cases, eight dispatches through UMU / GE-Proton11-5, without an external
asset-root setting. All completed with finite output, expected active preset,
and successful cleanup. These are functional checks, not new FPS measurements
or a replacement for the matched numerical matrix in ../drs-20260915.

Validated embedded provider SHA-256:
`e30aed4cb9b931836bba22593fb0aa2ae007d1b0eddcb64b506537b6599f0290`.
384 embedded paths, 323 unique payloads. Shader arithmetic is unchanged from
the validated DRS preparation. The optimisation bypasses the CPU active-model
map lookup only. Shader spill/scheduling experiments remain excluded.

An initial packaging test accidentally used a non-embedded developer build;
that failed before dispatch and was not released. The windows/ evidence here
is the corrected self-contained build, not that failed candidate. Fresh game,
Steam Deck, NVIDIA and native-Windows checks were not performed for this build.
