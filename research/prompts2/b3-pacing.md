# Task b3: BeginFrame / frame-pace control for the Windows root compositor (verification task)
Output file: C:\Users\Flysoft\Desktop\remotekit\research\b3-pacing.md   Time limit: 25 min.
Goal: the endpoint daemon (another process) wants to drive or bound the frame pace of the whole browser window
(e.g. "produce a frame now" when the encoder is free, or a fixed 30/60 Hz synthetic vsync), without breaking rAF/video.
Questions, at the pin:
1. Which BeginFrameSource drives the root Display of a Windows browser window (components/viz/service/display_embedder/
   root_compositor_frame_sink_impl.cc: GpuVSyncBeginFrameSource / DelayBasedBeginFrameSource / ExternalBeginFrameSource
   ...). Where does the vsync timebase come from on Windows (VSyncThreadWin / DXGI WaitForVBlank / DCompPresenter
   vsync callback)? What happens for a cloaked window (no visible output)?
2. External BeginFrame control: ui::Compositor constructor flag (e.g. `use_external_begin_frame_control`),
   ui::Compositor::GetExternalBeginFrameController / viz::mojom::ExternalBeginFrameController,
   ExternalBeginFrameSourceMojo, ExternalBeginFrameControllerClientImpl. Who uses it today (old headless_shell
   `--enable-begin-frame-control`, HeadlessExperimental.beginFrame in content/browser/devtools or headless/lib)?
   Is it reachable from Chrome `--headless=new` (chrome/browser/headless, chrome/browser/ui/views)? What would it take
   (files, approx lines) to enable it for Chrome browser windows created by DesktopWindowTreeHostWin?
3. Simpler knobs: `--disable-gpu-vsync`, `--disable-frame-rate-limit`, viz Display::SetSupportedFrameIntervals /
   SetPreferredFrameInterval / FrameRateDecider, `RootCompositorFrameSink::SetVSyncDisplayID/ SetDisplayVSyncParameters`,
   `ui::Compositor::SetDisplayVSyncParameters`, `ui::Compositor::SetVSyncParameterObserver`. Which of these lets a
   browser-side patch set an arbitrary interval (e.g. 1/30 s) for one root compositor at runtime? Give signatures.
4. Interaction with renderers: do renderer CompositorFrameSinks (web content, OOPIF) follow the root display's
   BeginFrameSource on Windows (via FrameSinkManager hierarchy), so throttling the root throttles rAF and video
   frame submission? Any separate source for video (VideoFrameSubmitter)?
5. Recommendation in <= 15 lines: the smallest patch giving the endpoint (a) a configurable fixed frame interval and
   (b) optional "BeginFrame on demand", with risks.
