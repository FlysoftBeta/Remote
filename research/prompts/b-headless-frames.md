# Task b: new headless on Windows, compositor output path, frame export, BeginFrame pacing
Write: C:\Users\Flysoft\Desktop\remotekit\research\b-headless-frames.md
First skim the prior-art findings: C:\Users\Flysoft\Desktop\remotekit\research\a-prior-art.md (if it exists) and the prior
art's docs/prts/ + patches 0010, 0011, 0015, 0019, 0021, 0026 (frame capture). Do not repeat what it already established;
verify it against the current stable source and fill gaps.

Questions (answer with file paths / classes / functions at the pinned stable version):
1. How `chrome --headless` (new headless, chrome/browser/headless/*, headless/lib/browser/*) creates top-level windows on
   Windows: which Widget/DesktopWindowTreeHostWin/HWNDMessageHandler path, where DWMWA_CLOAK / cloaking is applied
   (search "DWMWA_CLOAK", "headless" in ui/views/win, ui/views/widget/desktop_aura, ui/base/win), how window bounds/DPI/
   display (ScreenWin / headless screen) are handled, and what differs from headful. Also how the headless *shell*
   (headless_shell) differs, and why we must use the chrome path (browser UI chrome).
2. The frame path from aura::WindowTreeHost -> ui::Compositor -> viz (root CompositorFrameSink) -> Display ->
   SkiaOutputSurface(Impl) / SkiaOutputDevice for GPU (which device on Windows: SkiaOutputDeviceDComp / ...Gl / swap chain,
   DirectComposition presenter, gl::DCompPresenter) and SoftwareOutputDevice(Win) for software. When the window is cloaked,
   does viz still present every frame? Is presentation throttled (occlusion tracking, NativeWindowOcclusionTrackerWin,
   visibility -> compositor stops)? How did prior art keep frames flowing?
3. Options to export a whole-window frame (including tab strip/omnibox) as a shared D3D11 texture to another process
   (NT handle via IDXGIResource1::CreateSharedHandle + IDXGIKeyedMutex, or a fence):
   (A) FrameSinkVideoCapturer on the root frame sink (viz::FrameSinkVideoCapturerImpl, CopyOutputRequest; pixel formats;
       NV12/ARGB; GpuMemoryBuffer / shared image output "kGpuMemoryBuffer" buffer format; latency added; how prior art
       used it) ;
   (B) a custom viz OutputSurface / SkiaOutputDevice that renders into an offscreen shared texture
       (like CEF's OSR "shared texture" feature: viz::... "external begin frame" + "OnAcceleratedPaint" with a shared handle;
       find the CEF approach: cef/libcef/browser/osr/*, its patches to components/viz/service/display_embedder like
       software_output_device_proxy / "LayeredWindowUpdater", and the newer shared-texture OSR in CEF 12x);
   (C) intercepting the DComp / swap-chain present (gl::DCompPresenter / SwapChainPresenter) and copying the back buffer;
   (D) SoftwareOutputDevice proxy + LayeredWindowUpdater (software path, used by headless/OSR).
   For each: patch size and layer, GPU and software coverage, latency, risks, multi-window/popup handling
   (menus, select popups, omnibox dropdown are separate windows/widgets!).
4. BeginFrame / pacing control: headless BeginFrameControl (--enable-begin-frame-control, HeadlessExperimental
   .beginFrame, viz::ExternalBeginFrameSource / ExternalBeginFrameSourceMojo), ui::Compositor external begin frames
   (ui::ExternalBeginFrameClient? check current API), vsync providers on Windows (GpuVSyncProvider/
   VSyncThreadWin, DelayBasedBeginFrameSource), and how an external process (the endpoint) could drive frame pace:
   e.g. a Mojo/pipe "BeginFrame now" message, or an adjustable synthetic vsync interval. Note interaction with
   video playback and rAF.
5. Recommendation: the frame-export design (primary GPU path + software fallback) with concrete files to patch, an estimate of
   patch size, how popups are composed, and the pacing design. List spikes to verify early.
