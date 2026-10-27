# Task PT-REVIEW: code review of the viewer / mock endpoint / latency harness / control-plane (report only, no edits)
Deliverable (only this path): `research/pt-review.md`. Hard limit 25 minutes.
Spec: design/06-protocol.md (read fully). Code to review (all of it, ~1500 lines):
viewer/protocol.js, viewer/input.js, viewer/ime.js, viewer/viewer.js, viewer/index.html, tools/mock-endpoint/mock.js,
tools/latency/{marker,clock,measure}.js, tools/latency/run.mjs, control-plane/index.js, control-plane/endpoint.js, control-plane/framing.js.
Check, in this priority order:
1. Chrome 89 compatibility of everything loaded by viewer/index.html: any JS syntax, DOM, CSS or WebRTC API newer than Chrome 89 used without a
   feature check (give the Chrome version that introduced it). Do not flag APIs that are feature-detected.
2. Protocol conformance: binary layouts in protocol.js vs 06 section 2.2 (offsets, sizes, signedness, endianness), ctl/IME/probe message
   names and fields vs sections 3-4, signalling messages and session lifecycle in control-plane/index.js vs section 1.
3. Real bugs: stuck keys/buttons/touches (missing up/cancel/reset paths), pointer slot leaks, races in signalling (offer before welcome, ICE
   before remote description, renegotiation), control-plane session state when either side reconnects or a second viewer joins, marker
   decode robustness (cell sampling, threshold, CRC, unwrap), clock offset sign, percentile math.
4. Soft keyboard flow on Android Chrome 89 (focus without user activation, beforeinput/composition handling, the one-space sentinel).
Output: a table `| # | severity (high/med/low) | file:line | finding | concrete fix |`, at most 25 rows, highest severity first, then a
short list of things verified correct. Mark [V] (verified by reading/running) vs [I]. You may run `node --check` and the harness
(`node tools/latency/run.mjs --secs 10 --port 8200`) but do not modify any file except the report.
