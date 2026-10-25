# Task PT-FIX-B: input / IME / marker correctness (viewer input layer + latency tools)
Files you may edit (ONLY these): viewer/input.js, viewer/ime.js, viewer/protocol.js, viewer/index.html, tools/latency/marker.js,
tools/latency/measure.js, tools/latency/clock.js. Another Codex task edits viewer/viewer.js, control-plane/*, tools/mock-endpoint/mock.js
at the same time: do not touch those. Hard limit 30 minutes. Write a short change log to `C:\src\tmp\ptrack\fix-b.md`.
Spec: design/06-protocol.md (sections 2.2, 2.4, 3.2, 3.3, 4.2, 6 updated today). Findings to fix: research/pt-review.md rows 6 (input/IME
part), 7, 9, 10, 14, 15, 16, 17, 18, 19, 24, 25 (read them). Decisions (follow exactly):
- Add `RKInput.prototype`-style methods on the instance: `input.clearSession()` (drop pending moves, free all slots, forget captures; no
  message sent) and keep `input.reset()` (sends RESET). Add `ime.clearSession()` (blur, clear sentinel, lastSeq = -1, state.focused=false,
  hide the keyboard button). viewer.js (other task) calls both on session change.
- Gate the textarea key/IME forwarding on `input.enabled` too.
- Pointer slots: mouse = pid 0; touch/pen allocate the smallest free of 1..15.
- Buttons: track the last `buttons` mask per pointer; on pointermove whose mask changed, emit one DOWN/UP per changed bit (button = DOM
  button index for that bit: 1→0, 2→2, 4→1, 8→3, 16→4) immediately, then the move. Unexpected `lostpointercapture` while buttons are down
  (mouse) or while the touch is active → send `input.reset()` semantics for mouse (RESET) and CANCEL for that touch pointer.
- seq assigned at actual transmission (pending moves store the fields, encode when sent); flush pending before any discrete message.
- protocol.decodeInput: reject pid > 15, codeLen/keyLen > 32; `encodeKey` already truncates.
- Marker: one shared geometry function `cellFor(frameWidth) = Math.max(4, Math.round(frameWidth/160))` used by draw; measure uses
  `cell = cellFor(senderWidth) * videoWidth / senderWidth` where `senderWidth` is settable (`measure.senderWidth = …`, default = videoWidth);
  decode validates bounds (samples inside the buffer, finite luma) and returns null otherwise. Test widths 640, 1280, 1366, 1920 with a
  small Node script using a fake RGBA buffer (do not add it to the repo; put it under C:\src\tmp\ptrack\).
- i2p: when a frame acknowledges inputSeq s, record samples for every pending seq ≤ s (wrap-aware, 16-bit) and drop them; drop pending
  entries older than 5 s; `reset()` clears pending.
- IME: password fields use a separate hidden `<input type="password">` element sharing the same handlers (swap the active element on
  ime.state); autocomplete from flags (bit 1 → off, else on); handle `insertFromPaste`/`insertFromDrop` as commit of the pasted text
  (`e.data` or `e.dataTransfer.getData('text')`); `deleteWordBackward`/`deleteSoftLineBackward` → `ime.key` with `Ctrl+Backspace`-equivalent:
  send `{t:'ime.key', key:'Backspace', mods: 2}`; any `input` event whose value differs from the sentinel without having been forwarded →
  commit the difference (strip the leading sentinel space). Keyboard button: in the pointerdown gesture do `ta.blur(); ta.focus()`.
- Leave `willReadFrequently` (ignored on 89), add a comment.
Verify: `node --check` each file; your marker Node test; `node tools/latency/run.mjs --secs 10 --port 8230` must print percentiles incl.
input->photon. Keep ES2017, Chrome 89 compatible, no dependencies. Do not git commit.
