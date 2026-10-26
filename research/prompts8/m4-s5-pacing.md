# Task m4-s5-pacing (timeout 160 min): endpoint-driven BeginFrames (S5, patch 10) + input-triggered frames; final M4 matrix
Task dir C:\src\tmp\m4\s5\. Results file: research/m4/s5-pacing.md (create early; append each table as soon as it is measured).
You are the ONLY process that builds/launches the chain now. You MAY edit C:\src\chromium\src (branch prts working tree; no commits/rebase/reset;
the shared-fence patch 19 is committed, keep it) and remotekit endpoint/, tools/latency/ (no commits).
Why (research/m4/experiments.md "Correlated trace stages", d25j5-c12 S, fence capture, 8 Mbps): input->decoded P80 134 ms = uplink 28 + Mojo->keydown 4 +
keydown->rAF 5-11 + **page draw -> capture_begin 17-32 ms (1-2 whole frames; ideal ~1.6 ms)** + capture/encode ~17 + downlink last-packet dwell 38-43 +
jitter/decode 7. Target input->decoded P80 < 100 ms. The capture phase is the largest removable term, then the keydown->rAF wait.
Design to implement: research/m4/capture-phase.md section "S5 design fallback: one admitted external BeginFrame per root" (read it first, and its
"Timing investigation"/"Verification" findings: HeadlessSchedulerStateMachine, NoCompositorFrameAcks etc.), design/02 patch 10, research/b3-pacing.md.
1. Implement S5 behind `--prts-external-begin-frame` (browser switch, passed by the endpoint) under ui::IsPrtsRemoteMode(): the browser root uses an
   external BeginFrame source driven by the endpoint's high-resolution 60 Hz timer (fixed mode), at most one outstanding BeginFrame per root, never
   catch up; the capture must take the frame produced by THAT BeginFrame (same-frame capture; capture request issued so the copy is of the frame drawn
   for the admitted tick). Then demand mode: on an input event the endpoint issues a BeginFrame immediately (if none outstanding and the last one was
   >= ~4 ms ago) and shifts the fixed timer phase, so keydown->rAF wait shrinks. Keep rAF/animations/video correct (no freezes; pages see ~60 Hz).
2. Make 8 Mbps the endpoint default ceiling (`--max-kbps` default 8000, start 8000; experiments.md E5: removes c12 queue drops, S P99 123->88 ms).
3. Verify: 5 loopback marker runs (phase draw->capture_begin P50 must be < 5 ms in all 5, no negative-phase associations beyond the known limitation),
   S5 off vs fixed vs fixed+demand. Then the FINAL M4 matrix with the best mode: d25j5-c12 S/V/G x3 and d25j5-c12-wifi S/V/G x3 (60 s each), plus
   1 headed Chrome 89 run on d25j5-c12 S (`--viewer-exe`, see research/m4/chrome89.md). Table per row: median P80/P99 of capture->decoded, input->decoded
   (primary acceptance) and the glass estimates, pass/fail vs P80<100/P99<200, per-stage trace for one S run (experiments.md format).
4. If S5 does not stabilise the phase within ~90 min, stop implementing, run the final matrix with the current best configuration (fence + 8 Mbps), and
   document exactly what blocks S5. Either way save chromium.diff / endpoint.diff in the task dir and leave the best configuration applied as the default.
