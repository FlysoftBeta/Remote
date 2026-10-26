# Task m4-fence-impl (timeout 120 min): implement the shared-fence early hand-off as THE capture delivery path (or the fallback)
Task dir C:\src\tmp\m4\fence-impl\. Results file: research/m4/fence-impl.md (create early, fill incrementally).
You are the ONLY process that builds/launches the chain now. You MAY edit C:\src\chromium\src (branch prts working tree; no commits/rebase/reset) and
remotekit/endpoint/ (EXCEPT vpl_encoder.* rate-control code and peer.cc, which another task's draft diff C:\src\tmp\m4\endpoint-knobs\endpoint.diff will
change; keep your endpoint edits in d3d_native_frame.*, d3d_frame_source.*, endpoint_link.* and minimal hooks in vpl_encoder.* for the GPU wait).
Inputs: research/m4/shared-fence.md + C:\src\tmp\m4\shared-fence\ (probe results, Chromium code map, design, go/no-go), research/m4/capture-phase.md
(capture-phase change already applied in the chromium working tree: keep it; your diff is on top), research/m3/trace.md t6 (12.8 ms copy waits for present).
Decision rule (from the user): if the probe shows driver support AND the video engine (VPP/encode) is correctly ordered after the 3D copy by the GPU-side
Wait, implement the fence path and NOT the deliver-at-GPU-completion path. Only if the fence is infeasible, implement the fallback instead:
research/prompts8/m4-copy-delivery.md steps 1-3 (deliver the copy result at GPU completion by prompt completion polling).
Fence path:
1. viz (PRTS-guarded): a shared D3D11 fence per capturer (or per GPU device), signalled on the GPU right after the copy into the capturer pool texture
   is submitted; deliver the frame (texture handle as today + fence handle once + fence value per frame) at submit time instead of at the CPU completion
   callback. Carry the fence/value through the existing capture Mojo path with the smallest contract change (chrome/browser/prts/mojom or
   VideoFrameInfo metadata). Buffer reuse: viz must not overwrite a pool texture before the endpoint has consumed it (Done()/release after the
   endpoint's VPP is submitted, plus a GPU-side ordering for reuse if needed). Decide what replaces the keyed mutex and document it.
2. endpoint: open the fence once (OpenSharedFence), per frame ID3D11DeviceContext4::Wait(fence, value) on the oneVPL device, then VPP + ENCODE
   immediately; GPU process restart (research/m3/gpu-recover.md) must reopen the fence and IDR. Keep the keyed-mutex path as the fallback when the fence
   is not available (switch `--capture-sync=fence|mutex`, default fence when supported).
3. Correctness: a frame-number checksum test (S3 style: page draws the frame counter; endpoint or viewer verifies no torn/stale frame over >= 2000 frames,
   including under GPU load (scenario G)). Report it.
4. Measure (each 60 s, `--preset m3-combined`): loopback marker x3 and `--netem d25j5-c12` S and G x2 each, fence vs `--capture-sync=mutex` in the same
   session. Table: P50/P80/P95/P99 capture->decoded, input->decoded, capture->display, input->flip, plus capture stage (capture_begin -> endpoint
   receive -> VPP submit) from PRTS_TRACE=1. Save `git -C C:\src\chromium\src diff > C:\src\tmp\m4\fence-impl\chromium.diff` and
   `git -C C:\Users\Flysoft\Desktop\remotekit diff -- endpoint > C:\src\tmp\m4\fence-impl\endpoint.diff`; leave everything applied.
RESUME NOTE: a previous run of this task worked ~55 min (21:18-22:12) and was cut off by the Codex usage limit. Its partial implementation is
UNCOMMITTED in the working trees (chromium: prts mojom, prts_capture.cc, viz skia_output_surface_impl_on_gpu.*, gmb_video_frame_pool_context_provider*,
prts_copy_output_result.h; endpoint: d3d_frame_source.cc, d3d_native_frame.*, endpoint_link.cc) and described in research/m4/fence-impl.md and
C:\src\tmp\m4\fence-impl\. Continue from that state (review it, finish, build, verify, measure); do not start over.
