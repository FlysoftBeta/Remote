# Task m3-levers-integrate (timeout 45 min): integrate GPU-crash recovery + 60 fps vsync timer + pacer trial, verify, measure
Task dir C:\src\tmp\m3\levers-integrate\. You are the ONLY builder/launcher now. You MAY edit C:\src\chromium\src working tree (branch prts; do NOT commit,
rebase, reset or stash there) and remotekit endpoint/ (do not commit). Incremental builds (`be.sh chrome`, `be.sh`).
Drafts (reviewed, unverified): C:\src\tmp\m3\gpu-recover\{chromium.diff,remotekit.diff,NOTES.md} and C:\src\tmp\m3\levers\{chromium.diff,remotekit.diff,NOTES.md}
(the levers NOTES has the analysis and test plan T0-T4). The oneVPL encoder landed in endpoint/ just before you (C:\src\tmp\m3\vpl-integrate\NOTES.md);
merge with `git apply -3`, keep everything.
1. GPU recovery: during a running chain kill the GPU process (taskkill the chrome.exe with --type=gpu-process); capture must resume within ~2 s with a
   keyframe (viewer keeps decoding; report the gap). Run once with default Graphite and once with --cr-disable-skia-graphite.
2. 60 fps: with --cr-fake-vsync-rate=60 (+ the endpoint 60 fps switch from research/m2/latency.md) does the delivered rate reach ~60? CPU/GPU cost.
3. Pacer field trial (--webrtc-field-trials=... from the levers NOTES) A/B and the endpoint high-resolution timer A/B.
Measure each config: `node tools/latency/run.mjs --no-mock --secs 60` (2 runs each, loopback, headless viewer, encoder as the endpoint default (VPL);
also one run with the scrolling scenario page if the netem-matrix task added it under tools/latency/). Table: capture->display and input->flip
P50/P80/P95/P99 + endpoint stage stats + fps, with the pipeline configuration block. Recommend defaults; set them in endpoint/main.cc if the data is clear.
NOTES.md: merge decisions, results, recommended defaults, remaining risks.
