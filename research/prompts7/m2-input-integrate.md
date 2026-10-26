# Task m2-input-integrate (timeout 45 min): integrate + verify the cursor/IME/OSK and touch/zh-CN drafts on the real chain
Task dir C:\src\tmp\m2\input-integrate\. You are the ONLY builder/launcher now. You MAY edit C:\src\chromium\src (working tree on branch `prts`; do NOT
commit, rebase or reset there) and remotekit endpoint/, viewer/. Incremental builds only (`be.sh chrome`, `be.sh`). No commits.
Drafts (reviewed design, compile/runtime unverified; written against older HEADs, the window/viewport change (patch 15) and latency changes landed since):
- C:\src\tmp\m2\cursor-ime\{chromium.diff,remotekit.diff,NOTES.md} (cursor forwarding; IME composition/commit, caret, OSK show/hide)
- C:\src\tmp\m2\touch-l10n\{chromium.diff,remotekit.diff,NOTES.md} (viewer touch caps -> web prefs (patch 09), touch injection fixes; zh-CN/zh-TW PRTS strings)
Steps: apply each with `git apply -3` (src: `git -C C:\src\chromium\src apply -3`; remotekit likewise), resolve conflicts keeping both sides' intent,
build chrome + prts_endpoint, then run the test plans in the two NOTES on the chain (headless Edge viewer via CDP):
1. Cursor: hover a link -> viewer video element cursor = pointer; text field -> text; a page with `cursor:url(...)` custom cursor -> data URL cursor.
2. IME/OSK: click a remote <input>: viewer receives the IME state (log it); Input.insertText "hello" and Input.imeSetComposition (Chinese, e.g. "ni" ->
   commit "你") land exactly once in the remote field (screenshot); Backspace works; blur -> OSK hidden. With touch emulation (Emulation.setTouchEmulationEnabled
   + Emulation.setEmitTouchEventsForMouse or Input.dispatchTouchEvent) a tap on a remote input shows the OSK textarea on the viewer.
3. Touch: viewer with touch caps -> remote page reports navigator.maxTouchPoints>0 and matchMedia('(pointer: coarse)'); Input.dispatchTouchEvent tap
   clicks a link; one-finger drag scrolls a long page; two-finger pinch zooms (visualViewport.scale > 1) on a zoomable page.
4. zh-CN: launch with `--cr-lang=zh-CN` and check the profile/app menu strings show PRTS (screenshot).
Fix failures with minimal changes. NOTES.md: per-item pass/fail + evidence, conflicts resolved, files changed, remaining issues.
