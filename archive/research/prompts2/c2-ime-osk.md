# Task c2: editable-field detection and the on-screen keyboard hook (verification task)
Output file: C:\Users\Flysoft\Desktop\remotekit\research\c2-ime-osk.md   Time limit: 25 min.
Goal: when the remote user focuses (esp. by touch) an editable field anywhere in the browser (web content incl. OOPIF,
omnibox and other Views textfields), the browser must notify the endpoint with {editable, type, inputmode, enter-key
hint, caret bounds in window DIP} so the viewer can show its own soft keyboard; and the real Windows touch keyboard
(TabTip / InputPane) must NOT appear on the server. Text then comes back as committed text / key events.
Questions, at the pin:
1. Which ui::InputMethod implementation is used by a Windows Aura browser window in --headless=new
   (ui/base/ime/win: InputMethodWinTSF / InputMethodWinImm32 / headless-specific / mock)? Where chosen
   (ui/base/ime/init/input_method_factory.cc, ui/views/widget/desktop_aura/desktop_window_tree_host_win.cc)?
2. Observation points with exact signatures: ui::InputMethodObserver (OnFocus/OnBlur/OnTextInputStateChanged/
   OnCaretBoundsChanged/...), ui::TextInputClient getters (GetTextInputType, GetTextInputMode, GetTextInputFlags,
   GetCaretBounds, ShouldDoLearning, GetFocusReason ...), and how RenderWidgetHostViewAura updates them
   (TextInputManager, TextInputState incl. vk_policy, show_ime_if_needed, last_vk_visibility_request, mode,
   action/enterkeyhint). Does OOPIF focus go through the same top-level RenderWidgetHostViewAura TextInputClient?
   Does the omnibox (views::Textfield) report through the same InputMethod?
3. ui::VirtualKeyboardController and the Windows implementation (ui/base/ime/win/virtual_keyboard_controller_win.* or
   on_screen_keyboard*, InputPane / TabTip): who calls DisplayVirtualKeyboard/DismissVirtualKeyboard
   (InputMethodBase/InputMethodWinBase::ShowVirtualKeyboardIfEnabled, RenderWidgetHostViewAura on touch focus with
   FocusReason / pointer type), and what is the smallest override point where we can (a) suppress the real OS keyboard
   and (b) forward show/hide + input type to our own observer? Name file + function.
4. Committing remote text: smallest correct API to insert committed text / composition into the focused client
   (TextInputClient::InsertText / SetCompositionText / InsertChar, or InputMethod::DispatchKeyEvent with VKEY_PROCESSKEY).
   Any IME state we must keep coherent (composition cancel on blur)?
5. Recommendation in <= 15 lines with the files to patch.
