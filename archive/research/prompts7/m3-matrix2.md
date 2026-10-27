# Task m3-matrix2 (timeout 45 min): d25j5 acceptance matrix with the M3 pipeline (VPL + 60 fps precise vsync) vs the OpenH264 baseline
Task dir C:\src\tmp\m3\matrix2\. You are the ONLY builder/launcher now. You MAY edit tools/latency/, tools/netem/ and endpoint/main.cc defaults
(remotekit; no commits). FIRST run `bash /c/src/tmp/m1/be.sh chrome` (the vsync clock header just moved to ui/gl/prts_fake_vsync_clock.h; fix trivial
build errors in ui/gl or chrome/browser/prts only) and `bash /c/src/tmp/m1/be.sh`.
Background: research/m3/netem-matrix-openh264.md (baseline matrix, how to run S/V/G scenarios with --netem), research/m3/levers-measurements.md
("combined" config = VPL @60 fps + --cr-fake-vsync-rate=60 precise vsync + hi-res timer: loopback 44/49/52/53 ms), C:\src\tmp\m3\vpl-fix\NOTES.md
(VPL rate-change/IDR fix that just landed; use its recommended settings).
1. Make the "combined" config reproducible from the CLI (one endpoint switch or run.mjs preset); if VPL @60 is "startup-sensitive" (levers NOTES), find why
   and fix or document a workaround.
2. Matrix, headless Edge viewer, 60 s each: d25j5-c16, d25j5-c12, d25j5-c10, d25j5-c12-wifi x scenarios S, V, G with the combined config (2 runs for c12
   rows, 1 otherwise); then d50j10-c12 S once (stress); then one HEADED viewer run of d25j5-c12 S.
3. Table like the baseline (capture->display and input->flip P50/P80/P95/P99, send Mbps, relay drops/max queue, freezes, decoded fps, PASS/FAIL vs
   P80<100 and P99<200 for each metric) + the per-stage breakdown (viz, VPP, encode, pacer/network, viewer jitter, display) + the pipeline block,
   and a before/after comparison against the OpenH264 baseline rows.
NOTES.md: results, what still fails and why (data), recommended next levers.
