# PT-FIX-B change log

2026-10-25. Input / IME / marker correctness.
[V] = verified by source inspection or execution; [I] = inference / unverified device behavior.

## Scope and contracts
| Item | Result |
| --- | --- |
| Authorized source scope | [V] Edited viewer/input.js, viewer/ime.js, viewer/protocol.js, viewer/index.html, tools/latency/marker.js, tools/latency/measure.js only. |
| Clock | [V] tools/latency/clock.js inspected and syntax checked; unchanged. |
| Protocol | [V] Read design/06-protocol.md, including sections 2.2, 2.4, 3.2, 3.3, 4.2 and 6. |
| Findings | [V] Read research/pt-review.md; addressed requested input/IME portions and rows 7, 9, 10, 14-19, 24, 25. |
| Session integration | [V] Concurrent viewer.js edits now call input.clearSession() and ime.clearSession(); this task did not edit viewer.js. |
| Workspace | [V] Concurrent changes in viewer.js, control-plane/* and mock-endpoint/mock.js left untouched by this task. |
| Files / downloads | [V] All task fixtures and runtime artifacts are under C:/src/tmp/ptrack; no downloads and no git commit. |

## Changes
| Finding / area | Implementation and reason |
| --- | --- |
| 6 input cleanup | [V] input.clearSession drops pending fields, frees all slots, forgets button/capture/position state and releases native captures without sending. |
| 6 IME cleanup | [V] ime.clearSession blurs editors, clears sentinel values, sets lastSeq=-1 and state.focused=false, cancels fallback timer and hides keyboard button. |
| 6 forwarding | [V] Both editor handlers and input.sendKey/synthMove are gated on input.enabled; inactive editor handlers cannot forward. |
| 7 capture loss | [V] Track captured DOM pointer IDs; mouse loss while pressed sends RESET, active touch/pen loss sends its CANCEL. |
| 7 normal release | [V] Remove capture tracking before normal up/cancel, so implicit lostpointercapture sends no extra cleanup. |
| 9 slots | [V] Mouse is pid 0; touch/pen allocate smallest free slot 1..15; sixteenth contact is ignored. |
| 10 chords | [V] Track each pointer's prior buttons mask; move emits immediate DOWN/UP per changed bit using DOM indices 0,2,1,3,4, then moves. |
| 17 wire order | [V] Pending moves store pointer fields; assign seq/timestamp and encode only when transmitting. |
| 17 ordering | [V] All pending moves flush before discrete pointer events, wheel, KEY and RESET. |
| 14 geometry | [V] cellFor is max(4, round(frameWidth/160)); draw uses it directly. |
| 14 scaling | [V] measure.senderWidth is settable; unset defaults to current videoWidth; sample cell scales by videoWidth/senderWidth. |
| 15 invalid buffers | [V] Decoder rejects invalid stride/cell, insufficient extents, out-of-buffer sample windows and nonfinite sync/data luma. |
| 15 downscaling | [V] Sample radius can reach zero for small received cells; centered downscaled cells remain decodable. |
| 16 acknowledgment | [V] Each unique decoded frame samples/removes every pending seq acknowledged by the 16-bit half-range comparison. |
| 16 bounded lifetime | [V] Pending inputs expire after 5000ms on input submission or frame callbacks, including callbacks without usable video/clock. |
| 16 reset | [V] reset clears pending inputs, lastInputSeq and lastFrameId as well as counters and samples. |
| 18 paste/drop | [V] Commit event data or dataTransfer.getData('text'); absent beforeinput payload falls through to input reconciliation. |
| 18 deletion | [V] Word/soft-line backward deletion sends ime.key Backspace with mods:2. |
| 18 fallback | [V] Noncomposition input differences are committed after stripping one leading sentinel space. |
| 18 deduplication | [V] Track immediately forwarded edits to suppress trailing input/composition duplicate commits. |
| 19 password | [V] Separate hidden input type=password shares handlers; ime.state swaps editor and input.keyTarget. |
| 19 flags | [V] autocomplete follows flag bit 1 (off when set, on otherwise); existing correction/capitalization/spellcheck mappings retained. |
| Keyboard fallback | [V] Pointerdown blurs/refocuses the active editor inside the gesture, suppressing an artificial ime.hidden. |
| Composition lifecycle | [V] Caret-only touch state updates preserve local composition; deliberate hide/swap suppresses blur-triggered forwarding. |
| 24 decoding | [V] Reject raw pid>15 and KEY codeLen/keyLen>32 before dispatch. |
| 25 compatibility | [V] Retained willReadFrequently and documented its ignored-hint fallback on Chrome 89. |

## Synthetic verification
| Check | Result |
| --- | --- |
| Syntax | [V] node --check passed for input.js, ime.js, protocol.js, marker.js, measure.js and clock.js. |
| Whitespace | [V] git diff --check passed for all seven permitted paths. |
| Fixture | [V] node C:/src/tmp/ptrack/fix-b-test.cjs; all assertions passed. |
| Marker widths | [V] Fake RGBA raster round-trips passed at 640, 1280, 1366 and 1920 sender pixels. |
| Marker scale | [V] Each width tested at native, half and approximately 1.25x video width (12 successful combinations). |
| Marker malformed | [V] Empty/short buffers, invalid geometry, inadequate width and NaN data-cell luma return null. |
| Protocol malformed | [V] Raw pid 16 and KEY codeLen/keyLen 33 rejected. |
| Input fixtures | [V] 15 touch/pen slots plus mouse, slot reuse, all five chord bits, normal/unexpected capture loss, queued KEY order and clearSession passed. |
| IME fixtures | [V] Disabled handlers, password swap, autocomplete, paste/drop deduplication, input-only fallback, Ctrl+Backspace and composition deduplication passed. |
| IME lifecycle | [V] seq 10 then clearSession then seq 1 accepted; keyboard hidden and editor emptied without ctl output. |
| IME preservation | [V] Touch caret update preserves composing value; cleanup-induced compositionend produces no commit. |
| i2p wrap | [V] Pending 65534,65535,0,1,2 with ack 1 samples first four and retains 2; next ack 2 samples it. |
| i2p lifecycle | [V] Duplicate frame does not resample; entries older than 5s are removed; reset clears pending/dedupe state. |

## Browser harness
[V] Command: node tools/latency/run.mjs --secs 10 --port 8230.
[V] First run: Edge 156, H.264 1920x1080@30; 201/201 markers decoded, zero failures.
[V] First capture->display P50/P80/P95/P99: 89.4/97.1/106.4/125.1ms (n=201).
[V] First input->photon P50/P80/P99: 115.2/129.0/165.4ms (n=102).
[V] First artifact: C:/src/tmp/ptrack/latency-2026-10-25T15-45-12-778Z.csv and matching JSON.
[V] Final rerun after composition-preservation fix: 214/214 decoded, zero failures; capture->display P50/P80/P95/P99 85.5/93.8/103.3/110.0ms (n=214).
[V] Final input->photon P50/P80/P99: 107.9/129.8/171.1ms (n=102); min RTT 0.60ms.
[V] Final artifact: C:/src/tmp/ptrack/latency-2026-10-25T15-46-47-720Z.csv and matching JSON.

## Prior art and limits
| Item | Assessment |
| --- | --- |
| Prior-art use | [V] Summary headings only skimmed; no prior-art code opened, copied or ported. No task named a required prior-art section. |
| Evaluation basis | [V] Existing local protocol/design and explicit user decisions supply the contracts; no prior-art behavior was adopted as authority. |
| Compatibility | [V] Source uses ES2017-compatible syntax and Chrome 89-era APIs; newer willReadFrequently hint is optional. |
| Device limit | [I] Actual Android keyboard reopening and native IME event timing still require device verification; synthetic events and desktop headless do not establish those behaviors. |
| Half-range limit | [I] Wrap ordering assumes fewer than 32768 outstanding sequence positions; the requested five-second expiry bounds normal usage. |
