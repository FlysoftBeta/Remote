# Task c1: input injection layer and activation in Windows new headless (verification task)
Output file: C:\Users\Flysoft\Desktop\remotekit\research\c1-input-layer.md   Time limit: 25 min.
Prior art injected ui::MouseEvent/KeyEvent/TouchEvent via aura::WindowTreeHost::SendEventToSink (see a-prior-art.md
section 4 only; prior patch C:\Users\Flysoft\Desktop\chromium\patches\0023*, 0035* if needed).
Questions, at the pin:
1. The event path from aura::WindowTreeHost::SendEventToSink (or GetEventSink()->OnEventFromSource) through
   aura::WindowEventDispatcher -> views (DesktopNativeWidgetAura, RootView) and to content
   (RenderWidgetHostViewAura::OnMouseEvent/OnKeyEvent/OnTouchEvent/OnGestureEvent -> RenderWidgetHostInputEventRouter).
   Confirm that browser UI (Views) and web content (incl. OOPIF) both get events this way. Which coordinate space
   (DIP vs pixels, root transform) do injected events need?
2. What normal Windows input does that path skip compared with HWNDMessageHandler WM_* handling (e.g. HandleMouseEvent,
   touch WM_POINTER handling, cursor update, capture via SetCapture, TrackMouseEvent, keyboard WM_KEYDOWN -> ui::KeyEvent
   + WM_CHAR, IME)? List what we must emulate.
3. Activation and focus in HWNDMessageHandlerHeadless: what Activate/Deactivate/IsActive/SetFocus/Show do; does any code
   path still call ::SetForegroundWindow, ::SetFocus, ::SetActiveWindow, SetCapture, ::ShowWindow on the real HWND in
   headless? Which views/aura focus objects (wm::FocusController, aura::client::ActivationClient, views::FocusManager)
   decide where key events go? Is anything process-global or desktop-global that would conflict across several browser
   processes on one Windows desktop?
4. Touch: is a ui::TouchEvent injected through the sink handled by aura's GestureRecognizer and delivered to
   RenderWidgetHostViewAura as real touch (not mouse emulation)? What is needed (PointerDetails, touch id, unique event
   ids, timestamps)? Any Windows-specific gating (e.g. ui::GetTouchScreensAvailability / touch feature detection,
   pointer events)? Does web content see `navigator.maxTouchPoints > 0` / touch events enabled with no touch screen?
5. Alternatives in one paragraph each with a verdict: content::RenderWidgetHost::ForwardMouseEvent etc., the DevTools
   Input domain (content/browser/devtools/protocol/input_handler.cc), OS SendInput. Which can reach browser UI chrome?
