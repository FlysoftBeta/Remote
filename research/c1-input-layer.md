# c1 — input injection and activation in Windows new headless

Pin: `1aa07b638799e59656559ae6c6649d4b197f555e` (157.0.8103.15).
Evidence: [V] read at this pin; [I] inference from the cited code. Paths are relative to `src/`.
Source verification only; no browser build or runtime qualification.

## 1. Sink → Aura → Views/content, and coordinates

[V] Entry: `ui/events/event_source.cc`, `ui::EventSource::SendEventToSink` is inherited by
`aura::WindowTreeHost`; it runs registered event rewriters, then `DeliverEventToSink`.
`ui/aura/window_tree_host.cc`, `WindowTreeHost::GetEventSink` returns its `WindowEventDispatcher`.
Calling `GetEventSink()->OnEventFromSource` directly bypasses those rewriters.

[V] Dispatch: `ui/events/event_processor.cc`, `EventProcessor::OnEventFromSource` invokes
`OnEventProcessingStarted`, asks the root's targeter for a target, then dispatches.
`ui/aura/window_targeter.cc`, `WindowTargeter::{FindTargetForKeyEvent,GetPriorityTargetInRootWindow}`
uses the root's FocusClient for keys; mouse pressed-handler/capture and touch-locked target
take priority for located events. `GetNewEventSinkForEvent` can redirect across Aura roots.

[V] Views: `ui/views/widget/desktop_aura/desktop_native_widget_aura.cc`,
`DesktopNativeWidgetAura::{OnMouseEvent,OnKeyEvent,OnGestureEvent}` forwards to its Widget delegate.
`ui/views/widget/widget.cc`, `Widget::{OnMouseEvent,OnKeyEvent}` dispatches through RootView
and FocusManager; `ui/views/widget/root_view.cc`, `RootView::OnMousePressed` hit-tests Views.
[V] `ui/aura/window.cc`, `Window::Window` installs its WindowDelegate as the event target handler;
`DesktopNativeWidgetAura` creates `content_window_(new aura::Window(this))` in its constructor.
[I] Therefore tabstrip, omnibox and Views popup controls are reachable through the host sink;
target the correct popup host/root, with visible/logically active Widgets and appropriate focus.

[V] Content: `content/browser/renderer_host/render_widget_host_view_aura.cc`,
`RenderWidgetHostViewAura::{OnMouseEvent,OnKeyEvent,OnTouchEvent,OnGestureEvent}` delegates to
`content/browser/renderer_host/render_widget_host_view_event_handler.cc`,
`RenderWidgetHostViewEventHandler` methods of the same names. Mouse/wheel/touch/gesture use
`host_->GetInputEventRouter()->Route*` when `ShouldRouteEvents()` is true; page popups forward directly.
[V] The content delegate is installed by `RenderWidgetHostViewAura::CreateAuraWindow`, which creates
`aura::Window(this)` in `content/browser/renderer_host/render_widget_host_view_aura.cc`.
[V] At this pin the router is **`input::RenderWidgetHostInputEventRouter`**, in
`components/input/render_widget_host_input_event_router.cc`, not the older content namespace/path.
Its `RouteMouseEvent/RouteMouseWheelEvent/RouteTouchEvent` use `RenderWidgetTargeter`;
`FindViewAtLocation` consults Viz hit-test data and transforms coordinates into the selected view.
[I] This preserves OOPIF hit testing instead of assuming the main frame is the destination.

[V] Keys do **not** use the located-event router: `content/browser/renderer_host/render_widget_host_view_aura.cc`,
`RenderWidgetHostViewAura::ForwardKeyboardEventWithLatencyInfo` asks the host delegate's
`GetFocusedRenderWidgetHost` for the destination (explicitly including OOPIF), then forwards there.

[V] Coordinates at the sink are **host coordinates, normally client device pixels**:
`ui/aura/window_event_dispatcher.cc`, `WindowEventDispatcher::OnEventProcessingStarted`
calls `TransformEventForDeviceScaleFactor`, applying both inverse root transforms.
`ui/aura/window_tree_host.cc`, `WindowTreeHost::GetRootTransform` combines DSF and root-layer transform.
`ui/aura/event_injector.cc`, `EventInjector::Inject` applies the forward transforms to a DIP event
before calling the sink, providing a pinned example of the required inverse pair.
[I] Keep remote coordinates in root DIP, set both location/root_location consistently, then apply
the selected host's forward root transforms once; do not inject raw DIP at DSF != 1.
Simple DIP→pixel multiplication is sufficient only when the root has no additional transform.

## 2. Windows message processing bypassed, and required emulation

[V] Native mouse path: `ui/views/win/hwnd_message_handler.cc`,
`HWNDMessageHandler::HandleMouseEventInternal` builds `ui::MouseEvent(CHROME_MSG)`, handles
client/nonclient distinctions, touch-promoted mouse filtering, wheel rerouting and `TrackMouseEvents`.
`ui/views/widget/desktop_aura/desktop_window_tree_host_win.cc`,
`DesktopWindowTreeHostWin::HandleMouseEvent` then calls the same `SendEventToSink`.
[I] Supply event type, host coordinates, timestamp, modifiers, held-button flags, changed-button
flags and click count yourself; use dragged moves while buttons are held and correct wheel offsets.
[V] `ui/aura/env_input_state_controller.cc`, `EnvInputStateController::UpdateStateForMouseEvent`
sets pressed-button state from event flags and removes `changed_button_flags()` on release.
Synthesized non-native mouse events do not update last mouse location in this function.
[I] Preserve remote hover/position explicitly if marking mouse events `EF_IS_SYNTHESIZED`.
[V] `ui/display/win/screen_win_headless.cc`, `ScreenWinHeadless::GetCursorScreenPoint` returns a
stored virtual point, independently of Env; `SetCursorScreenPointForTesting` sets it.
[I] Keep that virtual screen point synchronized too; sink mouse dispatch does not call its setter.
[V] **Additional Windows trap:** `ui/aura/input_state_lookup_win.cc`,
`InputStateLookupWin::IsMouseButtonDown/IsLeftMouseButtonDown` reads `::GetKeyState`;
`ui/aura/env.cc`, `Env::IsMouseButtonDown` prefers that lookup over event-maintained flags.
`ui/views/widget/widget.cc`, `Widget::OnMouseEvent` checks this before acquiring Views capture.
[I] Virtualize this lookup for remote mode; event flags alone do not make ordinary Views drag/capture
behave as if the server's physical mouse button were down.

[V] `ui/aura/window_event_dispatcher.cc`, `WindowEventDispatcher::PreDispatchMouseEvent`
generates enter/exit when moving between targets; `OnOtherRootGotCapture` deliberately omits
Windows mouse-exit synthesis because native Windows normally supplies `WM_MOUSELEAVE`.
[I] Supply exits when leaving a root/viewer or switching popup roots; no OS TrackMouseEvent
will do that for injected input. Aura already maintains within-root targeting and hover transitions.
[V] `ui/views/widget/desktop_aura/desktop_capture_client.cc`, `DesktopCaptureClient::SetCapture`
maintains logical capture even though native headless capture is a no-op.
`ui/aura/client/capture_client.cc`, `GetCaptureWindow` queries the supplied root's local client.
[I] Route to `DesktopCaptureClient::GetCaptureWindowGlobal()`'s host when appropriate; ordinary
OS capture delivery to the right HWND is absent. Also choose separate popup roots in virtual z order.
[V] `ui/views/widget/desktop_aura/desktop_native_widget_aura.cc`, `DesktopNativeWidgetAura::HasCapture` requires both Aura
capture and `DesktopWindowTreeHostWin::HasCapture`, which delegates to headless `HasCapture()==false`.
`ui/views/widget/widget.cc`, `Widget::OnMouseEvent` selects `RootView::OnMouseDragged` only when HasCapture is true.
[I] Remote mode needs a logical capture query as well as logical capture storage/routing; otherwise
normal Views drag handling remains broken even after virtualizing the button-state lookup.

[V] Native keys: `ui/views/win/hwnd_message_handler.cc`, `HWNDMessageHandler::OnKeyEvent`
builds `ui::KeyEvent(CHROME_MSG)` from WM_KEY*/WM_SYSKEY*; `OnImeMessages` delegates WM_CHAR,
WM_SYSCHAR and IME messages (`ui/views/win/hwnd_message_handler.h`, message map) to `HandleIMEMessage`.
[V] `ui/aura/window_event_dispatcher.cc`, `WindowEventDispatcher::PreDispatchKeyEvent` skips
InputMethod for `EF_IS_SYNTHESIZED`; otherwise a host InputMethod can still intercept sink keys.
[I] Define explicit keydown/up, repeat, virtual key, DOM code/key and modifier semantics; maintain
held state and send releases on disconnect. A synthesized keydown alone does not commit Unicode.
[V] `ui/views/widget/desktop_aura/desktop_native_widget_aura.cc`, `DesktopNativeWidgetAura::OnKeyEvent` ignores char events;
`content/browser/renderer_host/render_widget_host_view_aura.cc`, `RenderWidgetHostViewAura::OnKeyEvent` identifies `InsertChar`
as the ordinary content character-forwarding step.
[I] Send committed text through the focused host InputMethod's TextInputClient (`InsertChar` or
`InsertText` as appropriate); full remote IME additionally needs composition/selection/caret handling.

[V] Native touch: `ui/views/win/hwnd_message_handler.cc`, `HWNDMessageHandler::OnPointerEvent` selects
PT_TOUCH/PT_PEN handlers; `HandlePointerEventTypeTouchOrNonClient` reads pointer info, maps IDs,
normalizes contact radii/pressure, timestamps and delivers a `ui::TouchEvent` to the host delegate.
`DesktopWindowTreeHostWin::HandleTouchEvent` also implements native touch-drag cursor sync/rerouting.
[I] Supply those logical touch fields directly (Q4); native WM_POINTER/WM_TOUCH bookkeeping and
Windows compatibility mouse generation are unnecessary for sink injection.
[I] Virtualize cursor export/visibility, logical activation/focus, capture and reset/cancellation;
define supported behavior for native frame moves, system menus and OLE drag rather than expecting
the omitted WM_NCHITTEST/DefWindowProc/native modal loops to implement remote UI behavior
(`ui/views/win/hwnd_message_handler.cc`, `HandleMouseInputForCaption`; `ui/views/widget/desktop_aura/desktop_drag_drop_client_win.cc`,
`DesktopDragDropClientWin::StartDragAndDrop`).

## 3. Headless activation/focus and desktop side effects

[V] `ui/views/win/hwnd_message_handler_headless.cc`, `HWNDMessageHandlerHeadless::Init`
creates a real HWND via `WindowImpl::Init`, then sets `DWMWA_CLOAK` and disables DWM transitions.
`Show` tracks `is_visible_`, notifies the delegate, and ordinarily calls logical `Activate`;
inactive/minimized show states suppress activation. `Hide` only changes visibility bookkeeping.
[V] In the same file/class, `Activate` sets `is_active_` and calls `HandleActivationChanged(true)`
only if inactive, activatable, and top-level; `Deactivate` clears it and notifies false.
`IsActive` returns that local flag. It does not implement a desktop-wide active-window election.
[V] There is no headless `SetFocus` method: `ClearNativeFocus` pretends native focus through
`delegate_->HandleNativeFocus(0)`. `SetCapture/ReleaseCapture` are no-ops; `HasCapture` returns false.
`ui/views/win/hwnd_message_handler.cc`, `HWNDMessageHandler::SetInitialFocus` explicitly avoids
`::SetFocus` when `IsHeadless()`; `OnKillFocus` suppresses headless native-blur notifications.

[V] `ui/views/widget/desktop_aura/desktop_native_widget_aura.cc`,
`DesktopNativeWidgetAura::InitNativeWidget` installs one `wm::FocusController(DesktopFocusRules)`
as both `aura::client::FocusClient` and **`wm::ActivationClient`** on the host root, and as a pretarget handler.
The pin uses `wm::ActivationClient`, rather than `aura::client::ActivationClient`.
`HandleActivationChanged` activates/deactivates Aura windows, restores/stores Views focus and calls
`InputMethod::OnFocus/OnBlur`; `OnWindowFocused` reports native-widget focus/blur to the Widget.
[V] `ui/aura/window_targeter.cc`, `WindowTargeter::FindTargetForKeyEvent` selects the focused Aura
window. `ui/views/widget/widget.cc`, `Widget::OnKeyEvent` and its FocusManager select Views handling.
[V] `ui/views/view_targeter.cc`, `ViewTargeter::FindTargetForKeyEvent` uses FocusManager's focused
View; `ui/views/focus/focus_manager.cc`, `FocusManager::OnKeyEvent/ProcessAccelerator` handles
accelerators. `ui/views/widget/focus_manager_event_handler.cc`, `FocusManagerEventHandler::OnKeyEvent`
also offers root pretarget keys to that manager, including keys destined for the embedded Aura content.
[I] Remote activation must preserve both the selected host's logical activation and its Aura/View
focus; a key event's coordinates cannot select the omnibox or a web editor.

[V] Native-call audit of the **stand-in host chain**: `ui/views/widget/desktop_aura/desktop_window_tree_host_win.cc`,
`DesktopWindowTreeHostWin::{Activate,Deactivate,ClearNativeFocus,SetCapture,Show}` delegates to
the virtual HWND handler. The `::SetForegroundWindow` calls in base `HWNDMessageHandler::Activate/
Deactivate`, the `::ShowWindow` calls in base `Show/Activate`, and the base `ClearNativeFocus`/
`SetCapture` calls are bypassed by the headless overrides. `SetInitialFocus` has its own headless guard.
[V] No production `::SetActiveWindow` call exists in the inspected Views desktop-Aura/Win host
implementations; `ui/wm/core/focus_controller.cc`, `FocusController::SetActiveWindow` is an Aura
method, not the Win32 function. Calls in desktop-window-host unit tests do not establish a product path.
[I] This establishes the normal headless host's logical behavior, not absence of all native UI APIs
throughout Chrome or third-party code.

[V] A real **child HWND** can still receive `::ShowWindow`: `content/browser/renderer_host/legacy_render_widget_host_win.cc`, `LegacyRenderWidgetHostHWND::{Create,Show,Hide}` has no
headless test; creation can be disabled with `switches::kDisableLegacyIntermediateWindow`
(`content/public/common/content_switches.cc`, value `disable-legacy-window`).
`content/browser/renderer_host/render_widget_host_view_aura.cc`, `RenderWidgetHostViewAura::UpdateLegacyWin` creates/shows it
when a host HWND exists and the renderer is not hidden. This is distinct from showing the stand-in.

[V] Headless does not override all cursor effects: `ui/views/widget/desktop_aura/desktop_window_tree_host_win.cc`,
`DesktopWindowTreeHostWin::SetCursorNative` calls inherited `HWNDMessageHandler::SetCursor`
(`ui/views/win/hwnd_message_handler.cc`), which calls `::SetCursor` without a headless guard.
`MoveCursorToScreenLocationInPixels` calls `::SetCursorPos`; `OnCursorVisibilityChangedNative`
can call `::ShowCursor` depending on kHideCursorWhileTyping. `UpdateMouseLockRegion` calls `::ClipCursor`.
[V] `DesktopWindowTreeHostWin::CaptureSystemKeyEventsImpl` creates a Windows keyboard hook;
`StartTouchDrag/FinishTouchDrag` use `ui::SendMouseEvent`, and
`ui/views/widget/desktop_aura/desktop_drag_drop_client_win.cc`, `DesktopDragDropClientWin::StartDragAndDrop` calls `::DoDragDrop`.
[I] Guard/replace these optional paths for the product; sink injection alone does not suppress
native effects subsequently requested by Views/content.

[V] Process-wide logical state: `ui/views/widget/desktop_aura/desktop_capture_client.cc`,
`DesktopCaptureClient::GetCaptureWindowGlobal` uses file-static `active_client_`;
`ui/views/widget/desktop_aura/desktop_native_widget_aura.cc`, `DesktopNativeWidgetAura::InitNativeWidget` shares static cursor
managers across its roots. `ui/aura/env.cc`, `Env` owns mouse/touch state and the gesture recognizer.
`ui/views/widget/widget_activation_delegate.cc`, `WidgetActivationDelegate::Get` is a singleton
extension intended for test activation; normal host behavior above assumes it is unset.
[V] `ui/views/widget/desktop_aura/desktop_native_cursor_manager_win.cc`, `DesktopNativeCursorManagerWin::InitSystemCursorObservers`
can install WinEvent hooks for system cursor visibility/desktop switches, receiving external process events.
[I] FocusControllers/FocusManagers and these statics live in each browser process, so they do not
elect one active browser across processes. Multiple roots within one process share capture/cursor/
touch state; several processes on one desktop still observe the same native keyboard/button and
cursor/device environment, and cursor writes/ClipCursor/native input or OLE can interfere.
[V] `ui/base/ime/win/input_method_win_tsf.cc`, `InputMethodWinTSF::OnFocus/OnBlur` registers/removes
its dispatcher with `TSFBridge::GetInstance()`; [I] bypassing native IME for remote keys and committing
through the selected TextInputClient avoids relying on the desktop's physical composition session.

## 4. Genuine touch, gesture recognition, and capability exposure

[V] Yes: `ui/aura/window_event_dispatcher.cc`, `WindowEventDispatcher::PreDispatchTouchEvent`
updates Env touch state and calls `GestureRecognizer::ProcessTouchEventPreDispatch` on a root-DIP
copy. `ui/events/gestures/gesture_recognizer_impl.cc`,
`GestureRecognizerImpl::ProcessTouchEventPreDispatch` records targets and calls GestureProviderAura.
`ui/events/gestures/gesture_provider_aura.cc`, `GestureProviderAura::OnTouchEvent` updates a
MotionEventAura and runs the filtered gesture provider. No touchscreen-availability test gates this path.

[V] `content/browser/renderer_host/render_widget_host_view_event_handler.cc`,
`RenderWidgetHostViewEventHandler::OnTouchEvent` updates its own pointer state and builds
`WebTouchEvent` using `CreateWebTouchEventFromMotionEvent`; it disables synchronous handling
and routes/forwards real touch. This is not the mouse→TouchEmulator path.
[V] `content/browser/renderer_host/render_widget_host_view_aura.cc`, `RenderWidgetHostViewAura::ProcessAckedTouchEvent`
feeds the renderer's handled/unhandled result to the Aura host dispatcher; synchronous Views touch
uses `WindowEventDispatcher::PostDispatchEvent` instead. ACKs control gesture delivery.
[V] `WindowEventDispatcher::PreDispatchTouchEvent/PostDispatchEvent` and
`RenderWidgetHostViewAura::OnGestureEvent` therefore preserve gesture recognition and content routing;
`RenderWidgetHostViewEventHandler::OnGestureEvent` may gate pinch on `IsPinchToZoomEnabled()`.

[V] Construction: `ui/events/event.cc`, `ui::TouchEvent::TouchEvent(type, location, root_location,
TimeTicks, PointerDetails, flags)` generates a fresh `GetNextTouchEventId()` automatically;
copy construction preserves that ID. Contact ID and per-event unique ID are different fields.
[I] Construct a fresh event for each changed contact; keep the same contact ID from down through
move/up/cancel. Never reuse one copied event ID for independent updates.
[V] `ui/events/pointer_details.h`, `ui::PointerDetails` supplies kTouch, ID, radii, normalized force
and optional orientation/tilt. `ui/events/gestures/motion_event_aura.cc`, `MotionEventAura::OnTouch`
rejects duplicate downs, missing downs and unchanged-position moves; records time and unique ID.
[I] Supply finite coordinates/force, monotonic `base::TimeTicks` preserving source intervals, and
down→moves→up/cancel ordering; retain the chosen host for the contact and cancel on control loss.
[V] `ui/events/gestures/motion_event_aura.cc`, `GetPointerPropertiesFromTouchEvent` preserves `source_device_id`;
[I] keep a consistent virtual source-device ID for contacts that should participate in one gesture.
[V] `ui/aura/env_input_state_controller.cc`, `EnvInputStateController::UpdateStateForTouchEvent`
uses `1 << pointer_details().id`; `GestureRecognizerImpl::GetTouchLockedTarget` keys by contact ID.
[I] Remap remote IDs into a small nonnegative bounded set, unique across simultaneous contacts in
the browser process; a 0–9 pool is suitable. Do not pass arbitrary external pointer IDs directly.
[V] `ui/events/event.cc`, `TouchEvent::UpdateForRootTransform` also transforms contact radii;
[I] use the same forward/inverse host transform pair for locations and radii (Q1).

[V] Device detection is separate: `ui/base/pointer/pointer_device_win.cc`,
`GetTouchScreensAvailability` checks SM_DIGITIZER/NID_READY and integrated/external touch bits;
`MaxTouchPoints` returns SM_MAXIMUMTOUCHES only when a touch device is present, otherwise zero.
`GetAvailablePointerAndHoverTypesImpl` similarly derives coarse/fine/hover capabilities from Windows.
[I] Injecting touch through Aura does not make these APIs report a virtual touchscreen.
[V] `content/browser/web_contents/slow_web_preference_cache.cc`, `SlowWebPreferenceCache::Update`
stores `ui::MaxTouchPoints()` in `pointer_events_max_touch_points_`. Touch feature detection defaults
disabled on non-Android/non-iOS here; the touch-events switch can enable it or select hardware-based auto.
`Load` copies these and pointer/hover fields into WebPreferences; `content/browser/web_contents/web_contents_impl.cc`,
`WebContentsImpl::ComputeWebPreferences` starts by loading this cache.
[V] `third_party/blink/renderer/core/exported/web_view_impl.cc`, `blink::WebView::ApplyWebPreferences`
sets `RuntimeEnabledFeatures::SetTouchEventFeatureDetectionEnabled` and `SetMaxTouchPoints`.
`third_party/blink/renderer/core/events/navigator_events.cc`, `NavigatorEvents::maxTouchPoints`
returns the frame Settings value. `third_party/blink/renderer/core/dom/global_event_handlers.idl`,
`GlobalEventHandlers` gates ontouchstart/end/move/cancel attributes on TouchEventFeatureDetection.
[I] With no physical screen and no override, **maxTouchPoints remains 0**; enabling touch feature
detection alone does not change that number. A virtual capability/explicit emulation policy is needed
if pages must detect a touch-capable client; Aura input injection does not supply one.
[V] Actual delivery is separate from feature-detection attributes: `third_party/blink/renderer/core/input/touch_event_manager.cc`, `TouchEventManager::FlushEvents` checks document, touch handlers and view,
then dispatches accumulated touch points; it does not test maxTouchPoints or hardware availability.
[I] Registered touch listeners can receive injected events even when feature detection reports no
touchscreen; sites that decline to install listeners because of detection still need capability policy.

## 5. Alternative injection layers and browser chrome reach

[V] **RenderWidgetHost forwarding:** `content/browser/renderer_host/render_widget_host_impl.cc`,
`RenderWidgetHostImpl::{ForwardMouseEvent,ForwardWheelEvent,ForwardKeyboardEventWithLatencyInfo}`
forward into renderer input handling; [I] this layer bypasses Aura/Views hit testing and cannot
click the browser tabstrip/omnibox/Views menus. The caller must choose a content widget and handle
OOPIF targeting or use the content router. Verdict: useful for page-only control, unsuitable as
the common browser-window input layer (Q1's `RenderWidgetHostViewEventHandler::ShouldRouteEvents`).

[V] **DevTools Input:** `content/browser/devtools/protocol/input_handler.cc`,
`InputHandler::HandleMouseEvent` finds the RenderWidgetHost under the page point asynchronously;
`OnWidgetForDispatchMouseEvent` transforms it and `InputInjector::InjectMouseEvent` focuses and calls
`ForwardMouseEvent`. `DispatchKeyEvent` selects the focused renderer; `InsertText` commits into content;
`DispatchTouchEvent` selects synthetic-pointer actions or WebTouchEvent injection through TouchEmulator.
[V] `content/browser/renderer_host/input/synthetic_gesture_target_aura.cc`,
`SyntheticGestureTargetAura::DispatchWebTouchEventToPlatform` sends debugger-tagged events directly
to the content window delegate; the base target also rejects touch-starts outside content bounds
(`content/browser/renderer_host/input/synthetic_gesture_target_base.cc`, `SyntheticGestureTargetBase::DispatchInputEventToPlatform`).
[I] It supports page/OOPIF input but supplies no general Views chrome input path. Renderer key
handling can invoke browser accelerators, so a command working does not prove chrome targeting.
Verdict: retain for debugging/page automation, not window-wide remote input.

[I] **OS SendInput:** Windows input synthesis enters desktop/native routing, so it can reach browser
chrome when an ordinary visible HWND has foreground/focus and pointer targeting. It cannot select
independent logical headless roots this way: `ui/views/win/hwnd_message_handler_headless.cc`,
`HWNDMessageHandlerHeadless::{Init,Activate,ClearNativeFocus,SetCapture}` cloak the HWND and substitute
logical state for native activation/focus/capture. Verdict: reject under the no-synthesized-OS-input
constraint; shared desktop input is incompatible with independently controlled browser processes.
