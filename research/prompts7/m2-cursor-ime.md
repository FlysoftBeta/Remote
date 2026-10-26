# Task m2-cursor-ime (timeout 40 min): DRAFT cursor forwarding + IME/on-screen-keyboard end to end
Task dir C:\src\tmp\m2\cursor-ime\. DRAFT ONLY: another agent is building and running the chain. Do NOT launch chrome/endpoint, do NOT build, do NOT edit
C:\src\chromium\src or the remotekit repo in place. Produce diffs: (a) chromium.diff against the src `prts` branch HEAD (copy files to a\ and b\ under
the task dir, git-style a/<rel> b/<rel>, verify `git -C C:\src\chromium\src apply --check`); (b) remotekit.diff against remotekit HEAD (endpoint/, viewer/,
verify with `git -C C:\Users\Flysoft\Desktop\remotekit apply --check`). Keep them small and self-contained so they apply with `git apply -3` after
other changes to the same files.

1. Cursor: patch 06 added ui/views/widget/desktop_aura/remote_cursor_sink.h (SetCursorNative in remote mode reports there). Wire: browser (chrome/browser/prts,
   implement the sink, map ui::mojom::CursorType -> CSS cursor names per design/06 `cursor {css}`; custom cursors (kCustom) -> PNG (<=128 px, hotspot)
   sent as `url(data:image/png;base64,...) x y, auto`; dedupe unchanged cursors) -> Mojo (new message on the existing endpoint-facing interface) ->
   endpoint -> ctl DataChannel cursor message -> viewer sets the cursor on the video element. Check the existing message naming in
   viewer/protocol.js and design/06.
2. IME / remote on-screen keyboard: read what exists (ui/base/ime/remote_input_method.*, chrome/browser/prts/prts_ime_bridge.*, viewer/ime.js, endpoint
   handling of ime.* ctl messages, design/06 IME section, research/c2-ime-osk.md). Identify every missing or broken link in: remote editable focus ->
   IME_STATE (type, mode, caret rect, reason) -> endpoint -> viewer shows/hides the OSK (focus a hidden textarea positioned at the caret on touch-capable
   viewers; on desktop viewers use it as the IME surface so Chinese IME composition works) -> viewer composition/commit (compositionstart/update/end,
   beforeinput insertText/deleteContentBackward) -> ime.compose / ime.commit / key events -> RemoteInputMethod -> TextInputClient. Fix the gaps in the
   diffs. Avoid double input (keydown + commit both inserting text).
NOTES.md: what was missing, the design of each fix, API citations, a manual test plan (CDP steps the integrator will run: Input.imeSetComposition,
Input.insertText, touch emulation), risks.
