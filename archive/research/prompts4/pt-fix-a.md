# Task PT-FIX-A: signalling/session robustness (control-plane, viewer.js, mock.js)
Files you may edit (ONLY these): control-plane/index.js, control-plane/endpoint.js, control-plane/framing.js, viewer/viewer.js,
tools/mock-endpoint/mock.js. Another Codex task edits viewer/input.js, viewer/ime.js, viewer/protocol.js, tools/latency/* at the same time:
do not touch those. Hard limit 30 minutes. Write a short change log to `C:\src\tmp\ptrack\fix-a.md` (what changed per finding, test output).
Spec: design/06-protocol.md (sections 1, 2.4, 3.1, 6 updated today). Findings to fix: research/pt-review.md rows 1, 2, 3, 4, 5, 8, 11, 12, 13,
21, 22, 23 (read the rows). Decisions (follow exactly):
- CP: per-socket `error` handler; all writes to the stdio endpoint wrapped (sync throw + async) with stdin `error` handler; HTTP handler
  wraps path decoding in try/catch → 400. Viewer messages require `m.sid === I.sid` (drop otherwise). On endpoint replacement send
  `bye reason:"endpoint-replaced"` to the viewer BEFORE the new `welcome`.
- Viewer reconnect policy: close codes 4000 (replaced), 4400 (version), 4401 (auth) are terminal: show a status message and a
  "reconnect" click-to-retry (status element click handler); other closes retry with backoff 1, 2, 4 … max 10 s.
- Viewer session generation: every `welcome` with a new sid tears down the old PC/channels/clock and calls `input.clearSession()` and
  `ime.clearSession()` when those functions exist (the other task adds them; guard with `typeof … === 'function'`). All async negotiation
  continuations capture the PC/generation and bail if stale; catch all rejections. Store `welcome.iceServers` and use them for the PC.
- ICE: queue remote candidates until setRemoteDescription resolved (per PC), then drain; pass `candidate:null` as end-of-candidates
  (`pc.addIceCandidate()` with no argument is not in M89 for null semantics: use `addIceCandidate({candidate:''})` inside try/catch, ignore errors).
  Mock: queue its outgoing ICE until the offer has been sent, and set sender parameters BEFORE createOffer where possible.
- Restart: at most one `restart` per session; 10 s after `failed` without recovery → send `bye reason:"ice-failed"`, close PC.
- Handshake: viewer enables input (input.enabled = true) only after it receives the endpoint's ctl `hello` with `v===1 && input===1`;
  mismatch → bye `version`. Mock ignores input/IME until it has received the viewer's ctl hello with matching versions and replies with its
  hello only then. Mock resets session-local state (field focus/composition, inputSeq, pointers, imeSeq, stats baseline) on every
  session.open and bye.
Verify: `node --check` on each file; `node --test control-plane/test/`; `node tools/latency/run.mjs --secs 10 --port 8220` must still print
latency percentiles (Edge); also run it with `--viewer-browser C:/src/tmp/chrome89/chrome-win/chrome.exe --old-headless --port 8221`.
Keep the code style (ES2017, no new dependencies, Chrome 89 compatible in viewer and mock). Do not git commit.
