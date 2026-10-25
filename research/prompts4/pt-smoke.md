# Task PT-SMOKE: automated input smoke test over the real protocol (CDP-driven, no new dependencies)
Deliverables (only these paths): `tools/smoke/smoke.mjs`, report `research/pt-smoke.md` (short: how to run, what passed/failed, raw output).
Hard limit 30 minutes. Do not modify any other file; if you find a bug elsewhere, describe it in the report with a proposed patch.
Read first: design/06-protocol.md sections 2-3, tools/latency/run.mjs (reuse its way of starting the control-plane and headless Edge),
tools/mock-endpoint/mock.js (`window.mockState` holds counts, pointers, field text, inputSeq), viewer/input.js, viewer/ime.js.
Write `node tools/smoke/smoke.mjs [--port 8210] [--viewer-browser <exe>] [--old-headless]` that:
1. Starts the control-plane (child process) on --port, the mock endpoint in headless Edge, and the viewer
   (`http://127.0.0.1:<port>/?stats=1`) in a second headless browser with `--remote-debugging-port=<free port>` (also use that for the mock).
   Use Node 24's global `WebSocket` + `fetch('http://127.0.0.1:<dp>/json')` for CDP; no npm packages.
2. Waits until the viewer has video (`video.videoWidth===1920` via Runtime.evaluate) and the data channels are open.
3. Drives REAL input through CDP on the viewer page (not synthetic DOM events): Input.dispatchMouseEvent (move, press/release left, right
   click, wheel), Input.dispatchKeyEvent (keyDown/keyUp for 'a', Shift+'A', Enter, Backspace, ArrowLeft, Ctrl+c), Input.dispatchTouchEvent
   with TWO simultaneous touch points (touchStart both, touchMove both, touchEnd one, touchCancel the other), and a tap on the mock's text field
   followed by Input.insertText('héllo 世界') and an IME composition (Input.imeSetComposition then Input.insertText) if available.
   Map coordinates with the same letterbox math as viewer/input.js (field rect is x 0.3-0.7, y 0.42-0.49 of the frame).
4. After each step reads `window.mockState` from the MOCK page via CDP and asserts: counts increased as expected, pointer down/up pairs leave
   no stuck pointers (`Object.keys(mockState.pointers).length===0` at the end), pid slots 0 and 1 for the two touches, normalized coordinates
   within 0.01 of the intended frame position, key codes/keys/modifiers as sent, field text equals the committed text, ime.state/ime.vk seen by
   the viewer (`window.rk.ime.state.focused`).
5. Prints one PASS/FAIL line per check and exits non-zero on failure; kills all child processes (taskkill /T /F) at the end.
Run it against headless Edge, then once with `--viewer-browser C:/src/tmp/chrome89/chrome-win/chrome.exe --old-headless` (Chromium 89; old
headless needs `--no-sandbox --enable-features=NetworkServiceInProcess` on this OS, see run.mjs). Put both outputs in the report.
