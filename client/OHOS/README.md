# FreeRDP OHOS client adaptation

This directory is the landing zone for HarmonyOS platform-client code that must
ship with the FreeRDP source adaptation rather than the demo HAP.

The HarmonyOS application should keep only UI, permissions, surface handles and
user configuration. RDP semantics such as keyboard mapping, cliprdr bridging,
display-control helpers, rdpgfx/codec policy and audio backends belong in this
FreeRDP OHOS client/backend layer.

Current contents:

- Public SDK headers:
  - `ohos_session.h`, `ohos_session_options.h`, `ohos_session_config.h`: opaque
    session lifecycle, option normalization, connection settings and standard
    channel defaults.
  - `ohos_audio.h`: OHOS rdpsnd/audin diagnostics and the microphone permission
    callback registration point.
  - `ohos_clipboard.h`: Pasteboard-backed `cliprdr` registration and read
    permission callback.
  - `ohos_display.h`: `disp` monitor layout and resize helpers.
  - `ohos_graphics.h`: graphics-mode parsing, fallback policy and H.264 desktop
    alignment helpers.
  - `ohos_ime.h`, `ohos_keyboard.h`, `ohos_pointer.h`, `ohos_input_queue.h`:
    input translation and queued dispatch helpers.
  - `ohos_rdpgfx.h`, `ohos_avc420_route.h`: RDPGFX bridge, AVC444 surface
    callbacks and AVC420 direct surface route helpers.
  - `ohos_certificate.h`: certificate policy parsing and callback registration.
- Internal headers:
  - `ohos_session_private.h` and `ohos_rdpgfx_internal.h` are implementation
    details and are not installed as SDK headers.
  - Any future `*_internal.h` header in this directory must stay private unless
    it is deliberately promoted in the SDK quickstart.
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
- `ohos_avc420_route.*`: owns HarmonyOS render-target state, AVC420 direct
  Surface route state and graphics diagnostics. AVC444 is intentionally routed
  through FreeRDP native `avc444_decompress()` into the GDI/RGB surface before
  the existing OHOS display path presents it.
- `ohos_rdpgfx.*`: owns the HarmonyOS RDPGFX callback bridge, AVC420 surface
  decisions, AVC444 negotiation diagnostics and codec diagnostics. The AVC444
  GPU compositor is the default path for `rdpgfx-h264`; it suppresses native GDI
  per command only after the HAP callback has consumed that command. The HAP
  supplies only NativeWindow/AVCodec surface callbacks.
- `ohos_session_config.*`: owns enhanced RDP settings and standard channel
  request parameters for cliprdr, display-control, rdpsnd/OHAudio playback and
  audin/OHAudio capture. It also registers the fixed OHOS Download directory
  drive as `\\tsclient\Downloads` when the directory is available.
- `ohos_session_options.*`: owns raw HAP option normalization, including
  username/domain splitting, port and desktop-size parsing, graphics-mode and
  certificate-policy parsing, H.264 desktop alignment and FreeRDP storage paths.
- `ohos_session_input.*`: owns the public session pointer/key/text/resize
  dispatch entry points.
- `ohos_session.*`: defines the public opaque HarmonyOS session API surface.
  It owns the FreeRDP instance/context lifecycle, standard OHOS settings,
  channel requests, connect/disconnect and event loop. The HAP supplies UI,
  surface, certificate, clipboard and input-pump callbacks.

Install/API boundary:

- OHOS public headers are installed under
  `include/freerdp3/freerdp/client/ohos/` for FreeRDP 3 builds. Third-party
  callers include them as `<freerdp/client/ohos/ohos_session.h>` after adding
  the FreeRDP include root.
- Public headers must not include ArkUI, N-API, Demo HAP classes or other HAP
  private C++ types. Platform-specific objects such as NativeWindow stay behind
  callback `void*` handles owned by the application.
- Keep `READ_PASTEBOARD` and `MICROPHONE` permission prompts in the app layer.
  The FreeRDP OHOS layer only calls permission callbacks at the protocol point
  that actually needs the capability.
- Keep Download directory authorization in the app layer. The FreeRDP OHOS layer
  derives the fixed public Download subdirectory itself and must not receive
  arbitrary ETS paths for drive redirection.
