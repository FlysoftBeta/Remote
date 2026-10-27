# Task input-ime (timeout 45 min): patches 06 aura-views-remote-input, 07 prts-input, 08 remote-ime (M1 subset)
Output dir: C:\src\tmp\m1\input\
Inputs: C:\src\tmp\m1\contract\prts.mojom; if present C:\src\tmp\m1\s10-link\prts_delegates.h (implement those interfaces exactly; else
define your own and say so). research/c1-input-layer.md, research/c2-ime-osk.md, design/02 sections 2.3 and 2.5, design/06 2.2 and 3.

Deliverables (compile-checked by reading headers at the pin; cite file:line in NOTES):
1. 06 as an a/b diff (`06-aura-views-remote-input.diff`, verified with git apply --check), M1 subset, every hook `// PRTS:` +
   `ui::IsPrtsRemoteMode()`:
   - ui/aura/input_state_lookup_win.cc: remote mode does not read ::GetKeyState (return false / derive from Env flags) so
     Env::IsMouseButtonDown follows injected event flags.
   - ui/views/win/hwnd_message_handler_headless.{h,cc}: logical capture (SetCapture/ReleaseCapture/HasCapture keep a bool and notify
     capture loss like the real handler), so Views drags (tab drag, scrollbars, text selection) work.
   - ui/views/widget/desktop_aura/desktop_window_tree_host_win.cc: SetCursorNative -> report to an observer instead of ::SetCursor
     (define ui/views/widget/desktop_aura/remote_cursor_sink.h: a tiny static setter interface in Views so chrome/ can receive cursor type);
     MoveCursorToScreenLocationInPixels / UpdateMouseLockRegion / OnCursorVisibilityChangedNative / CaptureSystemKeyEventsImpl no-op in remote mode.
   - Tab drag-out uses a nested move loop / DoDragDrop: make sure dragging a tab inside the strip works and dragging out does not enter
     ::DoDragDrop (desktop_drag_drop_client_win.cc) in remote mode (cancel instead). Keep total upstream lines ~110 or less.
2. 07 chrome/browser/prts/prts_input.{h,cc}: PointerInput/WheelInput/KeyInput (contract) -> ui::MouseEvent / MouseWheelEvent /
   ui::TouchEvent / ui::KeyEvent -> aura::EventInjector().Inject(host, &event). Coordinates: normalized * root DIP size (host->window()
   bounds). Mouse: track pressed buttons, generate ui flags + changed_button_flags, click count (double click within
   GetDoubleClickTime and few px), ET_MOUSE_ENTERED/EXITED on leave, mouse move vs drag (ET_MOUSE_DRAGGED when a button is held).
   Key: DomCode/DomKey from strings via ui::KeycodeConverter, KeyboardCode via ui::DomCodeToUsLayoutKeyboardCode or DomKey mapping;
   flags from mods; EF_IS_REPEAT; printable characters must produce text in omnibox and web pages (verify the char/key-press path in Aura:
   does a KeyEvent with is_char false + DomKey character insert text in Views Textfield and in RenderWidgetHostViewAura with MockInputMethod
   or does it need a separate char event? cite). Touch: ui::TouchEvent per changed pointer, PointerDetails(kTouch, pid, rx, ry, force).
   ResetInput: release buttons/keys, cancel touches. Timestamps from the struct.
3. 08 (M1 subset): ui/base/ime/remote_input_method.{h,cc} deriving MockInputMethod (notify observers on detach/null focus; virtual-keyboard
   controller that forwards Display/Dismiss to a callback), factory hook diff in ui/base/ime/init/input_method_factory.cc + BUILD.gn
   (`08-remote-ime.diff`), and chrome/browser/prts/prts_ime_bridge.{h,cc}: observes the active root's InputMethod
   (host->GetInputMethod()), emits ImeState (type/mode/flags/caret normalized), and implements ImeCommit (TextInputClient::InsertText),
   ImeCompose (SetCompositionText), ImeDelete (ExtendSelectionAndDelete).
Scope: no capture, no endpoint code. Max ~1300 lines total.
