# Task m4-copy-delivery (timeout 75 min): deliver the viz capture copy at GPU completion, not after present; then fix the capture phase
Results file: research/m4/copy-delivery.md (measurement tables + root cause + recommendation; create early, fill incrementally).
Task dir C:\src\tmp\m4\copy-delivery\. You are the ONLY process that builds/launches the chain right now (other tasks only write drafts).
You MAY edit C:\src\chromium\src (branch prts working tree; no commits/rebase/reset). Do NOT edit remotekit/endpoint (another task drafts there).
Measured fact (research/m3/trace.md t6, research/m3/stage-table.md rows 7-9): page rAF -> viz DrawAndSwap +2.25 ms, GPU CopyOutput done +3.45 ms,
but the capturer's capture_end (OnFrameCaptured) is at +16.4 ms; the CopyOutputRequest async trace event ends at ~16.8 = Display::FrameDisplayed.
So ~12.8 ms of every captured frame is a wait for the NEXT present/swap, not work. Renderer = Skia Graphite on ANGLE D3D11 (GPU process, UHD 630).
1. Find (read code at the pin, cite file:line) exactly what delays the result callback: e.g. Graphite finished-proc only checked on the next
   submit/swap (`SkiaOutputSurfaceImplOnGpu::CheckAsyncWorkCompletion`, readback-completion polling timer, `gpu::SharedContextState`
   finished procs, result posted on DidSwapBuffersComplete?), and why any <=2 ms polling does not apply to the GMB/shared-image D3D11 copy path.
2. Implement the smallest PRTS-guarded fix so the copy result is delivered as soon as the GPU finishes the blit (e.g. submit right after the
   CopyOutput and poll completion at <=1 ms, or signal via a D3D11 query/fence). Keep keyed-mutex semantics correct (the endpoint must never read a
   texture before the blit is complete).
3. FIRST (before you rebuild) measure the current binary: `--preset m3-combined --scenario marker` loopback 60 s, 3 runs. Then build chrome
   (incremental, `be.sh chrome`), smoke it, and measure 3 runs after; plus 1 run each of S and G on `--netem d25j5-c12` after. Report
   P50/P80/P95/P99 capture->display and input->flip and the Viz copy stage (PRTS_TRACE=1: capture_begin->capture_end) for every run, with report paths.
4. Record per run the capture phase (page draw -> capture_begin, research/m3/trace.md method) so phase effects are separated from the fix.
5. Capture phase (second lever, after 1-4 are done and measured): page draw -> capture_begin locks per run at 1.6 / 17 / 34 ms (1-3 vsyncs). Find why
   (oracle/min-capture-period vs BeginFrame timing, capturer pipeline depth / in-flight limit, refresh vs damage events) and make the capture always
   take the frame drawn in the same BeginFrame (the 1.6 ms case), PRTS-guarded. Verify over 5 loopback marker runs (phase must be ~1.6 ms in all 5)
   and 2 runs of S on d25j5-c12. If it needs endpoint-driven BeginFrames (design/02 patch 10, S5), only write the design in the results file.
NOTES.md: root cause with citations, the diff (also save `git -C C:\src\chromium\src diff > C:\src\tmp\m4\copy-delivery\chromium.diff`), measurement
table, risks. Leave the fix applied in the working tree.
