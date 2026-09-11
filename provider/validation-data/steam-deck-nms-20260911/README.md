# Steam Deck / NMS COSMOS device negotiation

NMS build `25233815`, Steam Deck LCD / RADV VANGOGH, Mesa
`26.2.0-devel (git-035ae2f854)`, Proton Experimental `11.0-100`.
The game was launched normally from Gaming Mode. No launch options were added.

## Failure evidence

The frozen `OptiScaler.log` spans 19:18:24 to 19:20:06 on 2026-09-11.
OptiScaler is `e7ee0b4`; the selected provider is the split-name DLL in
`Binaries/OptiScaler` and reports version 4.0.2. Relevant lines:

```text
707: FfxApiProxy::InitFfxVk LoadResult: true
708: FfxApiProxy::VersionVk FfxApi Vulkan version: 4.0.2
710: hkvkCreateDevice Vulkan FFX provider device negotiation failed: 3 - unhandled device pNext sType=1000044003
22643: IFeature::SetInitParameters Render Resolution: 752x480, Display Resolution 1280x800, Quality: 1
22660: FFXFeatureVk::InitFFX _createContext error: The underlying runtime (e.g. D3D12, Vulkan) or effect returned an error code.
22744: FeatureProvider_Vk::ChangeFeature init successful for FSR 2.1.2, upscaler changed
```

`1000044003` is `VkPhysicalDeviceDynamicRenderingFeatures`, also exposed through
the KHR typedef/enum aliases. The provider checked physical capabilities, then
failed to copy this game-owned node. OptiScaler continued with the original
device create-info, leaving the provider requirements unapplied. Subsequent FSR4
context creation failed. This trace does not establish a Gamescope or shader bug.

## Change and regression

The provider now copies the typed dynamic-rendering structure without changing
its feature value. Core and KHR aliases use the same case. No OptiScaler change
is needed. Unsupported-node errors include the complete bounded input type
sequence to make a later incompatibility diagnosable in one log.

CPU tests cover true and false values under API 1.1, 1.2, and 1.3, independent
storage, unchanged source, retained neighbours, aggregate Vulkan 1.3 behavior,
unknown-node diagnostics, and cycle rejection. The new tests against the old
header reproduce the exact `1000044003` failure; patched tests pass, including
AddressSanitizer/UndefinedBehaviorSanitizer. The promoted-command and shader
envelope checks also pass.

`probes/vulkan-device-negotiation-probe.cpp` tests the loaded Windows DLL's
actual prepare/release query ABI and creates a real Vulkan device. It performs
no GPU submission. On the Deck, using Proton Experimental's Wine binary and an
isolated debug prefix:

| Provider | Prepare | Device creation | Probe exit |
| --- | --- | --- | --- |
| Previously installed DLL | 3, unhandled `1000044003` | Not attempted | 1 |
| Patched DLL | 0 | Success, dynamic rendering preserved | 0 |

The existing Windows ABI probe passes too: five exports, 4.0.2c Vulkan INT8
enumeration, dummy-device rejection, FSR 3.1.4 compatibility ID, and unknown-ID
rejection. Initial Proton-script attempts produced no useful probe result;
only the explicit Wine-binary runs above count as runtime evidence.

## Deployment for the next manual test

The game was stopped. Both existing split-name provider copies were backed up
and replaced. The installed provider SHA-256 is:

```text
0a488ebe64d5148f6ce6361f24fbefe583ca91338580319e7e4109c7a0d37a77
```

Updated paths relative to the active NMS installation:

- `Binaries/amd_fidelityfx_upscaler_vk.dll`
- `Binaries/OptiScaler/amd_fidelityfx_upscaler_vk.dll`

OptiScaler (`dxgi.dll`), the separate legacy-name DLL, INI and launch options
were preserved. The ten model bundles (170 payload paths, 151 unique payloads)
remain embedded. This candidate includes the preceding general-resolution
cleanup at `4305d7d` and has not yet been verified for in-game dispatch or image
quality on the Deck.

On the Deck, rollback files are in
`/home/deck/fsr4-vulkan-debug/before-dynamic-rendering-20260911`; the frozen game
capture is in `captures/20260911-192317` under that debug root. The local ignored
copy is in the canonical checkout's `build/steam-deck-debug/20260911-192317`.

Next: launch normally from Gaming Mode, enter a save, select FSR4, let it render,
then exit. Check a fresh log for successful device preparation, context
creation and continuing successful FFX evaluations. If anything fails, preserve
the first error and full device-chain diagnostic before making another change.
