# b1: Windows new-headless root compositor output path

Pin: `1aa07b638799e59656559ae6c6649d4b197f555e` (local HEAD checked).
Evidence: [V] read at this pin; [I] inference from the cited implementation.
All code paths below are relative to `src/`. This report concerns the browser root, not a WebContents-only surface.

## 1. GPU root Display: output-device and presenter selection

- [V] `ui/aura/window_tree_host.cc`, `WindowTreeHost::OnAcceleratedWidgetAvailable`, supplies the host's accelerated widget to `ui::Compositor::SetAcceleratedWidget`.
  `ui/views/widget/desktop_aura/desktop_window_tree_host_win.cc`, `DesktopWindowTreeHostWin::GetAcceleratedWidget`, returns the retained HWND.
- [V] `ui/compositor/compositor.cc`, `Compositor::SetAcceleratedWidget` / `RequestNewLayerTreeFrameSink`, invokes the ContextFactory.
  `content/browser/compositor/viz_process_transport_factory.cc`, `VizProcessTransportFactory::OnEstablishedGpuChannel`, sets `root_params->widget = compositor->widget()` and `frame_sink_id = compositor->frame_sink_id()`.
  GPU selection is `!is_gpu_compositing_disabled_ && !compositor->force_software_compositor()`, with context failure falling back to software.
- [V] `components/viz/service/frame_sinks/root_compositor_frame_sink_impl.cc`, `RootCompositorFrameSinkImpl::Create`, passes that widget into `CreateGpuDependency` and `CreateOutputSurface`, then constructs the root `Display`.
  The intervening calls are `components/viz/host/host_frame_sink_manager.cc`, `HostFrameSinkManager::CreateRootCompositorFrameSink` -> `components/viz/service/frame_sinks/frame_sink_manager_impl.cc`, `FrameSinkManagerImpl::CreateRootCompositorFrameSink` -> `RootCompositorFrameSinkImpl::Create`.
- [V] `components/viz/service/display_embedder/output_surface_provider_impl.cc`, `OutputSurfaceProviderImpl::CreateOutputSurface`, selects `SkiaOutputSurfaceImpl::Create` for GPU compositing; its `headless_` flag does not select a GPU output device.
- [V] `components/viz/service/display_embedder/skia_output_surface_dependency_impl.cc`, `SkiaOutputSurfaceDependencyImpl::IsOffscreen`, means exactly `surface_handle_ == gpu::kNullSurfaceHandle`.
  [I] The retained, non-null headless HWND therefore takes the onscreen GPU-device branches; `--headless` does not itself imply `SkiaOutputDeviceOffscreen`.
- [V] `components/viz/service/display_embedder/skia_output_surface_impl_on_gpu.cc`, `SkiaOutputSurfaceImplOnGpu::Initialize`, selects Vulkan, Graphite/Dawn, or GL from the actual shared context.
  `InitializeForGL`: offscreen dependency -> `SkiaOutputDeviceOffscreen`; otherwise presenter -> Windows `SkiaOutputDeviceDComp`; no presenter -> `SkiaOutputDeviceGL` (external-stencil requirement is false in this dependency).
- [V] `gpu/ipc/service/image_transport_surface_win.cc`, `ImageTransportSurface::CreatePresenter`, returns `gl::DCompPresenter` when `gl::DirectCompositionSupported()`, otherwise null.
  `ImageTransportSurface::CreateNativeGLSurface` then uses `gl::NativeViewGLSurfaceEGL` with `gl::VSyncProviderWin` for ANGLE.
  `ui/gl/direct_composition_support.cc`, `gl::DirectCompositionSupported`, returns whether the global DComp device exists.

- [V] `components/viz/service/display_embedder/skia_output_surface_impl.cc`, `SkiaOutputSurfaceImpl::Create` -> `Initialize` -> `InitializeOnGpuThread`, constructs `SkiaOutputSurfaceImplOnGpu::Create` and copies its device capabilities back.
- [V] `ui/gl/direct_composition_support.cc`, `gl::InitializeDirectComposition`, requires a D3D11 device; returns early for `--disable-direct-composition`, injected `MCTU.dll` / `MCTUX.dll`, missing DXGI/DComp modules or `DCompositionCreateDevice3`, or failed device/interface creation.
  `gpu/ipc/service/gpu_init.cc`, `GpuInit::InitializeAndStartSandbox`, obtains the D3D11 device from Dawn or `QueryD3D11DeviceObjectFromANGLE` before this call.
  [I] Backend/device availability and these gates, rather than cloak, decide whether the usual DComp route is available.
- [V] `ui/gl/dcomp_presenter.cc`, `gl::DCompPresenter::DCompPresenter`, creates a child window and initializes a `DCLayerTree` against it.
  `components/viz/service/display_embedder/skia_output_surface_impl_on_gpu.cc`, `InitializeForGL` / `InitializeForDawn`, forwards `presenter_->GetWindow()` through `AddChildWindowToBrowser` before constructing `SkiaOutputDeviceDComp`.
  `content/browser/compositor/viz_process_transport_factory.cc`, local `HostDisplayClient::AddChildWindowToBrowser`, asks `GpuHostImpl::AddChildWindow` to parent this child under the browser HWND.
- [V] `components/viz/service/display_embedder/skia_output_device_dcomp.cc`, `SkiaOutputDeviceDComp` constructor, sets `renderer_allocates_images = true`; `BeginPaint` / `EndPaint` are unimplemented.
  `ScheduleOverlays` resolves SharedImage mailboxes to overlay images and calls `Presenter::ScheduleDCLayers`; `Present` calls `DCompPresenter::Present`.
  [I] It is incorrect to model this device as a single universally readable root swapchain owned by the presenter.
- [V] `components/viz/service/display/skia_renderer.cc`, `SkiaRenderer::AllocateRenderPassResourceIfNeeded`, allocates non-BufferQueue scanout backings with `SCANOUT_DCOMP_SURFACE` when requested and the format is not RGBA_1010102, otherwise `SCANOUT_DXGI_SWAP_CHAIN`; DComp-surface usage removes `DISPLAY_READ` because it is write-only.
  `gpu/command_buffer/service/shared_image/dcomp_image_backing_factory.cc`, `DCompImageBackingFactory::CreateSharedImage`, dispatches those usages to `DCompSurfaceImageBacking::Create` or `DXGISwapChainImageBacking::Create`.
  [V] `components/viz/service/display/direct_renderer.cc`, `DirectRenderer::CalculateRenderPassRequirements`, requests a DComp surface for scanout passes needing synchronous DComp commit, unless `kBufferQueuePerRenderPass` is enabled.
  `components/viz/service/display/output_surface.cc`, `IsBufferQueueSupportedAndEnabled`, requires dynamic-DComp-texture support plus `kBufferQueue`; `IsDelegatedCompositingSupportedAndEnabled` requires DComp-texture support plus the delegated-compositing feature.
- [V] `ui/gl/dc_layer_tree.cc`, anonymous `NeedSwapChainPresenter`, requires an overlay image whose type is not `kDCompVisualContent`.
  `DCLayerTree::CommitAndClearPendingOverlays` uses `SwapChainPresenter::PresentToSwapChain` for such images; pre-existing DComp visual content is attached directly.
  [I] `SwapChainPresenter` is a per-layer conversion/presentation helper, not a different root `SkiaOutputDevice` selection.
  [V] `ui/gl/dcomp_presenter.h`, class `DCompPresenter`, owns `ChildWindowWin child_window_` and explicitly documents that it does not own the root surface.
  [I] The relevant presenter name at this pin is `DCompPresenter`; `DirectCompositionChildSurfaceWin` is not the implementation on this path (no occurrence in the searched `ui/gl`, `gpu/ipc/service`, or `components/viz` worktrees).
- [V] `components/viz/service/display_embedder/skia_output_surface_impl_on_gpu.cc`, `InitializeForDawn`, uses DComp when a presenter exists, otherwise `SkiaOutputDeviceDawn::Create` plus its child handle.
  `components/viz/service/display_embedder/skia_output_device_dawn.cc`, `SkiaOutputDeviceDawn::Create`, further selects `SkiaOutputDeviceDawnD3D11BltMode` for Dawn D3D11, otherwise its internal `SkiaOutputDeviceDawnSwapChain` using a `wgpu::Surface`; `SkiaOutputDeviceDawnSwapChain::Initialize` creates a GPU-owned child HWND for its Windows D3D backend.
  `components/viz/service/display_embedder/skia_output_device_dawn_d3d11_blt_mode.cc`, `SkiaOutputDeviceDawnD3D11BltMode::Initialize`, creates a DXGI HWND swapchain against the supplied HWND.
  `InitializeForVulkan`, when compiled and selected, uses `SkiaOutputDeviceVulkan::Create` unless `disable_vulkan_surface`; offscreen Vulkan/Dawn use `SkiaOutputDeviceOffscreen`.
  [I] GPU-on alone does not uniquely specify GL versus Graphite/Dawn versus explicitly selected Vulkan.

## 2. Software root Display: output-device selection

- [V] `components/viz/service/main/viz_compositor_thread_runner_impl.cc`, `VizCompositorThreadRunnerImpl::CreateFrameSinkManagerOnCompositorThread`, reads `switches::kHeadless` and constructs `OutputSurfaceProviderImpl(..., headless)`.
  `content/browser/gpu/gpu_process_host.cc`, `GpuProcessHost::LaunchGpuProcess` and its `kSwitchNames` propagation list, include `switches::kHeadless`, so the Viz/GPU process receives the browser switch.
- [V] `components/viz/service/display_embedder/output_surface_provider_impl.cc`, `OutputSurfaceProviderImpl::CreateOutputSurface`, wraps the selected software device in `SoftwareOutputSurface` when `gpu_compositing == false`.
  `CreateSoftwareOutputDeviceForPlatform` checks `headless_` first and returns plain `viz::SoftwareOutputDevice`, before Windows dispatch.
- [V] `components/viz/service/display/software_output_device.cc`, `SoftwareOutputDevice::Resize`, allocates an opaque N32 raster `SkSurface`; `BeginPaint` returns its canvas, `EndPaint` is empty, and `OnSwapBuffers` posts an asynchronous acknowledgement.
  [I] Thus new-headless software rendering has an in-memory root image but no Windows display blit, LayeredWindowUpdater, or DXGI swapchain.
- [V] For non-headless comparison, `components/viz/service/display_embedder/software_output_device_win.cc`, `CreateSoftwareOutputDeviceWin`, selects:
  `ShouldRemoveRedirectionBitmap()` -> `SoftwareOutputDeviceWinSwapChain`; otherwise `NeedsToUseLayerWindow(hwnd)` -> `SoftwareOutputDeviceWinProxy`; otherwise `SoftwareOutputDeviceWinDirect`.
  `SoftwareOutputDeviceWinProxy::ResizeDelegated` transfers shared memory to `mojom::LayeredWindowUpdater`; `EndPaintDelegated` calls `Draw`, with swap acknowledgement gated by `DrawAck`.
  `SoftwareOutputDeviceWinDirect::EndPaintDelegated` copies the platform-canvas HDC to the HWND's DC.
- [V] `components/viz/service/display_embedder/software_output_surface.cc`, `SoftwareOutputSurface::SwapBuffers` / `SwapBuffersCallback`, calls the device's `OnSwapBuffers`, forwards `SWAP_ACK`, and generates presentation feedback locally.
  [I] Headless software frames therefore do not wait for an HWND to become visible or for DWM presentation acknowledgement.
- [V] `components/viz/common/features.cc`, `features::ShouldRemoveRedirectionBitmap`, requires Windows 11 22H2+, `kRemoveRedirectionBitmap`, no `--disable-direct-composition`, no `--override-use-software-gl-for-tests`, and no explicit ANGLE OpenGL backend.
  `components/viz/common/display/use_layered_window.cc`, `NeedsToUseLayerWindow`, tests the HWND's `ui::kWindowTranslucent` property.
- [V] `components/viz/host/host_display_client.cc`, `HostDisplayClient::CreateLayeredWindowUpdater`, constructs `LayeredWindowUpdaterImpl` only for a layered-window HWND.
  `components/viz/host/layered_window_updater_impl.cc`, `LayeredWindowUpdaterImpl::Draw`, uses `UpdateLayeredWindow` and acknowledges the request.
  `components/viz/service/display_embedder/software_output_device_win_swapchain.cc`, `SoftwareOutputDeviceWinSwapChain`, creates its own DComp child HWND and lazily creates DXGI/DComp resources during `ResizeDelegated`.
  [I] These Windows device implementations are bypassed by the headless-first factory return even when GPU compositing falls back at runtime.

## 3. Headless, cloak, visibility, occlusion and frame production

- [V] `ui/aura/window_tree_host.cc`, `WindowTreeHost::WindowTreeHost`, sets `native_window_occlusion_enabled_` to `!HasSwitch(kHeadless) && IsEnabled(kCalculateNativeWinOcclusion)` on Windows.
  `ui/aura/native_window_occlusion_tracker.cc`, `NativeWindowOcclusionTracker::EnableNativeWindowOcclusionTracking`, only enables `NativeWindowOcclusionTrackerWin` if the host permits it.
  [I] The Chrome `--headless=new` root is excluded from native HWND-occlusion tracking at host construction.
- [V] `ui/aura/native_window_occlusion_tracker.cc`, `NativeWindowOcclusionTracker::IsNativeWindowOcclusionTrackingAlwaysEnabled`, separately recognizes `CHROME_HEADLESS`, with an `AlwaysTrackNativeWindowOcclusionForTest` exception, plus host/feature gates.
  [I] This environment-variable behavior is separate from the stronger `--headless` constructor gate.
- [V] `ui/views/win/hwnd_message_handler_headless.cc`, `HWNDMessageHandlerHeadless::Init`, creates a real HWND and calls `DwmSetWindowAttribute(DWMWA_CLOAK, TRUE)`.
  `Show` / `Hide` maintain `is_visible_` and explicitly invoke `delegate_->HandleVisibilityChanged(true/false)` while keeping the platform window hidden.

- [V] `ui/views/widget/desktop_aura/desktop_window_tree_host_win.cc`, `DesktopWindowTreeHostWin::Show`, first calls `OnAcceleratedWidgetMadeVisible(true)`, shows the Aura content window, then invokes the headless message handler.
  `ShowImpl` delegates to `Show`; `HideImpl` calls the handler's `Hide` unless closing.
  `ui/aura/window_tree_host.cc`, `WindowTreeHost::Show` / `Hide`, also explicitly call `OnAcceleratedWidgetMadeVisible(true/false)`.
  `OnAcceleratedWidgetMadeVisible` -> `UpdateCompositorVisibility` -> `Compositor::SetVisible` is the logical compositor gate; no OS `ShowWindow` visibility is required for this gate.
- [V] `ui/compositor/compositor.cc`, `Compositor::SetVisible`, sets the cc host visibility and `DisplayPrivate::SetDisplayVisible(kVisible/kInvisible)`; it notes invisible compositors do not draw and flushes pending surface copies before hiding when possible.
  [I] Explicit hide, minimize, suspension or a caller setting visibility false can still suppress useful output; cloaking alone is not wired to that gate in the inspected headless path.
  [V] `ui/views/win/hwnd_message_handler_headless.cc`, `HWNDMessageHandlerHeadless::Minimize`, calls `HandleWindowMinimizedOrRestored(false)`; `ui/views/widget/desktop_aura/desktop_window_tree_host_win.cc`, `HandleWindowMinimizedOrRestored`, hides the Aura root when minimized and shows it when restored.
- [V] `components/viz/service/frame_sinks/root_compositor_frame_sink_impl.cc`, `RootCompositorFrameSinkImpl::Create`, normally chooses `ExternalBeginFrameSourceWin` for a DComp-capable output, otherwise `DelayBasedBeginFrameSource`.
  Explicit external-begin-frame control takes precedence; `disable_frame_rate_limit` chooses `BackToBackBeginFrameSource`.
  `components/viz/service/frame_sinks/external_begin_frame_source_win.cc`, `ExternalBeginFrameSourceWin::OnNeedsBeginFrames`, subscribes to `VSyncThreadWin`; `SetPreferredInterval` can choose half refresh rate.
  [I] Headless does not guarantee an unthrottled fixed-rate stream: demand, damage, scheduler backpressure, chosen frame interval and external control still apply.
- [V] `ui/gl/dcomp_presenter.cc`, `DCompPresenter::Present`, commits the layer tree and immediately runs swap completion with `SWAP_ACK` on success.
  `EnqueuePendingFrame` / `CheckPendingFrames` synthesize presentation feedback from the next global vsync (or a posted task when GPU vsync is disabled); no HWND-cloak or screen-visible check appears there.
  `ui/gl/swap_chain_presenter.cc`, `SwapChainPresenter::FinishPresentToSwapChain`, explicitly accepts `DXGI_STATUS_OCCLUDED` from `Present` / `PresentBuffer` rather than treating it as failure or stopping its loop.
  [I] A successful callback does not prove DWM displayed the cloaked window or that its complete GPU work finished; the pin's `CheckPendingFrames` has a TODO to restore GPU-query completion tracking.
- [I] Source inspection cannot establish whether a particular Windows driver/DWM combination blocks, discards or delays a cloaked presentation. The verified Chromium branches above do not implement a cloak-triggered stop, but a runtime measurement is needed for OS-side timing guarantees.
  Evidence boundary: `ui/gl/dcomp_presenter.cc`, `DCompPresenter::Present`; `ui/gl/swap_chain_presenter.cc`, `SwapChainPresenter::FinishPresentToSwapChain`; `ui/views/win/hwnd_message_handler_headless.cc`, `Init`.

- [V] `gpu/command_buffer/service/shared_image/dxgi_swap_chain_image_backing.cc`, `DXGISwapChainImageBacking::Present`, calls `Present1` with interval 0, accepts `DXGI_STATUS_OCCLUDED`, and returns failure only for an actual failed status.
  Its first swap waits on `IDXGIDevice2::EnqueueSetEvent` to avoid a black first frame; later presentation is not conditioned on HWND visibility.
  `ui/gl/gl_surface_egl.cc`, `NativeViewGLSurfaceEGL::SwapBuffers`, calls `eglSwapBuffers` and handles EGL failure without a headless/cloak branch.
- [V] `ui/gl/vsync_thread_win.cc`, `VSyncThreadWin::WaitForVSync`, falls back to timed `Sleep` when the hardware/compositor-clock wait fails or returns within 1 ms, including monitor-sleep/unplug cases noted in its comment.
  `PostTaskIfNeeded` stops scheduling while the power monitor reports suspension; `OnResume` restarts it.
  [I] The vsync implementation includes a monitor-loss fallback, but it is not evidence of tested session-0/RDP/cloak behavior.
- [V] Search audit (`headless|Headless`, production `.cc` / `.h` in the four requested trees): `ui/compositor` contains no explicit headless test; the relevant visibility gate remains `Compositor::SetVisible` in `ui/compositor/compositor.cc`.
  `components/viz/service/main/viz_compositor_thread_runner_impl.cc`, `CreateFrameSinkManagerOnCompositorThread`, and `components/viz/service/display_embedder/output_surface_provider_impl.cc`, `CreateSoftwareOutputDeviceForPlatform`, contain the operative Windows Viz headless selection checks; no GPU-device headless check was found.
  `ui/aura/window_tree_host.cc`, host constructor, and `ui/aura/native_window_occlusion_tracker.cc`, `IsNativeWindowOcclusionTrackingAlwaysEnabled`, contain the occlusion gates; `ui/aura/client/aura_constants.{h,cc}` defines `kHeadlessBoundsKey`.
  `ui/views/widget/desktop_aura/desktop_screen_win.cc`, `CreateDesktopScreen`, selects `DesktopScreenWinHeadless`; `desktop_window_tree_host_win.cc` there, `HandleHeadlessWindowBoundsChanged`, sets that Aura property.
  `ui/views/widget/desktop_aura/desktop_screen_win_headless.cc`, `IsNativeWindowOccluded`, simply reads the host state; it does not re-enable the tracker.
  Other search matches are Ozone screen forwarding, Mac external-begin-frame comments, and BeginFrameArgs documentation, outside this Windows execution path.
- [V] `ui/aura/native_window_occlusion_tracker_win.cc`, `NativeWindowOcclusionTrackerWin::Enable`, registers the real root HWND and observes the Aura root; `WindowOcclusionCalculator::RegisterEventHooks` registers cloak/uncloak events.
  Its `WindowCanOccludeOtherWindowsOnCurrentVirtualDesktop` and `IsWindowOnCurrentVirtualDesktop` check visibility/opacity and cloak/virtual-desktop state.
  [I] These checks would matter for a tracked ordinary HWND; Q3's headless host gate prevents entering this tracking path for the requested root.

## 4. Existing root-image acquisition hooks and signatures

- [V] Root-layer copy exists: `ui/compositor/layer.h`, `ui::Layer::RequestCopyOfOutput`:
  `void RequestCopyOfOutput(std::unique_ptr<viz::CopyOutputRequest> request);`
  `ui/compositor/layer.cc`, the implementation, supplies the compositor task runner if absent and forwards to `cc::Layer::RequestCopyOfOutput`.
  `ui/compositor/compositor.h`, `Compositor::root_layer`, returns `Layer*`; `ui/aura/window_tree_host.cc`, `InitCompositor`, sets that root to `window()->layer()`.
  [I] Calling it on the browser root captures the browser's composed layer subtree, including browser UI and embedded content; other top-level popup roots require separate capture/composition.
- [V] `components/viz/common/frame_sinks/copy_output_request.h`, `viz::CopyOutputRequest`, exact constructor and callback type:
  `CopyOutputRequest(ResultFormat result_format, ResultDestination result_destination, CopyOutputRequestCallback result_callback);`
  `using CopyOutputRequestCallback = base::OnceCallback<void(std::unique_ptr<CopyOutputResult> result)>;`
  `components/viz/common/frame_sinks/copy_output_result.h`, `CopyOutputResult::Destination`, offers `kSystemMemory` and `kSharedImage` (not the stale `kNativeTextures` wording still present in a request-header comment).
  [V] `CopyOutputResult` accessors include `ScopedSkBitmap ScopedAccessSkBitmap() const;`, `virtual scoped_refptr<gpu::ClientSharedImage> GetSharedImage();`, and `virtual ReleaseCallback TakeSharedImageOwnership();` in that header.
- [V] There is **no `ui::Compositor::RequestCopyOfOutput` method at this pin**: `ui/compositor/compositor.h`, class `Compositor`, exposes the root layer and timing/keepalive APIs instead; `ui/compositor/layer.h`, class `Layer`, owns that request method.
- [V] Surface-specific privileged copy exists in `components/viz/host/host_frame_sink_manager.h`, `viz::HostFrameSinkManager`:
  `void RequestCopyOfOutput(const SurfaceId& surface_id, std::unique_ptr<CopyOutputRequest> request, bool capture_exact_surface_id = false, base::TimeDelta timeout = base::TimeDelta());`
  It accepts the specified surface or a newer LocalSurfaceId for the same FrameSinkId unless exact capture is requested.
  `ui/aura/window.cc`, `Window::GetSurfaceId`, combines `GetFrameSinkId()` and `GetLocalSurfaceId()`; `Window::GetFrameSinkId` returns the compositor's ID for an Aura root.
  [I] Use the root window's SurfaceId and account for resize/local-surface lifetime when choosing this entry point.
- [V] Streaming API: `components/viz/host/host_frame_sink_manager.h`, `HostFrameSinkManager::CreateVideoCapturer`, has these overloads:
  `void CreateVideoCapturer(mojo::PendingReceiver<mojom::FrameSinkVideoCapturer> receiver, uint32_t capture_version_source = 0);`
  `std::unique_ptr<ClientFrameSinkVideoCapturer> CreateVideoCapturer(uint32_t capture_version_source = 0);`
  `ui/compositor/compositor.h`, `Compositor::frame_sink_id`, exposes `const viz::FrameSinkId& frame_sink_id() const;` (inline definition in header).
  `components/viz/common/surfaces/video_capture_target.h`, `VideoCaptureTarget`, has `explicit VideoCaptureTarget(FrameSinkId frame_sink_id);`: omitting the sub-target means the sink's root render pass.
- [V] `components/viz/host/client_frame_sink_video_capturer.h`, `viz::ClientFrameSinkVideoCapturer`, public signatures:
  `void ChangeTarget(const std::optional<VideoCaptureTarget>& target);`
  `void ChangeTarget(const std::optional<VideoCaptureTarget>& target, uint32_t sub_capture_target_version);`
  `void SetFormat(media::VideoPixelFormat format);`
  `void SetMinCapturePeriod(base::TimeDelta min_capture_period);`
  `void SetResolutionConstraints(const gfx::Size& min_size, const gfx::Size& max_size, bool use_fixed_aspect_ratio);`
  `void SetAutoThrottlingEnabled(bool enabled);`
  `void Start(mojom::FrameSinkVideoConsumer* consumer, mojom::BufferFormatPreference buffer_format_preference);`
  `void RequestRefreshFrame();` / `void Stop();`
- [V] `components/viz/host/client_frame_sink_video_capturer.h`, `ClientFrameSinkVideoCapturer::OnFrameCaptured`, the consumer override signature is:
  `void OnFrameCaptured(media::mojom::VideoBufferHandlePtr data, media::mojom::VideoFrameInfoPtr info, const gfx::Rect& content_rect, mojo::PendingRemote<mojom::FrameSinkVideoConsumerFrameCallbacks> callbacks);`
  `services/viz/privileged/mojom/compositing/frame_sink_video_capture.mojom`, `FrameSinkVideoConsumerFrameCallbacks`, requires `Done()` to release a delivered frame; buffer preferences include `kDefault`, `kPreferMappableSharedImage`, `kPreferSharedImageWithNativeHandle`.
- [V] `components/viz/service/frame_sinks/frame_sink_manager_impl.cc`, `FrameSinkManagerImpl::FindCapturableFrameSink`, resolves an ordinary whole-sink target directly from `support_map_`; it does not reject root sinks.
  `components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.cc`, `FrameSinkVideoCapturerImpl::MaybeCaptureFrame`, issues a `CopyOutputRequest` against the resolved target, using `kSharedImage` or `kSystemMemory` according to its capture mode.
  [I] Use `VideoCaptureTarget(compositor->frame_sink_id())` for browser chrome, instead of the selected tab's FrameSinkId. This needs embedder integration but no replacement output-device patch.
- [V] Timing-only hooks in `ui/compositor/compositor.h`, `Compositor`:
  `void RequestPresentationTimeForNextFrame(PresentationTimeCallback callback);`
  `using PresentationTimeCallback = base::OnceCallback<void(const gfx::PresentationFeedback&)>;`
  `void RequestSuccessfulPresentationTimeForNextFrame(SuccessfulPresentationTimeCallback callback);`
  `using SuccessfulPresentationTimeCallback = base::OnceCallback<void(const viz::FrameTimingDetails& frame_timing_details)>;`
  [I] These report metadata, not image pixels; copies/capture observe composited render passes rather than a post-DWM screenshot of the HWND.
- [V] Windows DisplayClient hooks in `services/viz/privileged/mojom/compositing/display_private.mojom`, interface `DisplayClient`, are Mojo signatures:
  `CreateLayeredWindowUpdater(pending_receiver<LayeredWindowUpdater> receiver);`
  `AddChildWindowToBrowser(gpu.mojom.SurfaceHandle child_window);`
  [I] Neither is a GPU pixel callback, and the headless software factory bypasses LayeredWindowUpdater entirely (Q2).

- [V] `components/viz/service/frame_sinks/root_compositor_frame_sink_impl.cc`, `RootCompositorFrameSinkImpl` constructor, registers `CompositorFrameSinkSupport(..., /*is_root=*/true)`; combined with `FindCapturableFrameSink` above this verifies root-sink eligibility.
- [V] `components/viz/service/display/overlay_processor_win.cc`, `OverlayProcessorWin::TryDelegatedCompositing`, rejects root passes with `HasCapture()` as `kCompositedCopyRequest`.
  `ProcessOverlaysFromOutputSurfacePlane` verifies that a root copy does not require a write-only synchronous DComp surface.
  `components/viz/service/display/dc_layer_overlay.cc`, `DCLayerOverlayProcessor::CollectCandidates`, rejects optional overlays when the pass has capture; required overlays, including protected video, remain exceptions.
  [I] A root copy/capture can change presentation allocation/promotion and incur composition/copy costs. It provides the captured render-pass image, not a zero-copy handle to the exact DWM-presented tree; protected/overlay-only pixels cannot be promised.
- [V] `components/viz/common/frame_sinks/copy_output_request.h`, `CopyOutputRequest`, recommends the video capturer for streaming and specifies empty results on unsupported formats/errors.
  `components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.cc`, `Start` / `MaybeCaptureFrame`, uses a bounded frame pool, capture oracle and the configured buffer preference.
  [I] Requesting a native handle is a preference, not proof that every GPU/backend/software-fallback configuration can export the desired D3D texture.
- [V] `components/viz/service/display/software_renderer.cc`, `SoftwareRenderer::CopyDrawnRenderPass`, normally returns a system-memory `CopyOutputSkBitmapResult`; `kSharedImage` is supported only with a client-provided blit destination (also optimized by `ReadPixelsIntoBlitDestination`).
  `components/viz/service/display_embedder/skia_output_device_offscreen.cc`, `SkiaOutputDeviceOffscreen::ReadbackForTesting`, has signature `void ReadbackForTesting(base::OnceCallback<void(SkBitmap)> callback);` and enforces `CHECK_IS_TEST()`.
  [I] This testing hook is not a production embedder export API; the layer/FrameSink capture APIs above are the applicable existing hooks.

## 5. Estimated offscreen shared-texture patch scope

- [I] Estimate method: count the existing selection, allocation, synchronization, presentation-completion and IPC seams identified in Q1/Q2/Q4; count new `.h` / `.cc` as two files. These are design estimates, not a compiled patch or a port of the M158 prior art.
  Anchors: `components/viz/service/display_embedder/skia_output_surface_impl_on_gpu.cc`, `InitializeForGL` / `InitializeForDawn`; `output_surface_provider_impl.cc` in that directory, `CreateSoftwareOutputDeviceForPlatform`; `components/viz/service/display_embedder/skia_output_device.h`, `SkiaOutputDevice`.
- [V] Existing GPU starting point: `components/viz/service/display_embedder/skia_output_device_offscreen.{h,cc}`, `SkiaOutputDeviceOffscreen::Reshape` / `EnsureBackbuffer` / `BeginPaint` / `Present`, owns a Ganesh or Graphite backend texture and acknowledges frames without a presenter.
  [I] It is an offscreen allocation template, not a cross-process D3D shared-handle export protocol; replacing its texture allocation alone does not establish release/fence/backpressure safety.
- [I] GPU prototype, **approximately 5-8 files** when a process-wide opt-in is acceptable:
  `components/viz/service/display_embedder/skia_output_surface_impl_on_gpu.cc`, `Initialize*`: choose the new device before presenter creation for supported GL/Dawn contexts.
  New `components/viz/service/display_embedder/skia_output_device_shared_texture_win.{h,cc}`, proposed `SkiaOutputDeviceSharedTextureWin`: allocate/import renderable SharedImages with exportable D3D backing, manage resize generations and publish only completed frames.
  `components/viz/service/BUILD.gn`, display-embedder source list: compile the device.
  `components/viz/service/display_embedder/skia_output_surface_dependency_impl.{h,cc}`, proposed dependency option/accessor, or `output_surface_provider_impl.cc`, `CreateGpuDependency`: provide a consistent offscreen-mode decision while retaining the browser HWND for Views.
  Optional `components/viz/service/display_embedder/skia_output_surface_impl_on_gpu.h` / existing feature declaration: carry the selection state.
- [I] A robust browser-daemon GPU integration is more realistically **10-16 Chromium files total**:
  add a frame/resource/release transport seam in `services/viz/privileged/mojom/compositing/display_private.mojom`, `DisplayClient`, with browser-side handling in `components/viz/host/host_display_client.{h,cc}` or the local HostDisplayClient in `content/browser/compositor/viz_process_transport_factory.cc`.
  For per-root selection add a field to `services/viz/privileged/mojom/compositing/frame_sink_manager.mojom`, `RootCompositorFrameSinkParams`, and pass it through `components/viz/service/frame_sinks/root_compositor_frame_sink_impl.cc`, `Create`, and `components/viz/service/display_embedder/output_surface_provider{,_impl}.{h,cc}`, `CreateGpuDependency` / `CreateOutputSurface` as needed.
  Some listed alternatives overlap; the range assumes choosing one transport and one flag route, not modifying every alternative.
- [I] Software fallback to an offscreen CPU shared-memory device is **approximately 4-6 files**, excluding the common transport above:
  `components/viz/service/display_embedder/output_surface_provider_impl.cc`, `CreateSoftwareOutputDeviceForPlatform`, replaces the headless generic return with a purpose-built device.
  New `components/viz/service/display_embedder/software_output_device_shared_memory.{h,cc}`, proposed `SoftwareOutputDeviceSharedMemory`: render into leased mapped buffers and publish damage/size/sequence at `EndPaint` or `OnSwapBuffers`.
  `components/viz/service/BUILD.gn`, source list, plus optionally `components/viz/service/display/software_output_device.{h,cc}`, base device access/lifetime integration.
  [V] Base seams are `SoftwareOutputDevice::Resize`, `BeginPaint`, `EndPaint`, `OnSwapBuffers` in `components/viz/service/display/software_output_device.cc`; Q2's Windows HWND variants need not be changed.
- [I] If software must also deliver a D3D shared texture, allow **another 2-4 files** for upload/pool code and its build/configuration, plus the same cross-process release protocol. CPU rasterization still needs an upload and a working D3D device; it cannot provide GPU-free fallback if D3D itself is unavailable.
  Evidence: `components/viz/service/display/software_output_device.cc`, `SoftwareOutputDevice::Resize`, allocates raster memory; `components/viz/service/display_embedder/software_output_device_win_swapchain.cc`, `SoftwareOutputDeviceWinSwapChain::ResizeDelegated`, separately creates D3D/DXGI objects.
- [I] Required design constraints: advertise no DComp overlay capability for a single fully composed offscreen texture, preserve swap acknowledgements and presentation feedback, select a non-HWND begin-frame source, and handle context loss/resize/resource leases.
  Anchors: `components/viz/service/display_embedder/skia_output_device_dcomp.cc`, constructor capabilities; `components/viz/service/frame_sinks/root_compositor_frame_sink_impl.cc`, `Create`; `components/viz/service/display_embedder/skia_output_device_offscreen.cc`, `Present`.
  [I] The smallest first implementation can retain existing root FrameSink capture (Q4), avoiding replacement of the output device until measurements justify it.
- [I] The counts above exclude tests and the external daemon. Allow roughly 2-3 additional test files for device selection, resize/context loss and lease/backpressure, using `components/viz/service/display_embedder/skia_output_device_dcomp_unittest.cc` and `software_output_device_win_unittest.cc` as existing test-location precedents (their corresponding device classes are the verified anchors).
  A backend requiring new SharedImage import/export support can expand the range beyond these estimates; qualify an ANGLE/Dawn D3D11 implementation first before claiming Vulkan/D3D12 parity.
