# viewer

The plain-JavaScript viewer in `viewer/` is served by the [control-plane](control-plane.md) and supports Chrome 79 and later, with newer APIs feature-detected. It is static files only (`index.html`, `viewer.css`, scripts); no build step or CDN. `probe.html` remains a separate codec probe.

## Connect screen

Open the viewer, pick an instance and choose **Connect**. The screen follows the system light/dark preference and works at phone width.

- **Connection** — the instance list comes from `GET /api/instances` (sent with `Authorization: Bearer <token>` when a token is entered) and shows each instance's name, ID, state badge (`running`, `stopped`, `starting`, `backoff`, `crashed`) and whether another viewer is using it. Use the refresh button to reload it; it also reloads when the token or control plane changes. The **Instance** field stays editable: an ID from the URL or typed by hand is kept even when the server does not list it. Empty means `default`. A missing or rejected token, or an unreachable server, is explained under the list. The access token field can be revealed with the eye button; it is never shown elsewhere in the UI. **Fullscreen on connect** (on by default) also requests landscape orientation where supported.
- **Display & network** — stats overlay, receive delay (lowest = 0 ms, custom milliseconds, or browser default), presenter (Standard / Clean / Canvas) and an alternative control plane.
- **Diagnostics & measurement** (developer) — latency measurement (off, pixel marker, or a timed field run), scenario label, report/warm-up durations, marker row, synthetic input, field test panel, decoder info, relay clock trace and relay URL. These are for testing and are not needed for normal use.

Sections open automatically when the URL or saved settings already use one of their options. Settings are saved in browser localStorage, including the access token; use a trusted browser profile. URL parameters override saved values. When a session ends with an error the screen shows the reason with **Retry**.

Instances and profiles are created and started through the [control-plane API](control-plane.md#instances-and-profiles); the viewer only lists them.

## Session

The remote video fills the viewport, letterboxed as needed. A small handle at the top centre opens an auto-hiding toolbar: connection dot and instance ID, **Stats**, **Keyboard** (touch devices; reopens the soft keyboard for a focused remote text field), **Fullscreen** and **Disconnect**. The toolbar also appears briefly after connecting and when the mouse rests at the top edge; it hides after a few seconds or when you touch the remote page. Toolbar buttons never take keyboard focus from the remote page.

A status chip at the bottom shows *Connecting*, *Waiting for the remote browser*, *Starting video*, *Reconnecting* or *Tap to start video* (tap it) and disappears once video plays. The round keyboard button at the bottom right appears when the remote page wants the soft keyboard but the browser could not open it automatically.

The stats overlay (top left) shows fps and resolution, then bitrate, codec/decoder, RTT, jitter buffer, decode time, loss counters, the receiver hint and, when available, clock sync, latency percentiles and sender stats. Toggle it from the toolbar or with **Ctrl+Alt+S**; `stats=1` shows it initially.

## URL parameters

The form edits exactly these query parameters; unrecognised parameters are preserved for external tools.

| Parameter | Values | Control |
|---|---|---|
| `instance` | instance ID, default `default` | Instance list / field |
| `token` | access token | Access token |
| `cp` | `host:port` | Control plane |
| `relay` | TURN URL, e.g. `turn:host:port` | Relay URL |
| `connect` | `1` connects immediately, skipping the settings screen (used by tools); removed on disconnect | — |
| `stats` | `1` shows the overlay | Show stats overlay |
| `delay` | milliseconds, or `off`; absent = 0 | Receive delay |
| `presenter` | `clean` or `canvas`; absent = standard | Presenter |
| `measure` | `1` pixel marker; `5`–`3600` seconds = field run | Latency measurement |
| `scenario` | label | Scenario |
| `report` | seconds; POSTs a summary to `/api/report` | Report after |
| `warmup` | seconds, default 3 | Warm-up |
| `my` | source pixel row; default auto-detect | Marker row |
| `autoinput` | `1` synthetic mouse moves every 97 ms | Synthetic input |
| `field` | `1` field test panel | Field test panel |
| `hwinfo` | `1` request media permission to expose decoder details | Decoder info |
| `trace` | `1` relay clock trace (needs `relay`) | Relay clock trace |

Switches write `1` when on and remove the parameter when off. The receiver prefers H.264, with VP8 as fallback; there is no codec-preference parameter. The optional [field overlay](tools.md#field-test) provides its own measurement controls when field mode is enabled.

## Files

`settings.js` runs the connect screen; `session-ui.js` the toolbar, status chip and stats toggle; `viewer.js` signalling, the peer connection and stats. `input.js` captures mouse, touch, pen, wheel and keyboard input with letterbox-aware coordinate mapping. `ime.js` bridges the remote soft keyboard and IME. `protocol.js` handles binary input; `presenter.js` contains opt-in presentation experiments. Receiver latency hints and codec selection are described in [protocol](protocol.md#5-receiver-latency-hints-chrome-89). Element IDs used by tools (`video`, `stage`, `status`, `overlay`, `kbd`, `ime`, `ime-password`, `settings-form`, `connect`, `disconnect`) are stable.
