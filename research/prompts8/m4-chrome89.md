# Task m4-chrome89 (timeout 45 min): Chrome 89 acceptance as the viewer (H.264 decode, playout-delay (0,0), latency on d25j5)
Task dir C:\src\tmp\m4\chrome89\. You are the ONLY process that launches the chain now. Output report: research/m4/chrome89.md (create early, fill
incrementally) + NOTES.md in the task dir.
Viewer browser: genuine Google Chrome 89.0.4389.90 at C:\src\tmp\chrome89-google\app\chrome.exe (Authenticode valid; read C:\src\tmp\chrome89-google\NOTES.md).
Flags: --user-data-dir=C:\src\tmp\chrome89-google\profile --no-first-run --no-default-browser-check --disable-component-update
--disable-background-networking --disable-sync (+ --remote-debugging-port=<p> for measurement). Run it HEADED (a visible window) unless that fails; record which.
1. Add a minimal `--viewer-exe <path>` (and `--viewer-flags "<...>"` if needed) option to tools/latency/run.mjs so the harness can drive Chrome 89 instead
   of Edge (keep defaults unchanged). Check the CDP methods and page APIs used by run.mjs/measure.js/viewer exist in Chrome 89 (requestVideoFrameCallback is
   Chrome 83+; check getStats fields such as jitterBufferDelay, decoderImplementation, framesDecoded; avoid syntax newer than Chrome 89 in viewer/ code
   the page loads: fix any such syntax minimally in viewer/ and list it).
2. Verify and record with evidence: (a) H.264 decode of the live 1080p (or current viewport) stream: codec mimeType/sdpFmtpLine from getStats, frame size,
   framesDecoded rising, decoderImplementation (hardware vs software if readable), SPS level actually sent; (b) playout-delay: the extension is negotiated
   (answer SDP) and jitterBufferDelay/jitterBufferEmittedCount per frame is ~0 (research/s8-chrome89.md predicted the receiver treats it as a lower bound only
   when set from JS); compare once with `--cr-...`/endpoint unchanged vs viewer JS playoutDelayHint unset.
3. Latency (each 60 s, `--preset m3-combined`, current out\dev binaries): loopback marker x2, then `--netem d25j5-c12` scenarios S, V, G x2 each (if time
   is short: 1 each, S first). Same table format as research/m3/matrix2.md: P50/P80/P95/P99 capture->display and input->flip, fps, the backend actually used.
   Then 1 run of Edge (default viewer) on d25j5-c12 S for an A/B reference in the same session.
4. Note anything Chrome 89 does differently (freezes, NACK/PLI storms, decode errors, resolution changes).
