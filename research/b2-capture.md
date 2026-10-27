# b2: FrameSinkVideoCapturer at Chromium 157.0.8103.15

Pin: `1aa07b638799e59656559ae6c6649d4b197f555e`.
[V] means read at the Chromium pin; [I] means inference from named code.
CEF evidence will be explicitly marked as current upstream rather than Chromium-pin evidence.

## 1. CompositorFrame to CopyOutputRequest and delivery

[V] Submission route: `components/viz/service/frame_sinks/compositor_frame_sink_support.cc`,
`CompositorFrameSinkSupport::MaybeSubmitCompositorFrame()` -> `Surface::QueueFrame()`;
`components/viz/service/surfaces/surface.cc`, `CommitFrame()` / `ActivateFrame()` promote an eligible
frame (possibly after dependencies/deadline). `components/viz/service/surfaces/surface_manager.cc`,
`SurfaceManager::SurfaceActivated()` -> `SurfaceModified()` notifies damage observers.
`components/viz/service/display/display_damage_tracker.cc`, `OnSurfaceDamaged()` / `ProcessSurfaceDamage()`
notify the display scheduler; `components/viz/service/display/display_scheduler.cc`,
`DisplayScheduler::OnDisplayDamaged()` sets `needs_draw_`, and deadline/attempt/draw dispatch reaches
`Display::DrawAndSwap()`. Capture then follows this sequence:

1. [V] Submission/activation alone does not call the capturer: damage reaches it during aggregation.
   `components/viz/service/display/display.cc`, `Display::DrawAndSwap()` calls
   `SurfaceAggregator::Aggregate(current_surface_id_, expected_display_time, ...)`.
   `components/viz/service/display/surface_aggregator.cc`, `SurfaceAggregator::PrewalkSurface()`
   computes damage from the selected active surface tree, including embedded child damage.
2. [V] In that prewalk, nonempty damage invokes `Surface::NotifyAggregatedDamage()`
   (`components/viz/service/surfaces/surface.cc`), which forwards to
   `CompositorFrameSinkSupport::OnSurfaceAggregatedDamage()`
   (`components/viz/service/frame_sinks/compositor_frame_sink_support.cc`).
   The support iterates `capture_clients_` and calls `OnFrameDamaged()` with the active
   frame metadata, root size, aggregated damage and expected display time.
3. [V] `components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.cc`,
   `FrameSinkVideoCapturerImpl::OnFrameDamaged()` updates source size/dirty content and calls
   `MaybeCaptureFrame(VideoCaptureOracle::kCompositorUpdate, damage_rect, expected_display_time, metadata)`.
   There is no capturer `OnBeginFrame()` step in this path; BeginFrames drive production/display elsewhere.
4. [V] In the same file, `MaybeCaptureFrame()` consults `ObserveEventAndDecideCapture()`,
   reserves/resurrects a frame, records the capture, and builds `CopyOutputRequest` with
   `DidCopyFrame()` as callback, a result task runner, source token, source area, scaling and selection.
   Texture capture adds a `BlitRequest` targeting the pool's SharedImage.
   It sends `PendingCopyOutputRequest(LocalSurfaceId(), subtree_id, request)` to the resolved sink.
   [V] `compositor_frame_sink_support.cc`, `TakeCopyOutputRequests()`, treats this invalid local ID as
   eligible ASAP; it is not an exact frame-token fence. `GetRequestRegionProperties()` uses the last
   activated surface; `MaybeCaptureFrame()` has a guard for aggregated/activated region-size mismatch.
5. [V] `CompositorFrameSinkSupport::RequestCopyOfOutput()` queues the request and marks the
   last activated surface modified. `SurfaceAggregator::PrewalkSurface()` then immediately calls
   `Surface::TakeCopyOutputRequestsFromClient()` **after** `NotifyAggregatedDamage()`.
   The load-bearing ordering at `components/viz/service/display/surface_aggregator.cc:2079` is:

   ```cpp
   surface->NotifyAggregatedDamage(damage_rect_surface_space,
                                   expected_display_time_);
   }
   // If any CopyOutputRequests were made at FrameSink level, make sure we grab
   // them too.
   surface->TakeCopyOutputRequestsFromClient();
   ```

6. [V] `Surface::TakeCopyOutputRequestsFromClient()` attaches requests to active render passes;
   `SurfaceAggregator::CopyPasses()` takes them with `Surface::TakeCopyOutputRequests()` and
   `MoveMatchingRequests()` moves them to the corresponding aggregated render pass.
   Paths: `components/viz/service/surfaces/surface.cc`,
   `components/viz/service/display/surface_aggregator.cc`.
7. [V] `components/viz/service/display/direct_renderer.cc`,
   `DirectRenderer::DrawRenderPassAndExecuteCopyRequests()` draws the pass, then executes its copies.
   `components/viz/service/display/skia_renderer.cc`, `SkiaRenderer::CopyDrawnRenderPass()`
   forwards to `SkiaOutputSurface::CopyOutput()` with source pass mailbox and geometry.
   `Display::DrawAndSwap()` draws even without ordinary damage when `have_copy_requests` is true.
8. [I] Thus an accepted damage-triggered capture joins the **same aggregation and DrawAndSwap**
   that discovered that damage, observing the selected active target pass and its aggregated children.
   It does not inherently wait for another submitted CompositorFrame or another display frame.
   Evidence: ordering in `SurfaceAggregator::PrewalkSurface()` and
   `DirectRenderer::DrawRenderPassAndExecuteCopyRequests()` above.
9. [I] This is not a guarantee to export every submitted frame: activation, display scheduling,
   oracle sampling, buffer availability and newer active surfaces can discard/coalesce submissions.
   Nor is delivery synchronous with DrawAndSwap returning: GPU execution/completion and posted callbacks
   add elapsed time, but the code has no mandatory one-extra-frame staging delay.
   Evidence: `FrameSinkVideoCapturerImpl::MaybeCaptureFrame()` and `CopyOutputRequest` callback setup.
10. [V] `FrameSinkVideoCapturerImpl::DidCopyFrame()` validates/readbacks the result, applies CPU
    overlays/letterboxing where needed, marks reusable content, then calls `OnFrameReadyForDelivery()`.
    That function uses a frame-number min-heap to preserve order; `MaybeDeliverFrame()` validates the
    capture version, consults the oracle, clones a buffer handle and sends `consumer_->OnFrameCaptured()`.
    `InFlightFrameDelivery` holds the frame until consumer `Done()`/connection teardown; feedback goes
    to the oracle. Path: `components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.cc`.

[V] GPU timing: `components/viz/service/display_embedder/skia_output_surface_impl.cc`,
`SkiaOutputSurfaceImpl::CopyOutput()` enqueues GPU work; `skia_output_surface_impl_on_gpu.cc`,
`CopyOutputRGBAInTexture()` / `CopyOutputNV12()` wait for completion for mappable blits.
`skia_render_copy_results.cc`, `ReadbackContextTexture::OnMailboxReadyInternal()` sends the result;
`components/viz/common/frame_sinks/copy_output_request.cc`, `CopyOutputRequest::SendResult()` posts
the callback to the capturer sequence. In the same display_embedder directory,
`skia_output_surface_dependency_impl.cc`, `ScheduleDelayedGPUTaskFromGPUThread()` polls at 2 ms
when completion checks need rescheduling; this is elapsed-time overhead, not another display frame.

## 2. Pixel formats, buffers, Windows GPU output and D3D11 sharing

[V] `FrameSinkVideoCapturerImpl::SetFormat()` accepts **I420, ARGB, NV12 and RGBAF16**;
the capturer default is I420. `VideoPixelFormatToCopyOutputRequestFormat()` maps these to
I420_PLANES, RGBA, NV12 and RGBAF16 respectively. Source:
`components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.cc`.

[V] `services/viz/privileged/mojom/compositing/frame_sink_video_capture.mojom`,
`BufferFormatPreference`, has exactly `kDefault = 0`, `kPreferMappableSharedImage`,
and `kPreferSharedImageWithNativeHandle`. The last value's texture-capture selection is
Linux-only in `FrameSinkVideoCapturerImpl::MaybeCaptureFrame()`; use the mappable preference on Windows.

| Format | Default preference | Mappable SharedImage preference | Evidence at pin |
| --- | --- | --- | --- |
| I420 | Read-only shared memory | Still shared-memory I420 | [V] impl.cc, `GetVideoFramePoolForFormat()` / `MaybeCaptureFrame()` |
| ARGB | Read-only shared memory, RGBA copy/readback | GPU SharedImage / native GMB handle | [V] same functions |
| NV12 | Rejected by `Start()` CHECK | GPU SharedImage / native GMB handle; required preference | [V] impl.cc, `SetFormat()` / `Start()` |
| RGBAF16 | Rejected by `Start()` CHECK | GPU SharedImage / native GMB handle; required preference | [V] impl.cc, `Start()` |

[V] In that table `impl.cc` denotes
`components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.cc`.
`SetFormat(NV12)` requires a GMB pool context and changing *to* NV12 during capture CHECK-fails.
`Start()` also requires the context for ARGB/RGBAF16 with the mappable preference.

[V] `GetVideoFramePoolForFormat()` chooses `MappableSharedImageVideoFramePool` for GPU output.
`components/viz/service/frame_sinks/video_capture/mappable_shared_image_video_frame_pool.cc`,
`RecreateVideoFramePool()` creates `media::RenderableMappableSharedImageVideoFramePool`;
`CloneHandleForDelivery()` returns `VideoBufferHandle::NewGpuMemoryBufferHandle(frame.GetGpuMemoryBufferHandle())`.
`components/viz/service/frame_sinks/video_capture/shared_memory_video_frame_pool.cc`, `SharedMemoryVideoFramePool::CloneHandleForDelivery()`,
instead duplicates the read-only shared-memory region.

[V] `media/gfx/renderable_mappable_shared_image_video_frame_pool.cc`, `FrameResources::Initialize()`,
uses `SCANOUT_CPU_READ_WRITE` on Windows and render/display/GLES2 SharedImage usages.
`FrameResources::CreateVideoFrame()` wraps it with `VideoFrame::WrapMappableSharedImage()`;
being mappable does **not** mean pixel data is copied to CPU during this capture path.
`media/base/format_utils.cc`, `VideoPixelFormatToSharedImageFormat()`, maps ARGB to BGRA_8888
and NV12 to a biplanar NV12 SharedImage.
`GetCodedSizeForVideoPixelFormat()` pads NV12 coded width to four and height to two;
consumers must honor coded size, visible rect and content rect.

[V] `components/viz/service/frame_sinks/gmb_video_frame_pool_context_provider_impl.cc`,
`GmbVideoFramePoolContext::InitializeOnGpu()` explicitly creates the in-process SharedImage interface
with `always_create_native_gmb_handle=true`; `CreateSharedImage()` submits the pool allocation to it.
`gpu/command_buffer/service/shared_image_interface_in_process.cc`, `CreateSharedImage()`, explicitly
creates a native handle in that mode, then imports it via the base interface.
`gpu/command_buffer/service/shared_image/shared_image_factory.cc`,
`SharedImageFactory::CreateNativeGpuMemoryBufferHandle()` uses `D3DImageBackingFactory` on Windows.

[V] `gpu/command_buffer/service/shared_image/d3d_image_backing_factory.cc`,
`D3DImageBackingFactory::CreateGpuMemoryBufferHandle()`, allocates a D3D11 DEFAULT texture with
shader-resource/render-target bind flags, no CPU access flags, and:

```cpp
D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
    D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX
```

[V] That function calls `IDXGIResource1::CreateSharedHandle()` with read/write access and wraps
the NT handle in `gfx::DXGIHandle` / `gfx::GpuMemoryBufferHandle` (`DXGI_SHARED_HANDLE`).
`GetDXGIFormatForGMB()` supports BGRA_8888 and NV12 (`DXGI_FORMAT_NV12`) as well as other formats.
The GMB overload of `D3DImageBackingFactory::CreateSharedImage()` imports it through
`DXGISharedHandleManager`, opens the D3D11 texture, and creates a **D3DImageBacking**.
These are the native-GMB allocation/import functions, not the separate internally allocated
SharedImage branch that may choose shared fences instead of keyed mutexes. Same file.

[V] `gpu/command_buffer/service/dxgi_shared_handle_manager.cc`,
`DXGISharedHandleState::AcquireKeyedMutex()` / `ReleaseKeyedMutex()`, use the same
`kDXGIKeyedMutexAcquireKey` on acquire and release; `gpu/command_buffer/common/constants.h`
defines that key as **0**. The exported capture allocation is keyed-mutex
backed by the texture flags above; there is no built-in producer-key-0/consumer-key-1 handoff here.

[V] **NV12 GPU output exists.** `components/viz/service/display_embedder/skia_output_surface_impl_on_gpu.cc`,
`SkiaOutputSurfaceImplOnGpu::CopyOutputNV12()` renders/scales an intermediate RGBA image, then
`skia::BlitRGBAToYUVA()` converts it into Y and interleaved UV destination planes using Rec.709 limited range.
For the mappable blit destination it waits for GPU completion and returns a SharedImage result;
`FrameSinkVideoCapturerImpl::DidCopyFrame()` performs no NV12 CPU plane readback.

[I] An encoder that imports compatible D3D11 NV12 textures can therefore consume the captured
NV12 allocation without an application BGRA-to-NV12 conversion. This verifies the export primitive,
not a particular encoder's import/adapter/lifetime support. Hold the capture lease until reads finish.
Evidence: `CopyOutputNV12()`, `GetDXGIFormatForGMB()` (file-local helper in the D3D factory file),
and `FrameSinkVideoCapturerImpl::MaybeDeliverFrame()`.
[V] GPU support remains capability-dependent: `D3DImageBackingFactory` constructor checks NV12
TEXTURE2D/SHADER_SAMPLE/RENDER_TARGET support; `IsSupported()` rejects unsupported formats/devices.
Path: `gpu/command_buffer/service/shared_image/d3d_image_backing_factory.cc`.
[V] `gpu/command_buffer/service/shared_image/d3d_image_backing.cc`, `D3DImageBacking::ProduceSkiaGanesh()`
uses `ProduceGLTexturePassthrough()`, which exposes individual NV12 planes with RED/RG GL formats;
`ProduceSkiaGraphite()` instead imports through Dawn. The Windows backing has multiplanar Skia paths.

## 3. Failed/invalidated capture and CompleteCapture

[V] **Yes, the bug is still present at the pin.**
`components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.cc:1524`,
`FrameSinkVideoCapturerImpl::MaybeDeliverFrame()` only calls `CompleteCapture()` under success,
and its `else if` bypasses completion when the capture version changed:

```cpp
if (frame_capture.success()) {
  // ...
  if (frame_capture.frame->metadata().capture_version != capture_version()) {
    frame_capture.CaptureFailed(CaptureResult::kSubCaptureTargetChanged);
  } else if (!oracle_->CompleteCapture(frame_capture.oracle_frame_number,
                                       frame_capture.success(),
                                       &media_ticks)) {
```

[V] The later `if (!frame_capture.success())` branch only traces, calls
`MaybeScheduleRefreshFrame()`, and returns. `FrameCapture::CaptureFailed()` records failure;
it does not complete oracle accounting. `DidCopyFrame()` reaches this path for failed
I420/ARGB readbacks and empty NV12/mappable SharedImage results. Same path and class.

[I] Prior patch 0013's unconditional completion is therefore still relevant to this pin,
with the pin's metadata spelling `capture_version` rather than the prior patch's `capture.version`.
Evidence: `FrameSinkVideoCapturerImpl::MaybeDeliverFrame()` above; historical comparison only:
`../browser/patches/0013-viz-complete-failed-video-captures-before-retrying-r.patch`.

## 4. Pacing, static content and forcing refresh

[V] `components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.cc`,
`SetMinCapturePeriod()`: sets the oracle sampling period, clamped to the media maximum frame rate;
without a high-resolution clock it caps at 30 FPS. Changing it while a retry timer runs calls
`RefreshNow()`. This controls capture sampling, not compositor BeginFrame production.
`services/viz/privileged/mojom/compositing/frame_sink_video_capture.mojom`,
`FrameSinkVideoCapturer::SetMinCapturePeriod`, documents the default as 200 ms / 5 FPS.

[V] Same impl file, `SetMinSizeChangePeriod()` forwards to the oracle; zero disables the delay.
`media/capture/content/video_capture_oracle.cc`, `VideoCaptureOracle::ObserveEventAndDecideCapture()`,
applies this delay when adaptive throttling is active; otherwise a changed size commits ASAP.
The mojom `SetMinSizeChangePeriod` default is three seconds.

[V] `VideoCaptureOracle::SetAutoThrottlingEnabled(false)` selects maximum allowed resolution within
constraints, instead of adapting it to pool/consumer utilization. It does not bypass FPS sampling
or pool exhaustion. Paths: `media/capture/content/video_capture_oracle.cc` and impl.cc,
`FrameSinkVideoCapturerImpl::MaybeCaptureFrame()` (the table's `impl.cc` path above).

[V] `FrameSinkVideoCapturerImpl::RequestRefreshFrame()` stops the retry timer and immediately calls
`RefreshNow()` -> `RefreshInternal(kRefreshDemand)` -> `MaybeCaptureFrame()`.
The oracle treats `kRefreshDemand` like `kCompositorUpdate`, including the normal samplers;
it is higher priority than passive `kRefreshRequest`, but not an unconditional sample.
Paths: impl.cc and `media/capture/content/video_capture_oracle.cc`,
`VideoCaptureOracle::ObserveEventAndDecideCapture()`.

[V] The passive retry uses `kRefreshRequest`: requires `num_frames_pending_ == 0`, plus either
no animation proposal or a gap over 250 ms since animation was last detected.
`RecordCapture()` increments pending; `CompleteCapture()` decrements it. Thus Q3's skipped completion
can permanently block passive retries on a static source until cancellation resets accounting.
`VideoCaptureOracle::CancelAllCaptures()` resets pending; capturer `Stop()` invokes it.
Paths: `media/capture/content/video_capture_oracle.cc`; impl.cc, `FrameSinkVideoCapturerImpl::Stop()`.

[V] On rejection/failure, `MaybeScheduleRefreshFrame()` retries after
`min(1 second, 2 * max(estimated_frame_duration, min_capture_period))`;
it is not a recurring idle-frame clock after successful delivery. Static content needs no damage
and no fresh GPU copy for explicit refresh: `CanResurrectFrame()` accepts unchanged content/version
and matching size; `MaybeCaptureFrame()` can deliver that marked frame immediately after sampling.
Path: impl.cc, `GetDelayBeforeNextRefreshAttempt()`, `MaybeScheduleRefreshFrame()`,
`CanResurrectFrame()`, `MaybeCaptureFrame()`.

[I] For low delay, configure the desired period before `Start()`, disable resolution auto-throttling,
optionally disable animation lock-in with `SetAnimationFpsLockIn(false, ...)`, release leases promptly,
and issue `RequestRefreshFrame()` when a duplicate/recovery image is needed.
This can promptly sample current content; it does **not** arm an unconditional capture of the next
future compositor submission or force that submission to exist. A demand may resurrect old content,
be FPS-limited, or encounter pool pressure; a new damaged frame still follows Q1's sampler path.
Evidence: impl.cc, `RequestRefreshFrame()`, `MaybeCaptureFrame()`;
`media/capture/content/video_capture_oracle.cc`, `ObserveEventAndDecideCapture()`.

## 5. Current CEF accelerated OSR and external BeginFrames

CEF evidence below is [V] **upstream CEF**, not code at the Chromium pin:
GitHub `chromiumembedded/cef` master resolved to
`e537569ecda2e3f23ffdac559ecf7f80b8e59467` (commit date 2026-10-23), read 2026-10-25.

[V] `libcef/browser/osr/video_consumer_osr.h`, `CefVideoConsumerOSR::video_capturer_`, is
`std::unique_ptr<viz::ClientFrameSinkVideoCapturer>`.
Source: [CEF header](https://github.com/chromiumembedded/cef/blob/e537569ecda2e3f23ffdac559ecf7f80b8e59467/libcef/browser/osr/video_consumer_osr.h).
[V] `libcef/browser/osr/video_consumer_osr.cc`, constructor / `SetActive()`:
ARGB; animation FPS lock-in disabled; auto-throttling disabled; zero minimum size-change period;
fixed size constraints. Shared-texture Windows/macOS use `kPreferMappableSharedImage`,
Linux uses `kPreferSharedImageWithNativeHandle`; CPU mode uses `kDefault`.
`SetFrameRate()` forwards to `SetMinCapturePeriod()`.

[V] Same file, `CefVideoConsumerOSR::OnFrameCaptured()` accepts ARGB/ABGR GPU handles,
derives BGRA/RGBA from delivered format, and on Windows forwards
`gmb_handle.dxgi_handle().buffer_handle()` through `view_->OnAcceleratedPaint()`.
`ScopedVideoFrameDone` returns the capture lease after the callback scope.
Source: [CEF consumer](https://github.com/chromiumembedded/cef/blob/e537569ecda2e3f23ffdac559ecf7f80b8e59467/libcef/browser/osr/video_consumer_osr.cc).

[V] `libcef/browser/osr/render_widget_host_view_osr.cc`, constructor, creates `ui::Compositor`
with external-BeginFrame mode when requested. `ShowWithVisibility()` creates the video consumer
when GPU compositing is available. `OnAcceleratedPaint()` calls the client's render handler.
`SendExternalBeginFrame()` creates NORMAL BeginFrameArgs with Now(), incremented sequence,
and deadline Now()+configured interval, progresses flings, and calls
`compositor_->IssueExternalBeginFrame(args, OnFrameComplete)`; it also forwards to a popup view.
`OnFrameComplete()` acknowledges and sends a queued request. The current file does not implement
an `OnBeginFrame()` capture callback; external production and video capture remain separate paths.
Source: [CEF OSR view](https://github.com/chromiumembedded/cef/blob/e537569ecda2e3f23ffdac559ecf7f80b8e59467/libcef/browser/osr/render_widget_host_view_osr.cc).

[V] `libcef/browser/osr/external_begin_frame_request_state.h`,
`CefExternalBeginFrameRequestState::Request()` / `Acknowledge()`, serialize outstanding BeginFrames
and coalesce overlapping requests into one additional frame after ACK.
Source: [CEF request state](https://github.com/chromiumembedded/cef/blob/e537569ecda2e3f23ffdac559ecf7f80b8e59467/libcef/browser/osr/external_begin_frame_request_state.h).
[V] `libcef/browser/osr/browser_platform_delegate_osr.cc`,
`CefBrowserPlatformDelegateOsr::SendExternalBeginFrame()`, forwards to the OSR view.
Source: [CEF delegate](https://github.com/chromiumembedded/cef/blob/e537569ecda2e3f23ffdac559ecf7f80b8e59467/libcef/browser/osr/browser_platform_delegate_osr.cc).
