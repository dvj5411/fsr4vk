# Preset control (0.3.2.2)

`ffx_vk_preset_query.h` defines optional descriptors on the existing `ffxQuery`
and `ffxConfigure` exports. They are project-specific, not AMD SDK descriptors.

- Query capabilities: mask `0x3f` advertises presets **0 through 5**, including DRS.
- Configure preset: 0 native, 1 quality, 2 balanced, 3 performance, 4 DRS,
  5 ultra performance. `FSR4VK_PRESET_AUTO` restores automatic selection.
- Auto honors the context's `FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION` flag; without
  that flag it uses the existing render/output ratio rules. An explicit override
  takes precedence, without changing the game's input/output resolution.
- Query active preset: last successfully recorded model, not the pending request
  or proof of GPU completion. The unknown sentinel applies before first dispatch.
- Configuration, dispatch, query and destruction are serialized per context.
  Unsupported numeric IDs return an error without changing the previous setting.

Changing models uses the existing temporal reset and lazily allocated core cache.
The small CPU optimisation caches the active core pointer, bypassing the map
lookup on repeated frames. Other cached cores stay owned and alive, including
across failures. No arithmetic, shader ordering, GPU synchronisation or assets
are changed by this optimisation. No measured GPU/FPS speedup is claimed.

The experimental scalar-spill shader scheduler (`7aa4a0e`) is **not** included:
its long temporal stability/oracle gates are unresolved. Preset 4's matched
native/portable/Windows results are recorded under
`native/validation-data/drs-20260915`. Arithmetic backends require matched oracles.
The existing aggregate numerical gates remain unchanged; byte identity is not
claimed, and previously observed Auto-repeat variation remains documented.

The manual OptiScaler patch adds a capability query and context configuration,
then reports the active model after dispatch. The existing upstream preset menu
is left untouched: no Preset 4 exclusion. Our personal fork additionally removes
its obsolete disabled/provider-managed menu and ratio-based preset guess.
An unmodified upstream build cannot send the new preset setting.
