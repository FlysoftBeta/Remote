# PT-REVIEW

2026-10-25. Read design/06-protocol.md fully and all 13 requested files.
[V] = verified by source reading or execution; [I] = inferred device/browser effect.
Report only; findings sorted by severity. Compatibility assessment follows the table.

## Findings

| # | severity (high/med/low) | file:line | finding | concrete fix |
|---|---|---|---|---|
| 1 | high | control-plane/index.js:144 | [V] No connected-WebSocket error handler. Installed ws emits error for receiver/protocol failures, including payload-limit failures; unhandled EventEmitter errors terminate CP and all sessions. | Handle socket errors and terminate/clean up only the affected connection. |
| 2 | high | control-plane/endpoint.js:11; control-plane/framing.js:62 | [V] writer.write throws synchronously on a closed/full pipe, before .catch is attached. child.stdin also has no error listener; endpoint transport failure can terminate CP. | Catch synchronous/asynchronous writes, handle stdin errors and end the affected session idempotently. |
| 3 | high | viewer/viewer.js:36; control-plane/index.js:62 | [V] Evicted viewer reconnects after close 4000 and evicts its replacement; two open viewers can repeatedly displace each other. Auth/version errors retry indefinitely too. | Treat replacement/auth/version closes as terminal; make replaced-viewer reconnect explicit. |
| 4 | high | control-plane/index.js:51; viewer/viewer.js:68 | [V] Endpoint replacement suppresses bye to viewer and sends new welcome. Viewer changes sid but reuses its old PC as renegotiation against a fresh remote PC/session. | Notify viewer of replacement and close old PC/state whenever welcome changes sid. |
| 5 | high | viewer/viewer.js:76; tools/mock-endpoint/mock.js:113 | [V] Async negotiations reference mutable global pc/ch/sid. A replacement/disconnect during an await lets old work operate on a new/null PC or signal under a new sid; offers are not serialized. | Capture local PC/session generation, serialize negotiations, discard stale continuations and catch all rejections. |
| 6 | high | viewer/viewer.js:38; viewer/ime.js:57 | [V] PC teardown leaves slots/pending moves and IME focus/lastSeq alive. New endpoint seq 1 after old seq 10 is ignored (VM verified). Textarea key handlers bypass input.enabled. | Reset input/IME session state, blur the editor and gate every forwarding path on active handshake. |
| 7 | high | viewer/input.js:84 | [V] Mouse lostpointercapture sends no cleanup (VM verified), leaving a stolen drag pressed remotely. Touch capture loss cancels only one contact, rather than §2.2 RESET. | Track captures; RESET on unexpected capture loss, distinguishing normal release after up/cancel. |
| 8 | high | viewer/viewer.js:112; tools/mock-endpoint/mock.js:134 | [V] Input enables at SDP answer, before ctl hello. Mock accepts input without handshake; neither peer validates ctl v/input versions. Independent channels need not deliver hello first (§6). | Validate versions and wait for peer hello/input-channel open; gate mock input until handshake completes. |
| 9 | med | viewer/input.js:29 | [V] First touch/pen gets slot 0, also always used by mouse. VM emitted both ptypes with pid 0; concurrent contacts overwrite remote identity/pending moves. | Reserve 0 for mouse and allocate touch/pen 1–15; document contact limit or revise identity format. |
| 10 | med | viewer/input.js:38; viewer/input.js:67 | [V] Chorded button transitions arrive as pointermove, but become coalescible MOVE/button=-1. Extra mouse/pen buttons lack guaranteed discrete down/up. | Diff buttons masks and emit immediate per-button transitions before ordinary movement; see W3C reference. |
| 11 | med | viewer/viewer.js:70; tools/mock-endpoint/mock.js:126 | [V] ICE is dropped without PC, otherwise applied before awaiting remote description. Mock gathers while awaiting setParameters before offer. Both peers ignore candidate:null. | Queue ICE per session/generation, drain after remote description and process null. |
| 12 | med | control-plane/index.js:77 | [V] Viewer sid is overwritten, not checked. Delayed old-session answer/ICE/restart is relabeled into a new session; VM confirmed old-sid restart forwarding. | Require m.sid === I.sid, as already done for endpoint messages. |
| 13 | med | viewer/viewer.js:102 | [V] Each failed transition sends restart; no single-attempt guard or ten-second terminal timeout (§1.3.6). Failed sessions remain allocated. | Allow one restart, set a ten-second deadline, cancel on recovery and send bye/close on expiry. |
| 14 | med | tools/latency/marker.js:31; tools/latency/measure.js:29 | [V] Draw rounds/clamps width/160; measurement uses the raw fraction. 1366px synthetic marker drawn at cell 9 failed at sample cell 8.5375; 1920/1280/640 passed. | Share identical integer geometry, validate 74 cells fit and communicate cell size if necessary. |
| 15 | med | tools/latency/marker.js:44 | [V] No sample bounds/finite-luma validation. Empty pixels produce NaN, bypass sync rejection, decode zero bits and pass zero CRC; fixture returned an all-zero marker. | Validate buffer/stride/cell/sample bounds and reject nonfinite luma before threshold/CRC. |
| 16 | med | tools/latency/measure.js:45 | [V] Only exact echoed inputSeq gets i2p; earlier superseded inputs are never sampled. Sending 1/2 then observing 2 left 1 pending. Violates first acknowledged seq >= s rule and biases percentiles. | Sample/retire all acknowledged pending inputs with wrap-aware ordering and bounded age; clear pending/dedupe state at session/reset. |
| 17 | med | viewer/input.js:67; viewer/input.js:98 | [V] Queued move gets seq before transmission. Move then immediate key emitted wire seq [2,1]; multi-pointer flush orders by pid. Ack/loss tracking cannot assume monotonic seq. | Assign sequence at actual transmission after coalescing; preserve required per-pointer flush ordering. |
| 18 | med | viewer/ime.js:83; viewer/ime.js:95 | [V] Unhandled beforeinput types/input-only edits are cleared without forwarding. Paste fixture sent no message and restored the sentinel. Word/line deletion becomes one Backspace; no deduped input fallback exists. | Reconcile unhandled input against sentinel, handle paste and deletion semantics, and suppress already-forwarded/composition commits. |
| 19 | med | viewer/index.html:21; viewer/ime.js:24 | [V] Password uses the same textarea, never input type=password (§3.3). autocomplete is forced off for other types regardless of flag. | Swap to a password input with shared bridge/keyTarget; map attributes from flags and reset composition safely. |
| 20 | med | viewer/ime.js:36; viewer/ime.js:53 | [I] Programmatic focus may succeed without opening Android keyboard. Fallback button focuses the already-focused textarea, which may fail to reopen it; an RTT does not guarantee keyboard activation. | Use a controlled blur/refocus in the trusted button gesture; verify reopening on Android M89 and keep fallback until opening is confirmed. |
| 21 | med | tools/mock-endpoint/mock.js:85; tools/mock-endpoint/mock.js:172 | [V] Session teardown preserves field.focused/comp and inputSeq. Next viewer tapping an already-focused field can receive only ime.vk, without ime.state; composition/ack state crosses controllers. | Reset session-local field/composition/ack and stats baseline, or send complete state after each new validated ctl handshake. |
| 22 | med | viewer/viewer.js:68; viewer/viewer.js:81 | [V] welcome.iceServers is discarded; viewer instead reads it from offer, where §1 does not define it. Current CP [] masks future STUN/TURN failure. | Store welcome configuration by sid and use it to construct that PC. |
| 23 | med | control-plane/index.js:124 | [V] URI decoding/path construction is outside the async HTTP handler catch. A decoding exception becomes an unhandled rejection under Node 24 and can terminate CP. | Catch request parsing/path failures at the handler boundary and return 400. |
| 24 | med | viewer/protocol.js:75; viewer/protocol.js:84 | [V] Decoder checks packet length but accepts pid>=16 and KEY codeLen/keyLen>32. Mock treats this decoder as validation, contrary to explicit §6 limits. | Reject out-of-range pid and KEY byte lengths before dispatch; count malformed packets. |
| 25 | low | tools/latency/measure.js:12 | [V] Unguarded willReadFrequently shipped in Chrome 99. M89 ignores this unknown dictionary member: optimization absent, not a runtime blocker. | Document the ignored-hint fallback; measure M89 readback cost and check supported context attributes before claiming optimized readback. |

## Verified correct

- [V] Binary layouts: 8-byte header; POINTER 20; WHEEL 16; KEY 14+codeLen+keyLen; RESET 8 bytes. All offsets agree with §2.2.
- [V] LE scalars, signed button/wheel, quarter-unit wheel scaling, modifiers/flags and timestamp ×10 agree with §2.2.
- [V] Short/unknown packets are rejected and trailing extensions tolerated; fixed negotiated ids 0/1/2 and reliability agree with §2.
- [V] Principal ctl/IME/probe names and fields agree with §§3–4. One-space sentinel places caret after space; backward-delete uses before:1/after:0. Composition cursor uses UTF-16 length; commit uses e.data, excluding sentinel.
- [V] CP sends welcome before session.open; normal offers cannot overtake welcome on viewer WS. Waiting viewer auto-starts when endpoint registers; sid is random 128-bit; allowlists and identity guards protect replaced-connection close callbacks.
- [V] Normal disconnect sends bye; token check uses 4401 without explicit token logging; 15-second ping/missed-pong termination exists.
- [V] Letterbox-aware coordinates, frame-pixel wheel scaling, touch up/cancel freeing, blur/hidden RESET and physical-key preventDefault exist.
- [V] Marker pack is big-endian bits, correct field widths, CRC-8 poly 0x07 over eight bytes. At 1920px, sync threshold and centered sampling work.
- [V] Clock fixture offset +100ms/RTT 10ms returned 100/10 and toLocal(1100)=1000. Offset sign, display conversion, last-32 min-RTT filter and warm-up rate agree with §4.1.
- [V] Nearest-rank percentiles handle empty/singleton inputs; unwrap maps low32=5 near 4294967293 to 4294967301 correctly.
- [V] Stdio framing validates four-byte LE prefix/4MiB limit before allocation, fragmented/coalesced records, fatal UTF-8 and JSON objects. Writer serializes writes with an 8MiB queue bound.

## Chrome 89 compatibility

- [V] Inspected every script loaded by viewer/index.html (including marker/clock/measure), DOM usage and inline CSS.
- [V] No post-M89 JS syntax or required CSS/DOM/WebRTC dependency found; row 25 is a newer ignored optimization hint.
- [V] Feature-checked jitterBufferTarget/playoutDelayHint, codec preference/capabilities, rVFC, visualViewport and modifier-state paths are not compatibility findings.
- [V] Installed Chromium 89 ran the viewer, all channels, rVFC marker measurement and legacy track jitter-buffer stats fallback.
- [I] Headless desktop does not verify Android focus/activation, OSK reopening or actual soft-keyboard composition event sequences.

## Validation

| Check | Result |
|---|---|
| Syntax | [V] node --check passed for all 12 requested JavaScript files. |
| Modern harness | [V] node tools/latency/run.mjs --secs 10 --port 8200; Edge 156.0.4314.42, H.264, 1920×1080@30. |
| Modern result | [V] 237/237 markers decoded; 63 skipped; capture→display P50/P99 105.0/124.0ms; i2p P50/P99 128.2/188.6ms, n=103. |
| M89 harness | [V] Same command with --viewer-browser C:/src/tmp/chrome89/chrome-win/chrome.exe --old-headless; preinstalled Chromium 89.0.4389.0. |
| M89 result | [V] VP8, 266/266 markers decoded; 34 skipped; capture→display P50/P99 103.3/121.3ms; i2p P50/P99 121.0/166.7ms, n=103. |
| Limits | [V] M89 advertised no H.264 capabilities; M89 H.264 remains unverified. Both i2p results retain row 16's sampling defect. |
| Fixtures | [V] Read-only Node VM/raster fixtures verified slots, capture loss, chord encoding, stale IME seq, paste loss, marker geometry/bounds, missing i2p, reordered seq, endpoint replacement and stale-sid forwarding. |
| Temp files | [V] Profiles/CSV/JSON only under C:/src/tmp/pt-review; no downloads. |

[V] CSV/JSON stems: latency-2026-10-25T15-09-25-561Z (Edge), latency-2026-10-25T15-12-04-253Z (M89).
[V] Only this report was written in the repo; existing design/00-brief.md and viewer/viewer.js modifications were left intact.
[V] No prior-art code opened or ported. Only research/a-prior-art.md headings were inspected; no sections were needed for this spec-based review.

## Primary references

- [V] [Chromium Chrome 99 release announcement](https://blog.chromium.org/2022/02/chrome-99-css-cascade-layers-new-picker.html): willReadFrequently in New Canvas 2D API, row 25.
- [V] [W3C Pointer Events Level 2: chorded buttons](https://www.w3.org/TR/pointerevents2/#chorded-button-interactions): extra button transitions can arrive as pointermove, row 10.
- [V] [W3C UI Events](https://w3c.github.io/uievents/#event-type-beforeinput): input/beforeinput semantics; Android delivery still needs device testing, rows 18/20.

