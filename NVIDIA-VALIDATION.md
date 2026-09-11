# NVIDIA portable pre-pass candidate

Branch: `codex/nvidia-portable-int8`, based on `63c5beb`.

Status on 2026-09-11: Windows DLL built; static shader and device-negotiation tests pass. NVIDIA GPU execution and NMS validation are pending SSH access. This is not yet a validated NVIDIA release.

The DLL embeds ten portable pre-pass variants alongside all original assets. Devices exposing the VALVE mixed-dot extension and feature use the original pre-pass. Other devices use FP16 inputs widened to FP32 multiplication and addition, keeping the remaining INT8 shaders unchanged. The device prepare query uses the same capability decision and no longer requires/enables VALVE features on unsupported devices.

The generated shaders use explicit FloatControls2 fast-math masks to prevent contraction/reassociation of the replacement operations. NoContraction is incompatible with the existing FPFastMathDefault execution modes. Numerical equivalence to the AMD implementation remains a hardware test requirement.

## Checks completed

- Ten converted pre-passes pass current Khronos `spirv-val` for Vulkan 1.1 and 1.3.
- All 170 original payload paths retain their original SHA-256; 140 other shader files remain byte-identical.
- Converted pre-passes contain no VALVE mixed-dot capability, extension or instruction. Descriptor/layout and entry-point ABI instructions are unchanged.
- Device feature-chain tests cover extension absent, feature absent, both present, and preserved application feature chains.
- MinGW x86-64 DLL builds with embedded assets and all five FFX exports. A Windows DLL-loading dispatch probe is also built.
- No target GPU pipeline creation, numerical comparison, performance measurement, or NMS dispatch has yet been verified.

## Reproduce local checks

```sh
python3 tools/test-portable-shaders.py --spirv-val /path/to/spirv-val
c++ -std=c++20 -Wall -Wextra -Wno-missing-field-initializers \
  -I"$VULKAN_SDK/include" native/fsr4-device-features-test.cpp \
  -o /tmp/fsr4-device-features-test
/tmp/fsr4-device-features-test
./tools/build-vulkan-provider-windows.sh
```

`native/dispatch-smoke.cpp` is imported from the research checkout's existing NMS/temporal probe and adapted to select the portable pre-pass. For a sidecar test, copy `build/provider-windows/embedded/portable/<tier>/<preset>/pass-01.portable.spv` alongside the corresponding original shader bundle. Normal DLL deployment requires no sidecars.

## Resume on the NVIDIA machine

1. Establish authenticated access and record device/driver identity, current NMS install path, launcher, Proton/native-Windows setup and existing OptiScaler files. Back up replaced files and launcher settings.
2. Query current features plus FP16 denormal preservation, subgroup quad support and packed signed INT8 acceleration. The new shader path still needs all non-VALVE features in `DeviceFeatures`.
3. Run the dispatch probe with validation, including `--provider-nms-formats` and `--provider-temporal`. Compare against the existing reference oracle; inspect finite output and temporal behavior.
4. Deploy the candidate DLL through the existing OptiScaler Vulkan integration and run NMS. Require fresh logs showing `shader_backend=portable-fp32-dot2-int8` and successful dispatch, plus visible in-game rendering. A loaded DLL alone is insufficient.
5. Investigate any further target errors, validate all presets/resolution tiers, record image/performance results, and package the verified DLL with its checksum.

SSH access at the checkpoint: the supplied key pair was moved from Downloads to `~/.ssh/nvidia_nms_ed25519{,.pub}` without replacing existing keys; private-key mode is 600. Public/private fingerprints match, but `admin@100.89.27.68` rejects the offered key. Confirm the account/address and install the public key in that account's `authorized_keys` before resuming. No authentication secrets are stored in this repository.
