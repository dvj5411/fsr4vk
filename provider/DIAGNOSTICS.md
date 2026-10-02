# Provider diagnostics (schema 2)

Enable `FSR4_VK_LOG=1` as an environment variable. For Steam launch options:
`FSR4_VK_LOG=1 %command%`. In Lutris, add key `FSR4_VK_LOG` with value `1` under
System options / Environment variables, not the executable arguments field.
The Windows DLL writes `fsr4vk-provider-<PID>.log` next to the game's executable,
falling back to the process temporary directory if that directory is unwritable.
An explicit `FSR4_VK_LOG_PATH` still overrides the default and enables logging.
Logging remains off by default. Rejection messages also go to the host's
`fpMessage` callback even with file logging disabled.

## What the log distinguishes

- `context=0 device negotiation`: exact features/extensions absent from the
  host's original device-create request and added to the provider-owned copy.
  This is only available when the host calls `PREPARE_DEVICE`; it is not proof
  the host used the copy or that `vkCreateDevice` succeeded.
- `UNSUPPORTED_DEVICE_CAPABILITIES`: all unavailable required feature bits and
  device extensions from the physical-support check, not just the first one.
  Unsupported hardware/driver capabilities cannot be fixed by enabling a bit.
- Missing Vulkan command: exact command names plus the associated extension,
  promoted core version and boolean feature. This identifies the contract to
  check, not proof that a particular bit was disabled.
- Resource rejection: input name, missing handle versus incompatible format,
  current FFX format name and numeric ID, accepted formats, and any mismatched
  type, dimensions, depth/stencil usage or state. All invalid inputs are
  reported together, before model/pipeline allocation or command recording.
- First-dispatch snapshot: every required input's presence, format, dimensions,
  mip count, resource flags/usage/state, and dispatch dimensions/pre-exposure.
- Build stamp/schema, model, API version, context input contract, physical
  device properties when the capability check is reached, and failure stage.
- Context destruction: attempts, successful command recordings and failures.
  Command recording is not proof of GPU completion. Driver failures name the
  VkResult (including device loss and allocation failures), not just a number.

Successful-frame metadata is not written each frame. Detailed strings are built
only for failed resource validation; the successful resource-check path keeps
the same format/type/dimension/state acceptance rules.

## Enshrouded evidence and limits

The supplied `fsr4vk-provider-308-2.log` contains two depth rejections and one
motion rejection before any successful dispatch. Model initialization succeeds;
contexts use 4.1.1, Quality and automatic exposure. This is not another external
R16 exposure failure. The Proton log contains provider exception unwinding, but
does not provide the rejected resource descriptors or prove a GPU device loss.
The schema-2 log is needed to identify the actual failing descriptor fields.

Update: the new `fsr4vk-provider-308-3.log` confirms depth-only usage and
ambiguous SNORM motion metadata. See [the input-format candidate](ENSHROUDED-INPUTS.md)
for the native D32 fix, opt-in RG16 SNORM path, and field-test instructions.

The checked-in OptiScaler source maps `VK_FORMAT_R16G16_UNORM/SNORM` to FFX
`R8G8B8A8_UNORM/SNORM`. It also maps several different depth formats to one FFX
format. Therefore a reported FFX format is **host-supplied metadata**, not proof
of the original VkImage format. Vulkan provides no general query that recovers
an arbitrary existing image's creation format. Do not reinterpret those images
or relabel metadata blindly; an ambiguous case still needs the host's original
VkImageCreateInfo or a host-side conversion. OptiScaler is unchanged here.

Similarly, physical feature support is not logical-device enablement. Vulkan
does not expose a general post-creation query for enabled feature bits. Without
provider negotiation, the log explicitly reports that state as unknown and
prints the provider's device-create contract. Host crashes, loader failures
before this DLL runs, GPU hangs and incorrect host metadata can still require
host/Proton/driver diagnostics; this DLL cannot honestly make those observable.
