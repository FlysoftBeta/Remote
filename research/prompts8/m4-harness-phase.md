# Task m4-harness-phase (timeout 75 min): decoded metric in the harness + capture-phase alignment
Task dir C:\src\tmp\m4\harness-phase\. Results file: research/m4/capture-phase.md (create early, fill incrementally).
You are the ONLY process that builds/launches the chain now. You MAY edit tools/latency/ and C:\src\chromium\src (branch prts working tree; no
commits/rebase/reset). Do NOT edit remotekit/endpoint (another task drafts there). Do NOT change how the capture copy result is delivered (the copy
delivery path is being redesigned around a shared GPU fence by another task; stay out of SkiaOutputSurfaceImplOnGpu/CopyOutput delivery code).
1. Harness (small; a killed earlier task already started this: tools/latency/measure.js and run.mjs have uncommitted partial edits, review and finish them): tools/latency/measure.js marker and input->flip detection use meta.expectedDisplayTime; add capture->decoded and input->decoded using
   meta.presentationTime of the same frame (same percentiles, new report fields, a printed summary line); keep the old fields unchanged. Verify on one
   20 s loopback run.
2. Baseline: `--preset m3-combined --scenario marker` loopback 60 s x3 with the current binaries; record decoded + display metrics and, with PRTS_TRACE=1,
   the capture phase per run (page draw -> capture_begin, research/m3/trace.md method; M3 saw it lock per run at 1.6 / 17 / 34 ms = 1-3 vsyncs).
3. Find why the phase varies (cite file:line at the pin: FrameSinkVideoCapturerImpl oracle / min capture period vs BeginFrame timing, in-flight frame
   limit / pool depth, refresh vs damage events, the 60 Hz fake vsync from patch 17 vs the capturer's own timing) and make the capture always take the
   frame drawn in the same BeginFrame (the 1.6 ms case), PRTS-guarded, minimal. Build chrome (`be.sh chrome`).
4. Verify: 5 loopback marker runs (phase must be ~1.6 ms in all 5) and 2 runs of S on `--netem d25j5-c12`. Table: P50/P80/P95/P99 capture->decoded,
   input->decoded, capture->display, input->flip, phase. If alignment needs endpoint-driven BeginFrames (design/02 patch 10, S5), write that design
   instead of implementing it. Save `git -C C:\src\chromium\src diff > C:\src\tmp\m4\harness-phase\chromium.diff`; leave the change applied.
