# viewer

The plain-JavaScript viewer in `viewer/` is served by the [control-plane](control-plane.md) and supports Chrome 79 and later, with newer APIs feature-detected. `probe.html` remains a separate codec probe.

Open the viewer to configure a connection, then choose **Connect**. Instance defaults to `default` and control plane defaults to the current host. Settings are saved in browser localStorage, including the access token; use a trusted browser profile. URL parameters override saved values. Fullscreen on connect is enabled by default and requests landscape orientation when supported. Browser or device restrictions can prevent either request.

The session fills the viewport with the remote video letterboxed as needed. Use the small × button to disconnect and return to settings. Connection errors also return there. The remote keyboard button appears when needed for touch keyboard/IME access. Press **Ctrl+Alt+S** to toggle the stats overlay; it is hidden by default. `stats=1` shows it initially.

Connection parameters: `instance`, `token`, `cp=<host:port>`, `relay=<TURN URL>`. Advanced settings expose `measure=1|<seconds>` (pixel-marker latency or field test), `scenario`, `presenter=clean|canvas`, `delay=<ms>|off`, `stats=1|0`, `report=<seconds>`, `my=<px>`, `warmup=<seconds>`, `autoinput=1|0`, `trace=1|0`, `hwinfo=1|0`, and `field=1|0`. Unrecognized query parameters are preserved for external tools. The receiver prefers H.264, with VP8 as fallback; there is currently no codec-preference URL parameter. The optional [field overlay](tools.md#field-test) provides its own measurement controls when field mode is enabled.

`input.js` captures mouse, touch, pen, wheel and keyboard input with letterbox-aware coordinate mapping. `ime.js` bridges the remote soft keyboard and IME. `protocol.js` handles binary input; `presenter.js` contains opt-in presentation experiments. Receiver latency hints and codec selection are described in [protocol](protocol.md#5-receiver-latency-hints-chrome-89).
