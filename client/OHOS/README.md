# FreeRDP OHOS client adaptation

This directory is the landing zone for HarmonyOS platform-client code that must
ship with the FreeRDP source adaptation rather than the demo HAP.

The HarmonyOS application should keep only UI, permissions, surface handles and
user configuration. RDP semantics such as keyboard mapping, cliprdr bridging,
display-control helpers, rdpgfx/codec policy and audio backends belong in this
FreeRDP OHOS client/backend layer.

Current contents:

- `ohos_keyboard.*`: maps HarmonyOS key codes to Windows virtual keys and marks
  keys that require extended scancodes. It also owns pressed-key state, modifier
  synthesis, long-press repeat generation and release-all cleanup. The HAP
  native bridge calls this source instead of owning the mapping or keyboard
  state itself.

Planned migration:

- IME Unicode dispatch helper.
- `cliprdr` + OH_Pasteboard backend.
- Display-control resize helper around `disp`.
- RDPGFX/AVC policy glue that keeps the HAP passing only `graphicsMode`.
