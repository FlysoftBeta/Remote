# viewer

Plain-JavaScript web client in `viewer/`, served by the [control-plane](control-plane.md). It must run on **Chrome 89+** (newer APIs only
behind feature detection); iOS Chrome (WebKit) is not the baseline.

| File | Role |
|---|---|
| `index.html`, `viewer.js` | page, signalling, PeerConnection answer, `<video>`, `ctl`/`probe` channels, stats overlay, latency measurement mode |
| `protocol.js` | binary input encode/decode and constants, shared with Node ([protocol](protocol.md#22-binary-input-format-input-little-endian)) |
| `input.js` | mouse/touch/pen/wheel/keyboard capture; letterbox-aware normalised coordinates (`object-fit: contain`); sender-side move coalescing; RESET on blur |
| `ime.js` | remote soft keyboard and IME bridge: hidden 1x1 textarea, `ime.*` messages, show-button fallback outside user activation |
| `presenter.js` | opt-in presentation experiments (`?presenter=clean` or `canvas`); rejected as defaults ([decisions](decisions.md) D15) |

URL parameters: `instance`, `token`, `cp=<host:port>`, `measure=1|<seconds>` (pixel-marker latency, tap-to-start field mode), `report=<s>`
(POST summary to `/api/report`), `my=<px>` (fixed marker row), `delay=<ms>|off` (receiver hint, default 0), `stats=0`, `scenario`.
The field overlay `tools/field-test/viewer-field.js` loads before `viewer.js` ([tools](tools.md#field-test)).

Receiver latency: applies `playoutDelayHint`/`jitterBufferTarget` = 0 and `setCodecPreferences` (H.264 first); the real low latency comes from
the RTP playout-delay the endpoint sends ([protocol](protocol.md#5-receiver-latency-hints-chrome-89)).
