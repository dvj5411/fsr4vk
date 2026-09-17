# Initial RDR2 support (0.3.2.4)

RDR2 with the new PR #1161 author build successfully created our provider
context, then failed dispatch because the provider rejected optional reactive
or transparency/composition masks. The host can forward the game's DLSS bias
mask through these fields.

The provider now accepts these optional resources and emits one warning per
context. It does not read, bind, or transition the mask resources. This is
compatibility handling, not mask-guided reconstruction: the current translated
model path does not consume external masks. Required-input validation remains.

## Validation

On a Radeon PRO W6400 with RADV and GE-Proton11-6:

- The old DLL reproduced the rejection with auto and external exposure.
- Ten two-frame synthetic GPU cases passed: no mask, no-mask repeat, reactive,
  transparency, and both, with each exposure mode. Outputs were finite.
- Maximum masked/unmasked RMSE was 0.0015572 (existing gate 0.002); less than
  0.405% of values differed by over 0.001 (gate 5%). Independent no-mask runs
  also varied. Sparse maximum errors reached 0.5535; this is not byte-exact
  determinism or comprehensive image-quality validation.
- Six actual DLL logging cases and CPU logging/feature-chain tests passed.
- All runtime model/shader payloads are unchanged from the preceding release.
- The user confirmed RDR2 launches with the fix. Long-session image quality,
  other hardware and native Windows RDR2 remain unvalidated.

## Provider logging

File logging is off by default. In Steam/Proton, prepend `FSR4_VK_LOG=1` to
existing launch options, preserving DLL overrides. This uses Windows `%TEMP%`
inside the prefix for `fsr4vk-provider-<pid>.log`. A nonempty `FSR4_VK_LOG_PATH`
also enables logging and selects the file, even if `FSR4_VK_LOG=0` is present.
Remove both options to disable file writes. Existing logs are not deleted.
The host's FFX error/warning callback remains active without file logging.

## OptiScaler binary

Use the supplied PR-author build. It was not rebuilt or patched for this release.
The previous experimental RDR2 legacy-BDA enumeration workaround is parked for
possible later investigation, not included in the release. The author's exact
matching source is not yet available; see `../SOURCE_PROVENANCE.md`.
