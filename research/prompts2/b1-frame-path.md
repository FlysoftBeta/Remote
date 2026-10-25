# Task b1: Windows new-headless root compositor output path (verification task)
Output file: C:\Users\Flysoft\Desktop\remotekit\research\b1-frame-path.md   Time limit: 25 min.
Questions, all at the pin, for the Chrome browser window (BrowserView -> DesktopWindowTreeHostWin with
HWNDMessageHandlerHeadless):
1. Which viz output device is created for that root compositor's Display on Windows with GPU on: trace from
   ui::Compositor / content's VizProcessTransportFactory (or equivalent) -> viz::OutputSurfaceProvider(Impl)::CreateOutputSurface
   -> SkiaOutputSurfaceImpl(OnGpu) -> which SkiaOutputDevice (DComp / swap chain / GL surface / ...) and which gl presenter
   (gl::DCompPresenter, gl::DirectCompositionChildSurfaceWin, SwapChainPresenter ...). Name the selection conditions.
2. Software compositing path: which SoftwareOutputDevice is used on Windows (software_output_device_win.cc variants,
   LayeredWindowUpdater, ...) and does headless change anything.
3. Does anything in headless mode stop or throttle frame production for a cloaked HWND? Check:
   NativeWindowOcclusionTrackerWin / aura::NativeWindowOcclusionTracker (is it disabled in headless? where?),
   ui::Compositor::SetVisible / WindowTreeHost::Show/Hide in HWNDMessageHandlerHeadless, DWM cloak effects on
   DComp/swapchain Present, any `headless` checks in ui/compositor, components/viz, ui/aura, ui/views/widget/desktop_aura.
4. Are there any existing hooks where an embedder can get the presented root image without a new patch
   (e.g. CopyOutputRequest on the root layer, ui::Compositor::RequestCopyOfOutput, viz video capture of the root
   FrameSinkId, DisplayClient callbacks)? List them with exact signatures.
5. Estimate (and say how you estimated) what a patch replacing the root OutputDevice by an offscreen shared-texture
   device would touch (files) for GPU and for software paths.
