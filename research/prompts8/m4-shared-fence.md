# Task m4-shared-fence (timeout 45 min): hand the capture texture + a shared D3D11 fence to the endpoint at submit time (investigation + probe + design)
Task dir C:\src\tmp\m4\shared-fence\. Report: research/m4/shared-fence.md (create early, fill incrementally).
Another task (m4-copy-delivery) is editing chromium viz and launching the chain right now: do NOT edit C:\src\chromium\src or remotekit/endpoint, do NOT
build in out\dev or launch chrome/endpoint. You MAY build a small standalone probe (cl.exe/clang-cl from the VS 2026 toolchain, files in your task dir).
Idea (from the user): instead of waiting for Chromium's CPU-side CopyOutput completion callback (~12.8 ms waits-for-present, research/m3/trace.md t6) and the
Mojo hop, viz delivers the frame (texture + shared fence + value) right after it SUBMITS the copy; the endpoint does a GPU-side wait
(ID3D11DeviceContext4::Wait(fence, value)) on the device it gave to oneVPL, then queues VPP and ENCODE immediately.
1. Probe (standalone exe, two D3D11 devices on the same adapter, ideally two processes): ID3D11Device5::CreateFence(D3D11_FENCE_FLAG_SHARED) ->
   CreateSharedHandle -> OpenSharedFence on the other device; device A copies into a shared (NT handle) texture and Signals; device B Waits on the GPU, then
   runs a VideoProcessorBlt BGRA->NV12 from that texture and reads back a checksum. Verify ordering (content of frame N, never N-1, under load: 500 frames,
   with device A deliberately slow, e.g. a large draw before the copy). Report D3D11_FEATURE_D3D11_OPTIONS5 / fence support, driver version, timings
   (Signal -> Wait release latency on the GPU), and whether the video engine (VPP and, if quick, a oneVPL ENCODE on the same device; vendored dispatcher
   at endpoint/third_party/libvpl) is correctly ordered after the 3D copy by the context Wait.
2. Chromium side (read at the pin, cite file:line): gfx::D3DSharedFence (ui/gfx/win/d3d_shared_fence.*) and its use in D3DImageBacking / shared images /
   DXGI GMB handles; whether gfx::GpuMemoryBufferHandle / DXGIHandle / media::mojom VideoFrameInfo or the FrameSinkVideoCapturer buffer path can carry a fence
   handle + value; where in viz (SkiaOutputSurfaceImplOnGpu copy-output path for the capturer's GMB/shared-image destination, Graphite on ANGLE D3D11) a
   fence could be signalled right after the copy is submitted and the frame delivered at submit time; how the keyed mutex is used today (capturer pool
   textures, research/b2-capture.md, endpoint d3d_native_frame.*) and what replaces it (fence for "copy done", plus a release fence / Done() for buffer
   reuse so viz never overwrites a texture the endpoint's VPP has not read yet).
3. Design: changed files and approximate patch size, intrusiveness (upstream files touched), expected ms saved vs. the current path (12.8 ms
   wait + Mojo/CPU hop ~0.8 ms), risks (ANGLE device vs. Graphite submission order, fence value monotonicity across GPU process restarts, buffer reuse),
   and how it combines or competes with the copy-delivery fix (m4-copy-delivery: deliver at GPU completion by polling). Recommend go/no-go and a prototype plan.
