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
- `ohos_display.*`: normalizes the XComponent size and sends `disp` monitor
  layout updates.
- `ohos_clipboard.*`: owns the HarmonyOS Pasteboard backend and `cliprdr` client
  callbacks for text clipboard redirection. The HAP keeps only permission and
  lifecycle wiring.
- `ohos_graphics.*`: owns HarmonyOS graphics-mode parsing, fallback policy,
  H.264 desktop alignment and RDPGFX AVC420/AVC444 capability/surface-command
  decisions.
- `ohos_session_config.*`: owns the HarmonyOS default RDP settings and
  standard channel request parameters for cliprdr, display-control,
  rdpsnd/OHAudio playback and audin/OHAudio capture. The HAP validation shell
  passes the selected graphics mode and receives log strings, but does not
  hard-code those channel parameters.

Planned migration:

- GPU composition and multi-surface AVC444 routing beyond the current
  AVCodec/surface policy helpers.
- A stable public include/install story for the OHOS helper headers if this
  branch becomes a reusable SDK rather than a repo-local port.
