# M1 input validation / S11

## Outcome

All ten checklist cases PASS on the real Chromium -> in-tree PRTS -> libwebrtc endpoint -> headless Edge viewer chain. No product-source changes, no builds, no commits. No tested popup became an additional native top-level window or disappeared from capture. Testing ran 2026-10-26 approximately 08:33-08:43 Asia/Singapore; this NOTES file was created before the first run (incremental record preserved in NOTES-investigation.md).

Source browser reports Chrome/157.0.8103.15 through Browser.getVersion. Integration checkout HEAD: 818260276dce993bd02ec07268dafea067194582 (existing patched checkout, not a change made by this task). Viewer Edge is 156. The default browser window is used, not --app. No NetworkServiceInProcess2, fake vsync, software capture or source-CDP input is used. Stream is 1898x1068 including browser chrome.

## File list

All created files are under C:/src/tmp/m1/input-validate:
- NOTES.md, NOTES-investigation.md.
- regression.mjs, regression.log, results.json: reproducible final interaction suite and assertions.
- cdp.mjs: viewer CDP mouse/key helpers and source read-only inspection.
- fixture.html, fixture-server.mjs: deterministic input/select/hover/long-page fixture, localhost:8305.
- launch.mjs: normal-browser chain on 8300, source CDP 9333, headless Edge CDP 9444. Profiles and chain/Edge logs are local to this output directory.
- final-*.png: final viewer Page.captureScreenshot evidence after each action, including intermediate tab/omnibox/popup states.
- 00-*.png through 10-*.png and step*.mjs: exploratory evidence and scripts.
- window-census.ps1, windows-*.txt, popup-window-results.json: native top-level HWND census while each popup is open.
- chrome-debug.log: frozen Chrome log after final suite; chrome-log-excerpts.txt, chrome-log-summary.json, summary.txt, summarize.mjs.

No files in the remotekit repository or Chromium checkout were written by this task. Existing binaries were exercised directly; no source patch or BUILD.gn change was necessary.

## Checklist and screenshot evidence

Final regression exited 0. Every action is sent to the viewer using Input.dispatchMouseEvent / Input.dispatchKeyEvent, passing through its normal listeners, binary DataChannel, endpoint and Aura injection. Source CDP only reads DOM/targets/visibility; it does not navigate, set values, dispatch input, or manipulate focus. Screenshots are of the viewer, not source CDP.

| # | Result and observed behavior | Screenshot(s) |
|---|---|---|
| 1 | PASS: Example Domain Learn more click navigates to https://www.iana.org/help/example-domains. | final-01-link.png |
| 2 | PASS: mouse omnibox focus, Ctrl+A, URL typed as KEY events, Enter -> task fixture. Omnibox dropdown visible in stream. | final-02-omnibox-typed.png; final-02-navigated.png |
| 3 | PASS: Ctrl+T increases remote page target count; click first/second tabs changes fixture visibility visible/hidden; X removes new tab and restores fixture. | final-03-new-tab.png; final-03-first-tab.png; final-03-second-tab.png; final-03-closed.png |
| 4 | PASS: three-dot app menu visible; click its New Tab tile creates a remote tab. | final-04-app-menu.png; final-04-item-new-tab.png |
| 5 | PASS: right-click page menu visible; mouse click Reload changes source performance.timeOrigin. | final-05-context.png; final-05-reloaded.png |
| 6 | PASS S11: select popup visible inside captured window; click Bravo changes select.value and onchange label. | final-06-select-open.png; final-06-select-picked.png |
| 7 | PASS: wheel scrollY 0 -> 672; long-page content visibly moves. | final-07-wheel.png |
| 8 | PASS: drag selects "Drag to select these words across the " with visible selection highlight. | final-08-drag.png |
| 9 | PASS: hover changes background to rgb(255, 223, 112); delayed native tooltip "PRTS tooltip visible" appears inside capture. Visually reviewed. | final-09-hover.png; final-09-tooltip.png |
| 10 | PASS: ab, actual Shift down/C/up, d, Backspace, Left, X, Right, ! -> abXC!, caret 5. | final-10-keyboard.png |

## Popup/root evidence and Chrome logs

While omnibox dropdown, app menu, page context menu, select and tooltip were open, EnumWindows filtered to browser PID 20896 found exactly the same five HWND/class/visibility/geometry entries as baseline. No additional/removed handles or geometry changes (popup-window-results.json). The two pre-existing Chrome widget HWNDs are 4261850 / Chrome_WidgetWin_1 and 7014206 / Chrome_WidgetWin_0; both remain OS-hidden in headless mode. The other three are status tray, power message and default IME windows. They predate popup opening. This census and the captured popup pixels together establish no new top-level popup HWND for the tested cases; HWND visibility=False alone is normal headless behavior and is not evidence of a missing popup.

The frozen chrome-debug.log corroborates one captured browser root, but has no per-popup parenting diagnostic. Do not infer popup placement from absence of log messages alone:
- line 7: PRTS: Network Service bootstrap crashed.
- line 10: PRTS: retrying startup GET after Network Service bootstrap.
- line 11: PRTS: StartCapture window=1 size=1898x1069 (encoder stream is aligned to 1068 high).
- lines 119, 1930, 3785, 6181, 8143: captured frames #1, #301, #601, #901, #1201, gmb.
- 26 StackChildLayerRelativeTo warnings for WebContentsViewAura; they occur at startup/tab switching. All tested stacking/menu rendering and interactions passed despite those warnings.
- 8319 DXGI-output diagnostic lines; known monitor lookup noise from preceding frame investigation, still present. Zero FATAL / CHECK failed lines in the frozen log.

No popup-specific Chrome error or separate capture-root start was observed. The native census is the direct evidence for unchanged top-level windows; screenshots are the direct evidence for visibility.

## Fixes and harness correction

No input/IME/popup/endpoint/viewer product fix was needed. No rebuild was run.

The initial test helper supplied nativeVirtualKeyCode to Edge CDP Ctrl+T, which triggered a LOCAL Edge tab. The corrected cdp.mjs omits that field, and explicitly selects the viewer target by URL rather than whichever page is first. windowsVirtualKeyCode, physical code, key and modifier bits remain present. The remote Ctrl+T creates the remote tab through the actual viewer listeners/DataChannel. This is a test-harness correction, not a change to the viewer. Exploratory screenshots were overwritten with the corrected successful tab run; the incremental notes record the failed harness attempt.

## API citations [V] and [I]

[V] = read at current pinned integration checkout. [I] = inference/limit.
- [V] ui/base/ui_base_switches.h:128: exported ui::IsPrtsRemoteMode runtime guard; existing popup hooks use it.
- [V] chrome/browser/ui/views/chrome_views_delegate_win.cc:162-166: parent OR context, non-WINDOW/non-CONTROL remote widgets select kNativeWidgetAura.
- [V] ui/views/widget/desktop_aura/desktop_native_widget_aura.cc:254-259: guarded GetDefaultParent returns root_window_ instead of constructing another desktop host.
- [V] ui/views/widget/widget.h:239 onward: InitParams::Type (POPUP/MENU/TOOLTIP/BUBBLE); fields parent/context/name are existing APIs. No new C++ call sites added.
- [V] ui/aura/event_injector.h:29-31: event injection coordinates are host-relative DIP.
- [V] ui/display/win/screen_win_headless.h:56-57: cursor query and existing virtual point setter used by PrtsInput::InjectMouse.
- [V] chrome/browser/prts/prts_input.cc: existing Pointer/Wheel/Key implementations reviewed in full; they are unchanged by this task.
- [V] content/browser/devtools/protocol/input_handler.cc:1270-1275: CDP input with native key may reach browser; absent native key sets skip_if_unhandled=true. Earlier incremental line citation was stale; these are the current lines read.
- [V] content/browser/devtools/protocol/input_handler.cc:770 onward: injector forwards keyboard events to renderer host; :1186 onward defines DispatchKeyEvent and modifier/code/key construction.
- [I] Local physical browser-reserved shortcuts are not qualified by this CDP test. Existing viewer does not request keyboard lock; OS/local-browser interception requires separate keyboard-lock/fullscreen/app-context evaluation. Passing remote KEY handling does not prove local shortcut capture.

## Exact integration / reproduction

No source integration, BUILD.gn dependencies, upstream hooks, patch application or incremental compile is needed. The pre-existing S11 hooks listed above are sufficient for this tested chain. Do not replace them with multi-root capture on this evidence.

With ports 8300/8305/9333/9444 free, from any directory, run in two terminals:

    node C:/src/tmp/m1/input-validate/fixture-server.mjs
    node C:/src/tmp/m1/input-validate/launch.mjs

Then:

    node C:/src/tmp/m1/input-validate/regression.mjs
    node C:/src/tmp/m1/input-validate/summarize.mjs

launch.mjs starts control-plane from the existing repository with ENDPOINT_PATH to out/dev/prts_endpoint.exe and ENDPOINT_ARGS --chrome=.../chrome.exe --user-data-dir=.../input-validate/chrome-profile --url=https://example.com --cr-enable-logging --cr-remote-debugging-port=9333. It uses task-local profiles and does not delete/reset another task's profile. It is equivalent to the requested cp.sh chain with a task-local profile. Fixture uses a separate task-local server instead of modifying control-plane routes. The server remains localhost-only.

The chain/viewer and fixture server are left running for inspection (launch session starts CP PID 21688, Edge PID 9232; browser PID 20896). Source log is chrome-profile/chrome_debug.log; frozen evidence is chrome-debug.log. regression.mjs overwrites final screenshots/results on rerun.

## Remaining failures and limits

- No failures among the ten requested CDP-chain cases. No tested popup escaped capture.
- Native file picker, permission dialogs, detached-tab/new-window behavior, IME composition, touch/pen, alternate DPI/geometry and popup boundary clipping beyond the presented select case were not part of this checklist.
- Local reserved-shortcut handling remains unqualified as explained above.
- Existing bootstrap crash recovery, unmanaged-layer warnings and DXGI monitor errors remain; they did not fail this suite and were not modified.

