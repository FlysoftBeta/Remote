# Task m4-copy-delivery (timeout 45 min): deliver the viz capture copy at GPU completion, not after present
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
NOTES.md: root cause with citations, the diff (also save `git -C C:\src\chromium\src diff > C:\src\tmp\m4\copy-delivery\chromium.diff`), measurement
table, risks. Leave the fix applied in the working tree.
