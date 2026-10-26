# Task m4-experiments (timeout 150 min): integrate the endpoint knobs, then run the design/03 section 6 experiments on d25j5 (3 repeats each)
Task dir C:\src\tmp\m4\experiments\. Results file: research/m4/experiments.md (create early; append each configuration's table as soon as it is measured).
You are the ONLY process that builds/launches the chain now. You MAY edit remotekit endpoint/ and tools/latency/ (no commits).
Inputs: C:\src\tmp\m4\endpoint-knobs\endpoint.diff + NOTES.md (switch draft: VPL readback tolerance, --vpl-*, --min/start/max-kbps, --playout-delay, --fec),
research/m4/copy-delivery.md + C:\src\tmp\m4\copy-delivery\NOTES.md (chromium fix left applied in C:\src\chromium\src; if its results say it made things
worse or it is broken, `git -C C:\src\chromium\src stash` it, rebuild chrome, and say so).
1. Review and apply endpoint.diff (`git apply`), fix compile errors, `be.sh` (endpoint), smoke one 20 s loopback run: backend must stay VPL (no OpenH264
   fallback in the log), every switch must appear in the report `pipeline` block.
2. Experiments. Each configuration = `--preset m3-combined --netem d25j5-c12`, scenarios S and G, 3 runs x 60 s each (6 runs/config); report per run
   P50/P80/P95/P99 capture->display and input->flip, fps, encoded frame size P50/P99, VPL cap violations, queue drops (netem stats), plus the median of
   the 3 runs per scenario. Order (biggest expected lever first; drop the tail if time runs out, but always finish a configuration you started):
   B   baseline (current defaults; after copy-delivery)
   E5  --max-kbps=10000, then --max-kbps=8000 (start-kbps <= max)  [link is 12 Mbps: a max equal to the link rate fills the bottleneck queue]
   E2  best E5 max + --vpl-max-frame-factor=1.0 ; then 2.0
   E3  best so far + VBV 2 frames vs 4 frames (--vpl-vbv-frames)
   E1  best so far at --fps 30
   E6  best so far + --vpl-intra-refresh=30, measured on d25j5-c10 S (loss bursts) vs the same without it (2 runs each)
   E13 best so far + --fec=ulpfec on d25j5-c10 S (2 runs) [skip if E6 ran out of time]
   W   best configuration on d25j5-c12-wifi, S/V/G, 3 runs each
   Use PRTS_TRACE=1 on one run of B and one run of the best configuration and give the per-stage breakdown (research/m3/stage-table.md format).
3. Conclude: best configuration (exact switches), what each lever did (only differences > ~20 ms or repeated count), whether P80 < 100 / P99 < 200 is met per
   scenario for capture->display and input->flip, and the remaining biggest term. Leave the best configuration as the endpoint defaults ONLY if it is a
   clear win in >= 2 scenarios (otherwise keep the switches and recommend).
