# Task m1-input-validate (timeout 45 min): verify and fix input + popups (S11) on the real chain
You MAY edit and incrementally build (you are the only builder now): C:\src\chromium\src\chrome\browser\prts\*, the upstream files our
patches touch (ui/views/..., ui/aura/input_state_lookup_win.cc, ui/base/ime/*, chrome/browser/ui/views/chrome_views_delegate_win.cc,
ui/views/widget/desktop_aura/desktop_native_widget_aura.cc; keep `// PRTS:` + ui::IsPrtsRemoteMode() guards), and
C:\Users\Flysoft\Desktop\remotekit\endpoint\ (= src\prts_endpoint) and viewer/ only if a bug is there. No git commits, no gn clean, no full builds.
Notes + evidence: C:\src\tmp\m1\input-validate\NOTES.md (create in first 5 minutes) and screenshots there.

Setup: frames already flow end to end (see C:\src\tmp\m1\debug-frames\NOTES.md for the scripts validate-aligned.sh / run-aligned.mjs and
how they drive a headless Edge viewer with CDP). Build: `bash /c/src/tmp/m1/be.sh` (endpoint) / `bash /c/src/tmp/m1/be.sh chrome`.
Chain: `bash /c/src/tmp/m1/cp.sh 8300 --url=https://example.com --cr-enable-logging` (control-plane + endpoint + PRTS chrome; chrome log in
C:\src\tmp\m1\prof-8300\chrome_debug.log; `--cr-<switch>` forwards to chrome). Viewer: http://localhost:8300/ in Edge
(C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe, --headless=new --remote-debugging-port=<p>), drive real input with CDP
Input.dispatchMouseEvent / dispatchKeyEvent on the viewer <video>, and grab what the remote shows with a viewer screenshot
(Page.captureScreenshot) after each step. Design of the input path: design/06 section 2.2 (binary input), chrome/browser/prts/prts_input.cc.

Checklist (record pass/fail + screenshot for each; fix failures with minimal changes, rebuild, retest):
1. Mouse click into the page (example.com link navigates). 2. Click the omnibox, type a URL (keyboard), Enter -> navigates.
3. Ctrl+T new tab, click between tabs in the tab strip, close a tab with its X. 4. Open the app menu (three dots) and pick an item;
5. right-click context menu in a page; 6. a page `<select>` popup (use a data: URL or a CP-served test page you add under C:\src\tmp\m1\input-validate\)
opens INSIDE the window and selection works (S11). 7. Mouse wheel scrolls a long page. 8. Text selection drag in the page. 9. Hover states/tooltip.
10. Keyboard typing into a page <input> including Shift/uppercase, Backspace, arrows.
Report which popups (if any) still become separate top-level windows (not visible in the capture) and the chrome log evidence.
Final message: checklist results, fixes (files), remaining failures.
