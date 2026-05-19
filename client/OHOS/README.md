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
- `ohos_ime.*`: dispatches committed IME text through FreeRDP Unicode keyboard
  events.
- `ohos_pointer.*`: maps HarmonyOS surface-local pointer events into RDP pointer
  packets, including viewport-to-desktop coordinate mapping, button flags and
  wheel direction.
- `ohos_certificate.*`: owns HarmonyOS certificate-policy parsing and FreeRDP
  certificate callback decisions for TOFU, strict and ignore modes. The HAP
  stores app-local certificate files and relays callback logs.
- `ohos_display.*`: normalizes the XComponent size and sends `disp` monitor
  layout updates.
- `ohos_clipboard.*`: owns the HarmonyOS Pasteboard backend and `cliprdr` client
  callbacks for text clipboard redirection. The HAP keeps only permission and
  lifecycle wiring.
- `ohos_graphics.*`: owns HarmonyOS graphics-mode parsing, fallback policy and
  H.264 desktop alignment helpers.
- `ohos_compositor.*`: owns HarmonyOS render-target state, AVC420 direct
  Surface route state and graphics diagnostics. AVC444 is intentionally routed
  through FreeRDP native `avc444_decompress()` into the GDI/RGB surface before
  the existing OHOS display path presents it.
- `ohos_rdpgfx.*`: owns the HarmonyOS RDPGFX callback bridge, AVC420 surface
  decisions, AVC444 negotiation diagnostics and codec diagnostics. The HAP
  supplies only NativeWindow/AVCodec surface callbacks.
- `ohos_session_config.*`: owns the HarmonyOS connection defaults, enhanced RDP
  settings and standard channel request parameters for cliprdr, display-control,
  rdpsnd/OHAudio playback and audin/OHAudio capture. The HAP validation shell
  passes host/user input and the selected graphics mode, then receives log
  strings without hard-coding those FreeRDP/channel parameters.

Planned migration:

- A stable public include/install story for the OHOS helper headers if this
  branch becomes a reusable SDK rather than a repo-local port.
