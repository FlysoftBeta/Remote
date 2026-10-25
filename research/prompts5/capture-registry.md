# Task capture-registry (timeout 45 min): patches 03 prts-registry, 04 viz CompleteCapture fix, 05 prts-capture (browser side)
Output dir: C:\src\tmp\m1\capture\
Inputs: C:\src\tmp\m1\contract\prts.mojom; if present, C:\src\tmp\m1\s10-link\ (prts_controller.h, prts_delegates.h: implement those
delegate interfaces exactly; if absent, define the interfaces yourself in prts_delegates.h and say so). research/b2-capture.md,
design/02 sections 2.2, 03 sections 2-3.

Deliverables (Chromium C++, compile-checked by reading headers at the pin; cite file:line in NOTES):
1. chrome/browser/prts/prts_window_registry.{h,cc}: tracks browser windows only (BrowserList observer; for each Browser get its
   BrowserView widget -> aura::WindowTreeHost via widget->GetNativeWindow()->GetHost()); assigns window ids; observes host bounds/DSF
   changes (aura::WindowTreeHostObserver / WindowObserver) and title; calls the client WindowAdded/Changed/Removed/Activated. Provides
   lookup id -> WindowTreeHost* for input/capture. Activate(id): logically activates that root (widget->Activate()) and Deactivate()s others (D8).
   NewWindow(url): open a browser window/tab via chrome::NewEmptyWindow / NavigateParams on the last-used profile.
2. 04 viz-capturer-complete-capture: a/b diff for components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.cc
   MaybeDeliverFrame failure/version-change paths calling oracle_->CompleteCapture(..., false, ...) (b2 section 3; re-verify the bug at the pin
   first: read the function fully; if not a bug, say so and drop it). Verify with git apply --check. No unittest needed now.
3. chrome/browser/prts/prts_capture.{h,cc}: per window: CreateVideoCapturer via the root compositor's HostFrameSinkManager
   (find the right accessor at the pin: ui::Compositor::context_factory()->GetHostFrameSinkManager() or content::GetHostFrameSinkManager()),
   ChangeTarget(VideoCaptureTarget(compositor->frame_sink_id())), WindowTreeHost::CreateVideoCaptureLock(), SetFormat ARGB,
   SetMinCapturePeriod(1/fps), SetMinSizeChangePeriod(0), SetAutoThrottlingEnabled(false), animation lock-in off,
   SetResolutionConstraints(fixed = root pixel size, re-applied on resize), Start(consumer, kPreferMappableSharedImage).
   Implements viz::mojom::FrameSinkVideoConsumer: OnFrameCaptured -> extract the D3D11 NT handle from the buffer handle the pin delivers
   (read media/capture/mojom/video_capture_buffer.mojom and the consumer mojom at the pin: GpuMemoryBufferHandle / shared image /
   gfx::DXGIHandle; find the stable per-buffer identity e.g. DXGIHandleToken) -> first time: assign buffer_id, DuplicateHandle, sink->NewBuffer;
   then sink->OnFrame(info, reply = callbacks->Done()). Keep the FrameSinkVideoConsumerFrameCallbacks remote alive until the reply.
   Software fallback (shared memory region): log and drop for M1 (note it). RequestRefreshFrame passthrough. Stop/teardown on window close.
   Thread: bind the consumer receiver and the PrtsFrameSink remote on a dedicated base::SequencedTaskRunner (ThreadPool, USER_BLOCKING)
   so frames do not hop through the UI thread; capturer control calls stay on UI.
4. Notes on GPU-LUID detection for HelloInfo (how the browser can get the GPU adapter LUID at the pin, e.g. content::GpuDataManager
   GPUInfo active_gpu().luid) - give a function.
Scope: no input, no endpoint code. Max ~1000 lines.
