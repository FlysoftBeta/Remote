# Task c: input injection, focus/activation management, editable-field detection
Write: C:\Users\Flysoft\Desktop\remotekit\research\c-input-focus.md
First skim C:\Users\Flysoft\Desktop\remotekit\research\a-prior-art.md (if it exists) and the prior art's input patches:
C:\Users\Flysoft\Desktop\chromium\patches\0008-*, 0009-*, 0023-*, 0035-*, 0036-* and docs/prts. Verify against current stable
source and fill gaps; do not just restate.

Questions (concrete file paths / classes / functions):
1. Candidate injection layers for mouse, wheel, keyboard (incl. IME/text commit), touch (multi-touch with pointer IDs),
   without synthesized OS input (no SendInput / PostMessage WM_*):
   (a) aura::WindowTreeHost::SendEventToSink / ui::EventSource / ui::EventSink (what DesktopWindowTreeHostWin does with
       events from HWNDMessageHandler; coordinate spaces, DIP vs pixels; event flags/timestamps);
   (b) views::Widget / HWNDMessageHandler delegate level;
   (c) content::RenderWidgetHost::ForwardMouseEvent / ForwardKeyboardEvent / ForwardGestureEvent / touch (bypasses the browser
       UI chrome -> no omnibox/tab strip interaction);
   (d) DevTools Input domain (Input.dispatchMouseEvent/KeyEvent/TouchEvent/insertText, where it is implemented:
       content/browser/devtools/protocol/input_handler.cc) - page only, no browser UI.
   Which layer reaches both the browser UI chrome (views) and web content, handles touch -> gesture recognition
   (ui::GestureRecognizer, aura::Env gesture handling), and works with the cloaked HWND + multiple top-level windows +
   popups (menus, select popups, omnibox dropdown, which are separate widgets/HWNDs)?
2. Focus and activation: how Aura/Win decides activation (wm::FocusController? on Windows desktop:
   DesktopNativeWidgetAura + DesktopFocusRules, HWNDMessageHandler WM_ACTIVATE, ::SetFocus / GetForegroundWindow dependency).
   What breaks when the window never becomes the OS foreground window (cloaked, multi-tenant on one desktop)? Where to patch
   so each PRTS browser window believes it is active/focused independently of the OS foreground window (e.g. faking
   activation in DesktopWindowTreeHostWin::IsActive / HWNDMessageHandler, aura::client::ActivationClient). Caret blink,
   :focus-visible, focus events and keyboard routing depend on this. Also mouse capture (SetCapture) and cursor.
3. Keyboard specifics: ui::KeyEvent construction without native MSG (DomCode/DomKey/KeyboardCode), character events,
   ui::InputMethod on Windows (InputMethodWinTSF / InputMethodWinImm32), how to commit composed text (from a remote IME /
   on-screen keyboard) directly: ui::TextInputClient::InsertText / SetCompositionText, and accelerators/shortcuts.
4. Editable-field focus detection for a remote on-screen keyboard: ui::InputMethodObserver (OnTextInputStateChanged,
   OnFocus/OnBlur), TextInputClient::GetTextInputType / GetTextInputMode / inputmode attribute, VirtualKeyboardController
   (ui::VirtualKeyboardController, ui/base/ime/win/on_screen_keyboard_display_manager*), content's
   RenderWidgetHostViewAura text input state (TextInputManager, ImeTextSpan, TextInputState with show_ime_if_needed /
   vk policy, ui::mojom::VirtualKeyboardVisibilityRequest), and omnibox (views::Textfield) focus. Where can one hook
   observe both web and browser-UI editable focus and also get caret bounds (for scrolling the field into view)?
5. Touch: how real Windows touch (WM_POINTER) flows into Aura (HWNDMessageHandler::OnPointerEvent -> ui::TouchEvent),
   how to inject ui::TouchEvent with PointerDetails directly, gesture recognition, scroll/fling/pinch behavior, and the
   "touch -> pop up keyboard" heuristic (show_ime_if_needed is set on tap). How prior art did it (0035/0036).
6. Recommendation: the injection layer (likely a small PRTS hook that builds ui::Events and dispatches via
   WindowTreeHost::SendEventToSink of the right host, plus fake activation), concrete files to patch, estimated size,
   and the event/focus protocol between endpoint and Chromium. List spikes to verify early.
