# c2 — editable-field detection and on-screen keyboard hook

Pin: `1aa07b638799e59656559ae6c6649d4b197f555e` (157.0.8103.15).
[V] = source read at this pin; [I] = inference or proposed patch. Paths below are relative to `src/`.

## 1. Windows Aura input-method selection

[V] **Actual unified-headless choice: `ui::MockInputMethod`.**
`ui/base/ime/init/input_method_factory.cc`, `ui::CreateInputMethod(...)` checks
`HasSwitch(switches::kHeadless)` before its Windows branch; the switch value is not inspected.
Load-bearing excerpt:
```cpp
if (base::CommandLine::ForCurrentProcess()->HasSwitch(switches::kHeadless))
  return base::WrapUnique(new MockInputMethod(ime_key_event_dispatcher));
#if BUILDFLAG(IS_WIN)
return std::make_unique<InputMethodWinTSF>(ime_key_event_dispatcher, widget);
```
[V] `ui/aura/window_tree_host.cc`, `aura::WindowTreeHost::GetInputMethod()` lazily calls
`ui::CreateInputMethod(this, GetAcceleratedWidget())`, retaining the result per host.
[V] `ui/views/widget/desktop_aura/desktop_window_tree_host_win.cc`,
`DesktopWindowTreeHostWin::GetHWNDMessageDelegateInputMethod()` returns `GetInputMethod()`;
`HandleIMEMessage(...)` forwards to `OnUntranslatedIMEMessage(...)`. Neither selects TSF.
[V] Non-headless Windows selects `InputMethodWinTSF`; there is no IMM32 selection branch in
`ui/base/ime/init/input_method_factory.cc`, `ui::CreateInputMethod` at this pin.
[V] `ui/base/ime/mock_input_method.h`, `MockInputMethod` derives directly from `InputMethod`,
not `InputMethodBase`/`InputMethodWinBase`, and owns `VirtualKeyboardControllerStub`.
[V] `ui/base/ime/mock_input_method.cc`, `GetTextInputType()` always returns `TEXT_INPUT_TYPE_NONE`;
read the focused client's getter, not this method, for field detection.
[V] Same file, `OnUntranslatedIMEMessage(...)` returns false, `CancelComposition(...)` is empty,
and `DispatchKeyEvent(...)` goes through client/post-IME dispatch without native IME conversion.

## 2. Observation points, metadata, OOPIF, and Views clients

[V] Exact observer declarations in `ui/base/ime/input_method_observer.h`, `ui::InputMethodObserver`
(each declaration below is public and virtual; the first five are pure virtual):

- `void OnFocus()`; `void OnBlur()`.
- `void OnCaretBoundsChanged(const TextInputClient* client)`.
- `void OnTextInputStateChanged(const TextInputClient* client)`.
- `void OnInputMethodDestroyed(const InputMethod* input_method)`.
- `void OnVirtualKeyboardVisibilityChangedIfEnabled(bool should_show)` (default empty body).
[V] `ui/views/win/hwnd_message_handler_headless.cc`, `HWNDMessageHandlerHeadless::Activate/Deactivate`
forward logical activation to the delegate; `ui/views/widget/desktop_aura/desktop_window_tree_host_win.cc`,
`HandleActivationChanged(bool)` forwards to `DesktopNativeWidgetAura::HandleActivationChanged(bool)` in
`ui/views/widget/desktop_aura/desktop_native_widget_aura.cc`, which calls input-method `OnFocus/OnBlur`.
[V] Same header, `OnFocus`/`OnBlur` describe top-level system-window focus, not DOM-field focus.
`ui/base/ime/mock_input_method.cc`, `MockInputMethod::OnFocus/OnBlur` notify these observers;
`ui/base/ime/input_method_base.cc`, `InputMethodBase::OnFocus/OnBlur` are empty.

[V] Exact client declarations in `ui/base/ime/text_input_client.h`, `ui::TextInputClient`
(all are public virtual, pure virtual):

- `ui::TextInputType GetTextInputType() const`.
- `ui::TextInputMode GetTextInputMode() const`.
- `int GetTextInputFlags() const`.
- `gfx::Rect GetCaretBounds() const`.
- `bool ShouldDoLearning()` (not const).
- `FocusReason GetFocusReason() const`.
- `bool CanComposeInline() const`; `bool HasCompositionText() const`.
[I] Observer arguments are const; obtain the current mutable client through `InputMethod::GetTextInputClient()`
when querying non-const `ShouldDoLearning()` (`ui/base/ime/input_method.h`; `text_input_client.h`, signatures above).
[V] Same class: `GetCaretBounds()` returns **screen DIP**, including selection bounds if selected.
`FocusReason` values are `FOCUS_REASON_NONE`, `_MOUSE`, `_TOUCH`, `_PEN`, `_OTHER`.
[V] There is **no `GetTextInputAction()` getter** in `ui/base/ime/text_input_client.h`, `TextInputClient`.
[V] `ui/base/ime/text_input_action.h`, `ui::TextInputAction` represents `enterkeyhint`:
`kDefault`, `kEnter`, `kDone`, `kGo`, `kNext`, `kPrevious`, `kSearch`, `kSend`.
[V] `ui/base/ime/text_input_mode.h`, `ui::TextInputMode` includes explicit `TEXT_INPUT_MODE_NONE`;
`ui/base/ime/text_input_type.h`, `ui::TextInputType` includes password, textarea, contenteditable,
date/time variants, and `TEXT_INPUT_TYPE_NULL` (raw-key-capable, restricted editing support).

[V] `ui/base/ime/mock_input_method.cc`, `MockInputMethod::SetFocusedTextInputClient(...)` notifies
only when the pointer changes **to non-null**; setting null emits no text-state notification.
`DetachTextInputClient(...)` clears the matching pointer silently.
[V] Same class, `OnTextInputTypeChanged(...)` and `OnCaretBoundsChanged(...)` notify observers
without checking that the supplied client equals `GetTextInputClient()`.
[I] Therefore subscribe per host, take the current `GetTextInputClient()` as authority, ignore
unfocused-client updates, and add null/detach notification in a PRTS-specific input method
(`ui/base/ime/mock_input_method.cc`, methods above). A plain observer alone misses blur-to-no-client.
[I] Emit `{editable:false}` on null, deactivation, and destruction; re-snapshot on focus,
text-state, and caret updates (`ui/base/ime/input_method_observer.h`, callbacks above).

[V] Web IPC enters `content/browser/renderer_host/render_widget_host_view_base.cc`,
`RenderWidgetHostViewBase::TextInputStateChanged(const ui::mojom::TextInputState&)`, which calls
`TextInputManager::UpdateTextInputState(this, state)`.
[V] `ui/base/ime/mojom/text_input_state.mojom`, `ui.mojom.TextInputState` contains
`node_id`, `type`, `mode`, **`action`**, `flags`, `selection`, `composition`, `can_compose_inline`,
`show_ime_if_needed`, `always_hide_ime`, `vk_policy`, and `last_vk_visibility_request`.
[V] `content/browser/renderer_host/text_input_manager.cc`, `ShouldUpdateTextInputState(...)`
compares node ID, type, mode, flags, and inline-composition support on Aura; **not action or VK policy**.
`TextInputManager::UpdateTextInputState(...)` nevertheless clones the full state and notifies observers.
[V] `third_party/blink/renderer/platform/exported/web_text_input_info.cc`, `WebTextInputInfo::Equals(...)`
compares action; renderer `WidgetBase::UpdateTextInputStateInternal` can therefore send an action-only
change even though browser `ShouldUpdateTextInputState` does not flag it.
[V] Producer: `third_party/blink/renderer/core/editing/ime/input_method_controller.cc`,
`InputMethodController::TextInputInfo()` obtains action via `InputActionOfFocusedElement()` from
`GetEnterKeyHintAttribute(...)`; mode and virtual-keyboard policy are collected alongside it.
`third_party/blink/renderer/platform/widget/widget_base.cc`, `WidgetBase::UpdateTextInputStateInternal(...)`
serializes `new_info.action` into `TextInputState.action` and `show_virtual_keyboard` into `show_ime_if_needed`.
[V] Exact callback in `content/browser/renderer_host/text_input_manager.h`, `TextInputManager::Observer`:
`void OnUpdateTextInputStateCalled(TextInputManager* text_input_manager,`
`RenderWidgetHostViewBase* updated_view, bool did_update_state)` (virtual, default empty).
[V] `content/browser/renderer_host/render_widget_host_view_aura.cc`,
`RenderWidgetHostViewAura::OnUpdateTextInputStateCalled(...)` calls
`GetInputMethod()->OnTextInputTypeChanged(this)` only when `did_update_state` and its active/focus/type guard pass.
`GetTextInputType/GetTextInputMode/GetTextInputFlags` read `text_input_manager_->GetTextInputState()`.
[I] Forward action/VK metadata through a PRTS callback on every relevant Aura state update,
including `did_update_state=false`, with focused-client/active-view gating and a null-state transition.
The existing full state supplies the hint; do not derive it from `flags` (`text_input_state.mojom`,
`TextInputState`; `render_widget_host_view_aura.cc`, `OnUpdateTextInputStateCalled`).
[I] State and visibility are separate: emit editable metadata for mouse/script/keyboard focus too;
`inputmode=none` can remain editable while requesting no automatic keyboard. Define editable from
current focused client plus non-NONE type, retaining the type for NULL/date-time handling
(`ui/base/ime/text_input_client.h`, getters; `virtual_keyboard_controller_win.cc`, `UpdateTextInputState`).
[V] Same Aura file, `OnSelectionBoundsChanged(TextInputManager*, RenderWidgetHostViewBase*)` calls
`GetInputMethod()->OnCaretBoundsChanged(this)`; `OnTextSelectionChanged(...)` also calls it.
`GetCaretBounds()` uses EditContext selection bounds or manager anchor/focus bounds, converts to
screen coordinates, and clamps to the content window bounds.
[I] Convert that screen-DIP rectangle into the streamed browser root's coordinate space, including
its toolbar offset; do not divide it by DSF again (`text_input_client.h`, `GetCaretBounds` contract;
`render_widget_host_view_aura.cc`, `GetCaretBounds` / `ConvertRectToScreen`).

[I] Concrete conversion: copy `client->GetCaretBounds()`, then call
`wm::ConvertRectFromScreen(host->window(), &caret)` for the root used for streaming;
`ui/wm/core/coordinate_conversion.cc`, `wm::ConvertRectFromScreen` uses its ScreenPositionClient.
[V] Renderer date/time caveat: `third_party/blink/renderer/platform/widget/widget_base.cc`,
`WidgetBase::UpdateTextInputStateInternal` returns early for `IsDateTimeInput(new_type)`;
all-enum coverage is not a guarantee every date-picker control generates ordinary text-input state.
[V] **OOPIF: yes, the top-level Aura client represents the active child widget.**
`content/browser/renderer_host/render_widget_host_view_child_frame.cc`,
`RenderWidgetHostViewChildFrame::Init()` registers through `GetTextInputManager()`.
`content/browser/renderer_host/render_widget_host_view_base.cc`, `GetTextInputManager()` obtains
`host()->delegate()->GetTextInputManager()` and registers each view.
[V] `content/browser/renderer_host/text_input_manager.h`, `TextInputManager::Observer` explicitly
requires the tab's top-level RWHV to observe child-frame state; `text_input_manager.cc`,
`UpdateTextInputState(...)` picks the focused non-NONE view as active, resetting the previous view
when cross-widget IPCs arrive out of order; `GetActiveWidget()` returns that view's widget.
[V] Same file, `SelectionBoundsChanged(...)` uses `TransformAndClampBounds(...)` and
`TransformPointToRootCoordSpace(...)` before notification: child-frame coordinates are already rooted.
[V] `content/browser/renderer_host/render_widget_host_view_aura.cc`, `OnWindowFocused(...)`
sets the Aura view as the focused `TextInputClient`; `GetInputMethod()` uses the root's host input method.

[V] **Omnibox/Views: yes, the same host input method.**
`chrome/browser/ui/views/omnibox/omnibox_view_views.h`, `OmniboxViewViews` inherits
`OmniboxContextMenuMixin<views::Textfield>`.
`ui/views/controls/textfield/textfield.cc`, `Textfield::OnFocus()` calls
`GetInputMethod()->SetFocusedTextInputClient(this)`; `OnBlur()` detaches it.
[V] `ui/views/view.cc`, `View::GetInputMethod()` uses its widget;
`ui/views/widget/widget.cc`, `Widget::GetInputMethod()` shares the top-level widget's method;
`ui/views/widget/desktop_aura/desktop_native_widget_aura.cc`, `GetInputMethod()` returns the host's method.
[V] `ui/views/controls/textfield/textfield.cc`, `Textfield::GetTextInputType()` returns NONE for
read-only/disabled controls; `GetTextInputMode()` returns DEFAULT; `GetTextInputFlags()` returns
`text_input_flags_`; `GetCaretBounds()` converts RenderText cursor bounds to screen coordinates.
`GetFocusReason()` returns `focus_reason_`; `ShouldDoLearning()` returns its override or false.
[V] `chrome/browser/ui/views/omnibox/omnibox_view_views.cc`, `OmniboxViewViews::OnFocus/OnBlur`
explicitly call the Textfield base; `GetPreferredTextInputType()` returns URL, or SEARCH for Windows CJK.
`ui/base/ime/mock_input_method.cc`, `IsInputLocaleCJK()` returns false, so headless selects URL here.
[I] Use default/unspecified enter-key action for native fields unless a Views control supplies a
specific hint; there is no DOM `enterkeyhint` to read (`Textfield` getters above; `text_input_client.h`).

## 3. Virtual keyboard call paths and suppression hook

[V] `ui/base/ime/virtual_keyboard_controller.h`, `ui::VirtualKeyboardController` exact virtual APIs:
`bool DisplayVirtualKeyboard()`, `void DismissVirtualKeyboard()`,
`void AddObserver(VirtualKeyboardControllerObserver*)`, `void RemoveObserver(VirtualKeyboardControllerObserver*)`,
`bool IsKeyboardVisible()` (all pure virtual).
[V] Native Windows selection is `ui/base/ime/win/input_method_win_base.cc`,
`CreateKeyboardController(HWND)`: Windows >= WIN10_RS4 uses `OnScreenKeyboardDisplayManagerInputPane`;
older branch uses `OnScreenKeyboardDisplayManagerTabTip`; `InputMethodWinBase` passes this into its base.
[V] `ui/base/ime/win/on_screen_keyboard_display_manager_input_pane.cc`,
`OnScreenKeyboardDisplayManagerInputPane::DisplayVirtualKeyboard/DismissVirtualKeyboard` debounce requests;
`Run()` posts `VirtualKeyboardInputPane::TryShowInBackgroundThread/TryHideInBackgroundThread`,
which invoke `IInputPane2::TryShow/TryHide`.
[V] `ui/base/ime/win/on_screen_keyboard_display_manager_tab_tip.cc`,
`OnScreenKeyboardDisplayManagerTabTip::DisplayVirtualKeyboard()` is the TabTip launch path;
`DismissVirtualKeyboard()` delegates to `OnScreenKeyboardDetector::DismissKeyboard()`.
[V] **Those managers are bypassed in headless:** `ui/base/ime/mock_input_method.cc`,
`MockInputMethod::GetVirtualKeyboardController()` returns its in-memory stub.
`SetVirtualKeyboardVisibilityIfEnabled(bool)` only notifies `InputMethodObserver`, without calling the stub.
[V] `ui/base/ime/virtual_keyboard_controller_stub.cc`, `VirtualKeyboardControllerStub::DisplayVirtualKeyboard()`
sets `visible_ = true` and notifies with an empty rectangle; `DismissVirtualKeyboard()` sets false
and sends `OnKeyboardHidden()`; neither calls Windows keyboard APIs.

[V] The generic native call is `ui/base/ime/input_method_base.cc`,
`InputMethodBase::SetVirtualKeyboardVisibilityIfEnabled(bool)`: notify the input-method observer,
then call controller `DisplayVirtualKeyboard()` or `DismissVirtualKeyboard()`.
[V] The task's `InputMethod[Win]Base::ShowVirtualKeyboardIfEnabled` name does not match this pin's
API: the helper is `ui/views/controls/textfield/textfield.cc`, `Textfield::ShowVirtualKeyboardIfEnabled()`.
It checks enabled/read-only/input-method then calls `SetVirtualKeyboardVisibilityIfEnabled(true)`.
`Textfield::RequestFocusForGesture(...)` calls it for touch or pen on Windows;
`RequestFocusWithPointer(...)` records the corresponding `FocusReason` before requesting focus.
[V] `content/browser/renderer_host/render_widget_host_view_aura.cc`,
`RenderWidgetHostViewAura::FocusedNodeChanged(...)` records `last_pointer_type_before_focus_`;
`GetFocusReason()` maps that to mouse/touch/pen/other (with a source comment that it is approximate).
[V] `content/browser/renderer_host/render_widget_host_view_aura.cc`, `OnTouchEvent(ui::TouchEvent*)`
records `pointer_details().pointer_type`; `OnGestureEvent(ui::GestureEvent*)` records the primary pointer.
[I] Preserve this Aura bookkeeping for remote touch; direct renderer injection alone need not update it
(same functions; `virtual_keyboard_controller_win.cc`, `IsPointerTypeValidForVirtualKeyboard`).
[V] Same file, `OnUpdateTextInputStateCalled(...)` only enters the Windows VK path for active,
non-NONE type and mode != NONE, creating `content::VirtualKeyboardControllerWin` if needed.
[V] **This is `content/browser/renderer_host/virtual_keyboard_controller_win.{cc,h}`, not a
`ui/base/ime/win/virtual_keyboard_controller_win.*` implementation.**
`VirtualKeyboardControllerWin::UpdateTextInputState(const ui::mojom::TextInputState* state)`
requires touch/pen **or manual policy**, a controller and live view/delegate; auto policy plus
`show_ime_if_needed` calls `ShowVirtualKeyboard()`; manual SHOW requires sticky activation,
manual HIDE calls `HideVirtualKeyboard()`.
[V] Same class, `ShowVirtualKeyboard()` calls input-method `SetVirtualKeyboardVisibilityIfEnabled(true)`;
`HideVirtualKeyboard()` and `HideAndNotifyKeyboardInset()` call controller `DismissVirtualKeyboard()` directly.
`FocusedNodeChanged(false)` hides; `IsPointerTypeValidForVirtualKeyboard()` checks last pointer type.
[V] `ui/base/ime/virtual_keyboard_controller_observer.h`, `VirtualKeyboardControllerObserver` signatures:
`void OnKeyboardVisible(const gfx::Rect& keyboard_rect)`; `void OnKeyboardHidden()` (pure virtual).
[I] Wire viewer dismissal/visibility back into remote-controller state: `virtual_keyboard_controller_win.cc`,
`VirtualKeyboardControllerWin::ShowVirtualKeyboard()` suppresses another show while its shown flag is true.
The current mock only sends show intent, so controller visibility and content's shown flag can diverge
(`ui/base/ime/mock_input_method.cc`, `SetVirtualKeyboardVisibilityIfEnabled`; same content helper).
[I] Treat `mode=NONE` and policy changes explicitly in the remote state machine: the existing Aura
VK guard skips mode NONE, so that update alone does not run Windows `HideVirtualKeyboard()`
(`render_widget_host_view_aura.cc`, `OnUpdateTextInputStateCalled`; content VK helper above).
[I] Thus observing only `OnVirtualKeyboardVisibilityChangedIfEnabled` misses web hide requests;
observe/wrap the controller too, or unify show/hide forwarding in a remote controller (methods above).
[I] Smallest seam for explicit OS-keyboard requests if native IME were retained: replace the controller at
`ui/base/ime/win/input_method_win_base.cc`, `CreateKeyboardController(HWND)` for PRTS only.
A guard solely in `InputMethodBase::SetVirtualKeyboardVisibilityIfEnabled` misses direct controller calls.
[I] For the actual headless path, keep suppression already provided by `MockInputMethod`; use a
PRTS-only factory branch in `ui/base/ime/init/input_method_factory.cc`, `CreateInputMethod`, with
coherent remote visibility handling in its input method/controller; forward type from the focused client.
[I] Do not claim OS-global suppression: the verified path prevents Chromium's InputPane/TabTip calls;
absence of independently launched Windows keyboard UI requires runtime qualification
(`MockInputMethod::GetVirtualKeyboardController`; native managers above).

## 4. Remote committed text and composition

[V] `ui/base/ime/text_input_client.h`, `ui::TextInputClient` exact text APIs:
`void InsertText(const std::u16string& text, InsertTextCursorBehavior cursor_behavior)`;
`void SetCompositionText(const ui::CompositionText& composition)`;
`size_t ConfirmCompositionText(bool keep_selection)`; `void ClearCompositionText()`;
`void InsertChar(const ui::KeyEvent& event)` (all virtual, pure virtual).
The contract uses UTF-16 strings/ranges; InsertText replaces composition/selection and forbids type NONE.
[I] Smallest shared committed-text route: resolve the **current focused client** from the window's
`InputMethod::GetTextInputClient()` and call `InsertText(text, kMoveCursorAfterText)` on the UI thread,
after checking the client/type (`text_input_client.h`, `InsertText`; Aura/Views implementations below).
[V] `content/browser/renderer_host/render_widget_host_view_aura.cc`, `InsertText(...)` CHECKs non-NONE,
then calls `text_input_manager_->GetActiveWidget()->ImeCommitText(...)`, routing into the active OOPIF too;
`ui/views/controls/textfield/textfield.cc`, `Textfield::InsertText(...)` filters characters, updates its model,
and invokes user-action/change callbacks, including the omnibox controller.
[V] Same Aura file, `SetCompositionText(...)` calls active widget `ImeSetComposition(...)` and records
`has_composition_text_`; it collapses composition selection to `composition.selection.end()` at this pin.
`ConfirmCompositionText(...)` uses `ImeFinishComposingText(keep_selection)`; `ClearCompositionText()`
uses `ImeCancelComposition()` when locally composing; all reset/update the local composition flag.
[V] `ui/views/controls/textfield/textfield.cc`, `SetCompositionText/ConfirmCompositionText/ClearCompositionText`
operate on the textfield model; `ConfirmCompositionText(true)` is not fully implemented there.
[V] `ui/base/ime/text_input_client.h`, `InsertChar(...)` is for a character generated by an unhandled
key press, expressly not a preceding `VKEY_PROCESSKEY`; it is not the arbitrary committed-string API.
`ui/base/ime/mock_input_method.cc`, `DispatchKeyEvent(...)` does not turn PROCESSKEY into committed text.
[I] Send Enter/Tab/arrows/backspace/shortcuts through the existing Aura key dispatch path;
use committed text only for textual insertion, avoiding a duplicate character event
(`text_input_client.h`, `InsertText/InsertChar`; `MockInputMethod::DispatchKeyEvent`).
[I] Bind remote text/composition to a window and focus generation; reject late messages after focus
changes rather than resolving them into a new field (`TextInputManager::UpdateTextInputState`, node_id;
`TextInputClient::AsWeakPtr`). Same Aura client can represent different nodes and OOPIF widgets.
[V] `content/browser/renderer_host/render_widget_host_view_aura.cc`, `FocusedNodeChanged(...)` and
`OnImeCancelComposition(...)` call input-method `CancelComposition(this)` and clear the local flag;
`ui/base/ime/mock_input_method.cc`, `CancelComposition(...)` itself does nothing.
[V] `ui/base/ime/input_method.h`, `InputMethod::OnTextInputTypeChanged` requires the client to confirm
or clear existing composition and call input-method `CancelComposition()` when necessary before the change.
[I] A node ID is document-local; include widget/document identity and a generation. After manager
active-widget switches, clearing the same Aura client's composition can target the new widget;
consume renderer cancellation and invalidate old remote preedit instead of canceling the new editor
(`text_input_state.mojom`, `TextInputState.node_id`; `text_input_manager.cc`, `UpdateTextInputState`;
`render_widget_host_view_aura.cc`, `ClearCompositionText`).
[I] Remote composition needs an explicit finish-or-cancel policy before detaching/changing client;
clear viewer preedit on blur, node/widget switch, navigation and destruction, and notify it when
renderer composition is canceled (Aura methods above; `Textfield::OnBlur`; `MockInputMethod::CancelComposition`).
Do not retain a raw client pointer for later RPCs; revalidate weak pointers across callbacks
(`TextInputClient::AsWeakPtr`; Aura `OnUpdateTextInputStateCalled` reentrancy guards).

## 5. Recommendation (at most 15 lines)

1. [I] `ui/base/ime/init/input_method_factory.cc`, `CreateInputMethod`: select a PRTS-only `RemoteInputMethod` derived from the existing mock.
2. [I] New `ui/base/ime/remote_input_method.{h,cc}`, proposed `RemoteInputMethod`: null/detach notifications, current client type, filtered updates, composition resets.
3. [I] Same proposed class, `SetVirtualKeyboardVisibilityIfEnabled`/`GetVirtualKeyboardController`: one remote controller forwards show/hide and viewer visibility acknowledgments, with no OS calls.
4. [I] `ui/aura/window_tree_host.cc`, `WindowTreeHost::GetInputMethod`: attach one endpoint observer per streamed host; include lifecycle and root-DIP conversion.
5. [I] `content/browser/renderer_host/render_widget_host_view_aura.{h,cc}`, `OnUpdateTextInputStateCalled`: add a PRTS full-state callback even when `did_update_state=false`, gated to the focused client/active view.
6. [I] That callback forwards action/mode/policy/node identity; native clients use existing getters and a default action (`ui/base/ime/text_input_client.h`, `TextInputClient`).
7. [I] `content/browser/renderer_host/render_widget_host_view_aura.cc`, `FocusedNodeChanged`/`OnImeCancelComposition`: invalidate remote preedit and focus generations.
8. [I] `ui/base/ime/BUILD.gn`/`ui/base/ime/init/BUILD.gn`: wire the proposed class; existing `Textfield` and content VK helper already reach the input method/controller.
9. [I] Remote adapter calls `ui/base/ime/text_input_client.h`, `InsertText`/composition APIs, and `ui/wm/core/coordinate_conversion.cc`, `ConvertRectFromScreen`.
10. [I] Qualify `content/browser/renderer_host/render_widget_host_view_aura.cc`, `RenderWidgetHostViewAura`: re-tap, mode=none, manual policy, action-only, OOPIF, blur, DSF, no OS keyboard.

