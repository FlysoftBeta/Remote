# Task m3-capture-phase (timeout 45 min): why the viz capture copy takes 10-15 ms, and phase alignment (measured)
Task dir C:\src\tmp\m3\capture-phase\. You are the ONLY builder/launcher now. You MAY edit C:\src\chromium\src (branch prts working tree; no commits,
no rebase/reset) for instrumentation or small experiments behind switches, and remotekit endpoint/ (no commits). Incremental builds.
Background: research/m3/levers.md (analysis: the copy request is serviced in the same draw; 12.7 ms = rest of the draw + GPU execution + blit +
fence callback; unverified), research/m3/levers-measurements.md and research/m3/matrix2.md (Viz copy 10-16 ms P50 at 60 fps), patch 17 precise fake
vsync, design/03 + research/b1-frame-path.md / b2-capture.md / b3-pacing.md (capture options, external BeginFrame S5, output-device tap S12).
1. Attribute the 10-15 ms with a real trace: CDP Tracing on the remote browser (categories viz, gpu, gpu.service, cc, media, disabled-by-default-viz.*)
   during a m3-combined loopback run: BeginFrame -> Draw -> CopyOutputRequest issue -> GPU task execution -> blit done -> fence/callback ->
   capturer OnFrameCaptured -> Mojo -> endpoint receive. Give a timeline for 5 representative frames and P50 per sub-step.
2. Experiments (each a 60 s `node tools/latency/run.mjs --preset m3-combined` loopback run, 2 runs): (a) fake vsync 120 Hz with capture at 60 fps;
   (b) capturer min period / oracle settings that request every frame; (c) any cheap fix the trace points at (e.g. GPU scheduling priority, avoiding
   a scaler/format pass, NV12 output from the capturer).
3. Design note (no implementation): swap/present-time tap (S12 output-device tap of the just-drawn back buffer) and endpoint-driven BeginFrames (S5):
   expected ms saved from the measured trace, changed files, risks. Recommend one.
NOTES.md: trace timeline + sub-step table, experiment results (P50/P80/P95/P99 capture->display and input->flip + Viz copy stage), recommendation.
