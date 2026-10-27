# b3: Windows root compositor frame pacing

Pin: `1aa07b638799e59656559ae6c6649d4b197f555e` (157.0.8103.15).
Paths below are relative to `src/`. [V] means inspected at this pin; [I] means inference/proposed patch.

## 1. Root BeginFrameSource, Windows vsync, and cloaking

[V] The file is `components/viz/service/frame_sinks/root_compositor_frame_sink_impl.cc`,
`RootCompositorFrameSinkImpl::Create` (not `display_embedder/`). Selection order:
external controller -> `ExternalBeginFrameSourceMojo`; otherwise
`disable_frame_rate_limit` -> `BackToBackBeginFrameSource`; otherwise Windows with
`dc_support_level != kNone` -> `ExternalBeginFrameSourceWin`; otherwise
`DelayBasedBeginFrameSource` with `DelayBasedTimeSource`. The selected source is
passed to `DisplayScheduler` and registered against the root FrameSinkId.

[V] `components/viz/service/frame_sinks/external_begin_frame_source_win.cc`,
`ExternalBeginFrameSourceWin::OnNeedsBeginFrames` subscribes to singleton
`gl::VSyncThreadWin`; `OnVSyncOnSequence` generates args with frame_time=vsync_time,
deadline=vsync_time+interval. `SetVSyncDisplayID` is a TODO/no-op on Windows.
The pin's Windows root does not select a class named `GpuVSyncBeginFrameSource`.

[V] `ui/gl/vsync_thread_win.cc`, `VSyncThreadWin::GetInstance` selects
`VSyncThreadWinDXGI` normally; `ui/gl/gl_features.cc`,
`features::UseCompositorClockVSyncInterval` selects DComp only on Windows 11 24H2+
with `kUseCompositorClockVSyncInterval` (disabled by default) enabled.
`VSyncThreadWin::WaitForVSync` timestamps notifications with `TimeTicks::Now()`
after the wait, rather than using a per-window presentation callback as its clock.

[V] `ui/gl/vsync_thread_win_dxgi.cc`, `VSyncThreadWinDXGI::WaitForVSyncImpl`
finds the primary monitor's IDXGIOutput and calls `WaitForVBlank`;
`GetVsyncInterval` uses `gfx::VSyncProviderWin` (monitor interval on newer Windows,
composition timing parameters on older Windows). It refreshes stale DXGI factories.
[V] `ui/gl/vsync_thread_win_dcomp.cc`, `VSyncThreadWinDComp::WaitForVSyncImpl`
uses `DCompositionWaitForCompositorClock(0, nullptr, 100ms)` and its provider's
interval; failure returns a 100ms fallback interval.

[V] `ui/gl/dcomp_presenter.cc`, `DCompPresenter::OnVSync`,
`HandleVSyncOnMainThread`, `StartOrStopVSyncThread` use that same singleton for
pending presentation-feedback callbacks. The presenter is a parallel observer;
it is not the callback that drives `ExternalBeginFrameSourceWin`.

[V] `ui/views/win/hwnd_message_handler_headless.cc`,
`HWNDMessageHandlerHeadless::Init` sets `DWMWA_CLOAK=TRUE` on the retained HWND.
[I] Cloaking alone does not select another clock or stop the root source:
`ExternalBeginFrameSourceWin::OnNeedsBeginFrames` has no visibility/HWND test,
and `VSyncThreadWinDXGI::WaitForVSyncImpl` waits on the primary output, not that HWND.

[V] `ui/gl/vsync_thread_win.cc`, `VSyncThreadWin::WaitForVSync` falls back to
`Sleep(ceil(interval_ms))` when the hardware wait fails or returns within 1ms
(comments explicitly cover monitor sleep/unplug/desktop occlusion).
`PostTaskIfNeeded` stops scheduling without observers or during power suspend.
[I] No-monitor/RDP behavior and actual draw/capture liveness still need a runtime
test; this code supplies a fallback tick, not a guarantee that an invisible
Display or hidden renderer will draw (`DisplayScheduler::SetVisible`, Q4).

## 2. External BeginFrame control and Chrome headless reachability

[V] `ui/compositor/compositor.h`, `ui::Compositor::Compositor` accepts
`bool use_external_begin_frame_control = false` after `enable_pixel_canvas`.
The flag is immutable; `use_external_begin_frame_control()` exposes it.
There is no `GetExternalBeginFrameController` in this class at the pin:
use `IssueExternalBeginFrame(const viz::BeginFrameArgs&,
base::OnceCallback<void(const viz::BeginFrameAck&)>)` on Windows;
`SetExternalBeginFrameController(AssociatedRemote<...>)` is the factory hook.

[V] `content/browser/compositor/viz_process_transport_factory.cc`,
`VizProcessTransportFactory::OnEstablishedGpuChannel` wires the associated
controller receiver into RootCompositorFrameSinkParams when that flag is true,
optionally obtains a controller client from the compositor's client factory,
copies `wait_for_all_frame_sinks`, and installs the controller remote back into
the compositor after `SetLayerTreeFrameSink`.

[V] `services/viz/privileged/mojom/compositing/external_begin_frame_controller.mojom`,
`ExternalBeginFrameController::IssueExternalBeginFrame(BeginFrameArgs args)
=> (BeginFrameAck ack)` exists on non-iOS, including Windows.
`ExternalBeginFrameControllerClient` supplies `SetNeedsBeginFrame(bool)`,
`NeedsBeginFrameWithId(int64,bool)`, and `SetPreferredInterval(TimeDelta)`.
It is a browser/Viz connection; the endpoint needs a browser-side RPC bridge.

[V] `components/viz/service/frame_sinks/external_begin_frame_source_mojo.cc`,
`ExternalBeginFrameSourceMojo::IssueExternalBeginFrame` rejects overlapping
requests with `mojo::ReportBadMessage`, injects `OnBeginFrame(args)`, and calls
`Display::SetNeedsOneBeginFrame(args)` even if the Display did not need a frame.
`OnDisplayDidFinishFrame` acknowledges drawn versus undrawn work;
with `wait_for_all_frame_sinks`, it waits for observed sink completions too.
`DispatchFrameCallback` clears missed args and may discard unfulfilled copies.
[I] An ACK/has_damage is not an exported capture buffer or encoder completion;
serialize ticks and coordinate capture credits separately using these callbacks.

[V] `ExternalBeginFrameControllerClientImpl` is not used/defined in the inspected
`content/browser/compositor/viz_process_transport_factory.cc` factory path.
At this pin, `ui/compositor/external_begin_frame_adapter.{h,cc}`,
`ExternalBeginFrameAdapter` implements the client/factory (needs/preferred-interval
notifications); `DesktopWindowTreeHostPlatform::Init` in
`ui/views/widget/desktop_aura/desktop_window_tree_host_platform.cc` uses it when
the platform window supplies a BeginFrameSourceExtension. That host explicitly
sets `wait_for_all_frame_sinks=false` to avoid deadlocks as sinks join.
[V] `services/viz/privileged/mojom/compositing/frame_sink_manager.mojom`,
`RootCompositorFrameSinkParams` makes the controller client optional; old desktop
headless control therefore does not require a do-nothing client implementation.

[V] Old shell: `headless/lib/browser/command_line_handler.cc`,
`HandleCommandLineSwitches` handles `--enable-begin-frame-control` and rejects
combining it with explicit `--site-per-process`.
`headless/lib/browser/headless_web_contents_impl.cc`, `Create` derives the flag
from create params/browser options; `headless/lib/browser/headless_platform_delegate_aura.cc`,
`HeadlessPlatformDelegate::InitializeWebContents` constructs a
`HeadlessWindowTreeHost(flag)`; `headless/lib/browser/headless_window_tree_host.cc`,
its constructor calls `CreateCompositor(false, flag)`.

[V] `headless/lib/browser/protocol/headless_devtools_session.cc`,
`HeadlessDevToolsSession` installs `HeadlessHandler` for page targets;
`headless/lib/browser/protocol/headless_handler.cc`, `HeadlessHandler::BeginFrame`
implements `HeadlessExperimental.beginFrame`, requires control enabled AND
`--run-all-compositor-stages-before-draw`, and validates interval > 0.
`headless/lib/browser/headless_web_contents_impl.cc`, `HeadlessWebContentsImpl::BeginFrame`
rejects a pending frame, creates NORMAL args, optionally sets animate_only, then
calls `ui::Compositor::IssueExternalBeginFrame`. This is not content's handler.

[V] Chrome's `content/browser/devtools/protocol/target_handler.cc`,
`TargetHandler::CreateTarget` accepts `enable_begin_frame_control` but never uses
it in its body. `chrome/browser/headless/headless_mode_init.cc`,
`HeadlessModeHandleImpl::Initialize` does not wire a BeginFrame controller;
`ui/views/widget/desktop_aura/desktop_window_tree_host_win.cc`,
`DesktopWindowTreeHostWin::Init` calls `CreateCompositor(params.force_software_compositing)`.
[V] `chrome/browser/devtools/chrome_devtools_session.cc`,
`ChromeDevToolsSession::ChromeDevToolsSession` installs no HeadlessHandler.
[I] Therefore `--headless=new`/that CDP argument alone do not activate old shell
control for browser UI roots. A schema listing of HeadlessExperimental proves
neither its handler nor root control is connected.

[I] Minimal enabling change: in the above `DesktopWindowTreeHostWin::Init`, pass
an opt-in second `true` argument before `OnAcceleratedWidgetAvailable` (~3-8 lines).
`ui/aura/window_tree_host.cc`, `WindowTreeHost::CreateCompositor` already forwards
it to `ui::Compositor`; the transport factory already supplies all Viz plumbing.
Use a Views-level flag/property/switch, not a dependency from Views to Chrome.
[I] Add a root-owned pacing helper (~100-180 lines in `ui/compositor/` or the
Windows host's `.cc/.h`, plus ~10-25 GN/host hookup lines): timer/args generator,
one outstanding request, fixed/on-demand modes, teardown/recreation handling.
A browser endpoint-command adapter adds ~20-50 lines if window lookup/RPC already
exists; adding a general per-widget InitParams flag costs another ~10-25 lines
across `ui/views/widget/widget.h` and its caller. These are estimates, not a diff.

## 3. Runtime interval knobs and signatures

[V] `ui/gl/gl_features.cc`, `features::UseGpuVsync` observes `--disable-gpu-vsync`;
`ui/gl/dcomp_presenter.cc`, `DCompPresenter::EnqueuePendingFrame` then posts feedback
without waiting for the vsync thread. `RootCompositorFrameSinkImpl::Create` above
does NOT test that switch before selecting `ExternalBeginFrameSourceWin`.
[I] Thus this switch alone does not replace/bound the DComp root clock.

[V] `components/viz/common/switches.cc`, `kDisableFrameRateLimit` documents removal
of cc/display limiting and implied disable-gpu-vsync. The transport factory's
`OnEstablishedGpuChannel` copies it to root params; `RootCompositorFrameSinkImpl::Create`
then selects BackToBack (unless external control was supplied first).
[I] This is a launch-wide uncap, not an arbitrary per-root runtime interval.

[V] Additional existing launch knob: `ui/gl/gl_switches.cc`,
`switches::GetFakeVsyncIntervalFromCommandLine` caches positive `--fake-vsync-rate=Hz`;
`ui/gl/vsync_thread_win.cc`, `VSyncThreadWin::WaitForVSync` bypasses hardware wait
and sleeps at that interval. `gl_switches.cc`, `kGLSwitchesCopiedFromGpuProcessHost`
list membership forwards the switch (see the switch-copy array at the pin).
[I] Useful diagnostic for this task, but GPU-process-global, fixed at startup,
and rounded-up millisecond Sleep pacing; it cannot control one root at runtime.

[V] Actual APIs (`ui/compositor/compositor.h`, `ui::Compositor`):
```cpp
void SetDisplayVSyncParameters(base::TimeTicks timebase, base::TimeDelta interval);
void AddVSyncParameterObserver(
    mojo::PendingRemote<viz::mojom::VSyncParameterObserver> observer);
```
[V] `ui/compositor/compositor.cc`, `Compositor::SetDisplayVSyncParameters` caches
and sends params through DisplayPrivate; zero becomes DefaultInterval, disabled
frame-rate limiting makes it return. `AddVSyncParameterObserver` only forwards
if display_private_ exists. There is no `SetVSyncParameterObserver` here.

[V] `services/viz/privileged/mojom/compositing/display_private.mojom`,
`DisplayPrivate::SetDisplayVSyncParameters(TimeTicks timebase, TimeDelta interval)`
maps to `components/viz/service/frame_sinks/root_compositor_frame_sink_impl.cc`,
`RootCompositorFrameSinkImpl::SetDisplayVSyncParameters(base::TimeTicks,
base::TimeDelta)`. `SetVSyncDisplayID(int64_t display_id)` is **Mac-only** in
that interface/root implementation; `ui::Compositor::SetVSyncDisplayID` is also
Mac-only (`ui/compositor/compositor.{h,cc}`). It selects a display, not an interval.

[V] `RootCompositorFrameSinkImpl::UpdateVSyncParameters` chooses a nonzero
preferred interval over the supplied display interval. Synthetic source:
`components/viz/common/frame_sinks/begin_frame_source.cc`,
`DelayBasedBeginFrameSource::OnUpdateVSyncParameters(TimeTicks,TimeDelta)` calls
`DelayBasedTimeSource::SetTimebaseAndInterval`; this can run at arbitrary intervals.
External source: the root instead calls `SetPreferredInterval(TimeDelta)`.
[V] `components/viz/service/frame_sinks/external_begin_frame_source_win.cc`,
`ExternalBeginFrameSourceWin::SetPreferredInterval` implements only this decision:
```cpp
auto interval_for_half_refresh_rate = vsync_interval_ * 2;
constexpr auto kMaxDelta = base::Milliseconds(0.5);
bool run_at_half_refresh_rate =
    interval > (interval_for_half_refresh_rate - kMaxDelta);
```
`OnVSyncOnSequence` skips alternate ticks and doubles args.interval in that mode.
[I] A request for 30Hz can yield 30Hz on a 60Hz clock, but not 30Hz on 144Hz;
40Hz/20Hz requests cannot implement arbitrary divisors either.

[V] At the pin, `components/viz/service/display/display.{h,cc}`, `viz::Display`
has neither `SetSupportedFrameIntervals` nor `SetPreferredFrameInterval`.
`components/viz/service/display/frame_interval_decider.h`, `FrameIntervalDecider`
explicitly replaces FrameRateDecider and exposes `UpdateSettings(Settings settings,
std::vector<std::unique_ptr<FrameIntervalMatcher>> matchers)`.
`RootCompositorFrameSinkImpl::UpdateFrameIntervalDeciderSettings` installs matchers;
`FrameIntervalDeciderResultCallback` calls the root's private
`void SetPreferredFrameInterval(base::TimeDelta interval)`, not a browser API.
`GetSupportedFrameIntervals` defaults through `BeginFrameSource::GetDefaultSupportedFrameIntervals`
(`components/viz/common/frame_sinks/begin_frame_source.cc`) to `{interval, 2*interval}`.
[I] Supported intervals and content votes are policy, not a fixed-rate override.

[V] `RootCompositorFrameSinkImpl::Create` binds output-surface vsync updates;
`components/viz/service/display_embedder/skia_output_surface_impl.cc`,
`SkiaOutputSurfaceImpl::BufferPresented` feeds presentation parameters back to it.
`RootCompositorFrameSinkImpl::AddVSyncParameterObserver` stores a listener;
`UpdateVSyncParameters` notifies that listener only on the synthetic-source branch.
[I] **No listed setter reliably supplies an arbitrary interval to the default
Windows DComp root.** `SetDisplayVSyncParameters` works for a controlled timer
fallback, subject to hardware callbacks/preferred-interval policy overwriting it.
For an arbitrary per-root clock, select external control and issue args at the
desired cadence; a locked synthetic override would require additional Viz policy.

## 4. Renderer and video propagation

[V] `components/viz/service/frame_sinks/root_compositor_frame_sink_impl.cc`,
`RootCompositorFrameSinkImpl` constructor registers the root source and gives
the same source to its Display scheduler. `components/viz/service/frame_sinks/frame_sink_manager_impl.cc`,
`FrameSinkManagerImpl::RegisterBeginFrameSource`, `RegisterFrameSinkHierarchy`,
`RecursivelyAttachBeginFrameSource` propagate it down registered child sinks,
calling `CompositorFrameSinkSupport::SetBeginFrameSource` for source-less mappings.

[V] `content/browser/renderer_host/delegated_frame_host.cc`,
`DelegatedFrameHost::AttachToCompositor` calls `ui::Compositor::AddChildFrameSink`
when it owns hierarchy registration; `ui/compositor/compositor.cc`, that function
registers root -> content. `content/browser/renderer_host/render_widget_host_view_child_frame.cc`,
`RenderWidgetHostViewChildFrame::SetParentFrameSinkId` registers parent -> OOPIF.
[I] Normal attached Windows content/OOPIF sinks therefore inherit the root tick.
Another root/popup or a sink already supplied with another source needs its own
pacing association; controlling one root is not automatically instance-wide.

[V] `components/viz/service/frame_sinks/compositor_frame_sink_support.cc`,
`CompositorFrameSinkSupport::OnBeginFrame` sends adjusted args to the renderer,
with possible per-sink throttling. `cc/mojo_embedder/async_layer_tree_frame_sink.cc`,
`AsyncLayerTreeFrameSink::OnBeginFrame` forwards to its external source;
`BindToClient` uses a supplied synthetic source instead if one was configured.
`cc/scheduler/scheduler.cc`, `Scheduler::OnBeginFrameDerivedImpl` drives scheduled
actions, including `ScheduledActionSendBeginMainFrame` in `ProcessScheduledActions`.
[V] `third_party/blink/renderer/core/frame/web_frame_widget_impl.cc`,
`WebFrameWidgetImpl::BeginMainFrame` passes args.frame_time to `Page::Animate`
(`third_party/blink/renderer/core/page/page.cc`), then
`PageAnimator::ServiceScriptedAnimations` (`third_party/blink/renderer/core/page/page_animator.cc`)
executes `ScriptedAnimationController::ExecuteFrameCallbacks` for active controllers.
[I] This bounds normal compositor animation/rAF opportunities, not JS timers or
every possible unsolicited commit; opt-out/test synthetic sources require checking.

[V] `third_party/blink/renderer/platform/graphics/video_frame_submitter.cc`,
`VideoFrameSubmitter::StartSubmitting` creates an EmbeddedFrameSinkProvider sink
(optionally bundled); `StartRendering`/`UpdateSubmissionState` request BeginFrames.
`OnBeginFrame` selects video for the args' deadline window with
`VideoFrameProvider::UpdateCurrentFrame`, submits with its BeginFrameAck, or NAKs.
`content/browser/renderer_host/embedded_frame_sink_impl.cc`,
`EmbeddedFrameSinkImpl::RegisterFrameSinkHierarchy` registers it under its parent.
[I] It is a separate sink/thread, not an independent display-vsync source;
ordinary video sampling/submission follows the inherited root cadence.

[V] Same `VideoFrameSubmitter` file: `DidReceiveFrame` calls `SubmitSingleFrame`,
which uses `BeginFrameAck::CreateManualAckWithDamage`; `UpdateSubmissionState`
can also submit a frame immediately. `ShouldSubmit` tests visibility/force_submit.
[I] Root pacing therefore does **not** cap all video submissions or stop decode,
media time, audio, or live delivery. Sparse ticks can drop/repeat video and cause
animation jumps; use monotonic real frame times and plausible positive intervals.

[V] `components/viz/service/display/display_scheduler.cc`,
`DisplayScheduler::ShouldDraw` additionally requires visible, damage, a root frame,
and a valid output surface. `ui/aura/window_tree_host.cc`,
`WindowTreeHost::UpdateCompositorVisibility` changes compositor visibility;
`CreateVideoCaptureLock` participates in occlusion capture accounting.
[I] BeginFrame injection does not override these prerequisites or hidden-page
policy; verify capture liveness, OOPIF joins, video and UI together under cloak.

## 5. Smallest recommended patch (at most 15 lines)

[I] 1. Opt managed roots into external control at `DesktopWindowTreeHostWin::Init`
(`ui/views/widget/desktop_aura/desktop_window_tree_host_win.cc`); retain existing Viz plumbing (Q2).
[I] 2. Add one per-root UI-thread pacer calling `ui::Compositor::IssueExternalBeginFrame`
(`ui/compositor/compositor.cc`): configurable 1/30s or 1/60s timer; on-demand mode uses the same call.
[I] 3. Browser RPC maps daemon window IDs to this pacer; create monotonic NORMAL args locally,
unique source IDs/increasing sequences, positive intervals/deadlines; coalesce ticks while an ACK is pending.
[I] 4. Prefer `set_wait_for_all_frame_sinks(false)` for a live browser, following
`DesktopWindowTreeHostPlatform::Init` (Q2); ACK is not a barrier guaranteeing every child is fresh.
[I] 5. A client factory is optional; add one only for idle/needs/preferred notifications
(`RootCompositorFrameSinkParams`, Q2); don't let content preferences override the configured ceiling.
[I] 6. Pace each popup root too; keep capture visibility/credits independent and handle sink recreation.
[I] 7. Risks: OOPIF stale frames, late ACKs, video manual submissions, media/rAF jumps under long stalls
(Q2/Q4 code paths); validate before shipping. No renderer patch or OS-input synthesis is needed for this design.
