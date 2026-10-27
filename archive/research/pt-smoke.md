# PT-SMOKE: real protocol input smoke

## Scope and status

- [V] Deliverables: `tools/smoke/smoke.mjs` and this report only.
- [V] Initial workspace already had unrelated changes; these are left untouched.
- [V] Read protocol sections 2–3, latency runner, mock endpoint, input and IME bridges.
- [V] Harness implemented with Node built-ins, global WebSocket and fetch; no packages added.
- [V] Edge run completed: 38 PASS / 1 FAIL, exit 1 (requested touch-slot mismatch).
- [V] Chromium 89 run completed: 37 PASS / 1 FAIL, exit 1; composition capability unavailable.

## How to run

```powershell
node tools/smoke/smoke.mjs --port 8210
node tools/smoke/smoke.mjs --port 8211 --viewer-browser C:/src/tmp/chrome89/chrome-win/chrome.exe --old-headless
```

- [V] Each run starts its own control-plane; an occupied port is rejected instead of reusing a service.
- [V] Mock always uses Edge; `--viewer-browser` / `--old-headless` affect only the viewer.
- [V] Profiles/logs stay under `C:/src/tmp/pt-smoke/`; profiles are retained for inspection.
- [V] Browser/CP children are killed with `taskkill /T /F` on completion, failure, SIGINT or SIGTERM.
- [V] 150-second watchdog bounds a run; failures exit 1; no added npm dependencies.

## Approach and reuse

| Source | Selected reuse / reason |
|---|---|
| `tools/latency/run.mjs` | Control-plane child, isolated browser profiles, Edge flags, old-headless compatibility, taskkill cleanup. |
| `viewer/input.js` | Object-fit contain coordinate mapping; mouse slot 0, touch slots 1–15. |
| `design/06-protocol.md` sections 2–3 | Binary input semantics and IME control messages. |
| Chromium prior art | None used; no read or write required. |

## Results

| Run | Measured result | Exit |
|---|---|---|
| Edge 156.0.4314.42 | [V] 38 PASS / 1 FAIL; input, text and composition pass; requested touch slots fail. | 1 |
| Chromium 89.0.4389.0, old headless | [V] 37 PASS / 1 FAIL; Unicode and IME focus/VK pass; composition unavailable; touch slots fail. | 1 |

| Coverage | Verified result |
|---|---|
| Mouse / wheel | [V] Move, left/right down/up, scaled wheel deltas and normalized coordinates. |
| Keyboard | [V] All 12 down/up packets: code, key, VK, modifiers for a, Shift+A, Enter, Backspace, ArrowLeft, Ctrl+c. |
| Touch | [V] Two simultaneous contacts, both moved, one ended while the other remained, other cancelled. |
| Letterbox | [V] 1920×1080 frame inside 1280×900 viewport: 90px top/bottom bars; coordinate error <0.01. |
| Text / IME | [V] Touch field focus, viewer ime.state.focused, received ime.vk show, exact Unicode commit. |
| Composition | [V] Edge preedit then exact commit with cleared comp; Chrome 89 returns CDP method-not-found. |
| End state | [V] No active pointers; 26 consecutive binary packets; inputSeq=26; no gaps. |
| Cleanup / syntax | [V] taskkill cleanup passed; node --check passed; terminated process entries checked with HasExited. |

## Findings and proposed patches

- [V] Task requests touch pid slots 0 and 1; current design §2.2 and viewer/input.js slotFor reserve 0 for mouse and allocate touches from 1. Both browsers send [1,2]. The harness explicitly fails the requested check and separately passes the documented allocation.
- [V] CDP-driven Edge compositionend has isTrusted=false, while all pointer/wheel/key events are trusted. No synthetic DOM events are dispatched by the harness.
- [I] Proposed correction: change task expectation from "pid slots 0 and 1 for the two touches" to "pid slots 1 and 2 for the two touches; mouse slot 0". Changing viewer allocation alone would violate the current mouse reservation. No production patch is warranted against the current design.
- [V] Passive CDP-installed channel observers record received binary/control messages; they preserve application handlers and never send input. Every drive action uses CDP Input methods, with mockState read after each step.
- [V] An initial diagnostic Edge run treated untrusted compositionend as an error; refined the extra trust check to pointer/wheel/key events, then reran. The browser-generated composition event still appears in raw diagnostics.
- [V] Windows can retain terminated Edge process entries; HasExited=true and taskkill reports no running instance. Explicit harness exit avoids waiting on obsolete browser pipe/WebSocket handles.
- [I] Limits: browser mock validates transport/viewer behavior, not native prts_endpoint injection or a physical OS keyboard. Composition on Chrome 89 is untested because its CDP method is absent.

## Raw output: Edge

```text
RUN 2026-10-25T15:56:45.110Z port=8210 viewer=C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe oldHeadless=false
INFO profiles=C:\src\tmp\pt-smoke\run-l4bBPB
PASS control-plane started
INFO mock=Edg/156.0.4314.42 viewer=Edg/156.0.4314.42
PASS viewer video 1920 and all data channels open: {"width":1920,"enabled":true,"channels":[{"id":0,"label":"input","state":"open"},{"id":1,"label":"ctl","state":"open"},{"id":2,"label":"probe","state":"open"}]}
PASS mock data channels open and handshake received: [{"label":"input","id":0,"state":"open"},{"label":"ctl","id":1,"state":"open"},{"label":"probe","id":2,"state":"open"}]
PASS letterbox coordinate mapping exercised: {"left":0,"top":90,"width":1280,"height":720,"scale":0.6666666666666666}
PASS mouse move coordinates: count +1; seq=1
PASS left mouse down: count +1; seq=2
PASS left mouse up, no stuck pointer: count +1; seq=3
PASS right mouse down: count +1; seq=4
PASS right mouse up, no stuck pointer: count +1; seq=5
PASS wheel coordinates and scaled deltas: count +1; seq=6
PASS a keyDown: code/key/VK/modifiers: count +1; seq=7
PASS a keyUp: code/key/VK/modifiers: count +1; seq=8
PASS Shift+A keyDown: code/key/VK/modifiers: count +1; seq=9
PASS Shift+A keyUp: code/key/VK/modifiers: count +1; seq=10
PASS Enter keyDown: code/key/VK/modifiers: count +1; seq=11
PASS Enter keyUp: code/key/VK/modifiers: count +1; seq=12
PASS Backspace keyDown: code/key/VK/modifiers: count +1; seq=13
PASS Backspace keyUp: code/key/VK/modifiers: count +1; seq=14
PASS ArrowLeft keyDown: code/key/VK/modifiers: count +1; seq=15
PASS ArrowLeft keyUp: code/key/VK/modifiers: count +1; seq=16
PASS Ctrl+c keyDown: code/key/VK/modifiers: count +1; seq=17
PASS Ctrl+c keyUp: code/key/VK/modifiers: count +1; seq=18
PASS two simultaneous touch downs and normalized positions: count +2; seq=20
FAIL requested two-touch pid slots 0 and 1: observed=[1,2]; design reserves mouse=0, touch=1..15
PASS documented two-touch pid slots 1 and 2: [1,2]
PASS both touch moves preserve slots and positions: count +2; seq=22
PASS touchEnd one leaves the other active: count +1; seq=23
PASS touchCancel remaining contact: count +1; seq=24
PASS tap text field down: count +1; seq=25
PASS tap text field up: count +1; seq=26
PASS viewer received ime.state and ime.vk, editor focused: focused=true; active=ime
PASS insertText Unicode committed exactly once: count +1; seq=26
PASS IME composition reaches mock without committing: count +1; seq=26
PASS IME composition commit exactly once, composition cleared: count +2; seq=26
PASS final committed field text: "héllo 世界你好"
PASS final pointers empty: {}
PASS binary inputSeq delivered in order without gaps: packets=26; inputSeq=26
PASS viewer pointer/wheel/key events are browser trusted: {"trusted":32,"untrusted":["compositionend"],"hardwareTrusted":26,"hardwareUntrusted":0}
INFO final counts={"pointer":13,"wheel":1,"key":12,"reset":0,"ime":4,"ctl":11}
PASS child process trees cleaned up: taskkill /T /F
RESULT 38 passed, 1 failed
EXIT_CODE=1
```

## Raw output: Chromium 89

```text
RUN 2026-10-25T15:57:01.017Z port=8211 viewer=C:/src/tmp/chrome89/chrome-win/chrome.exe oldHeadless=true
INFO profiles=C:\src\tmp\pt-smoke\run-i3oCVS
PASS control-plane started
INFO mock=Edg/156.0.4314.42 viewer=HeadlessChrome/89.0.4389.0
PASS viewer video 1920 and all data channels open: {"width":1920,"enabled":true,"channels":[{"id":0,"label":"input","state":"open"},{"id":1,"label":"ctl","state":"open"},{"id":2,"label":"probe","state":"open"}]}
PASS mock data channels open and handshake received: [{"label":"input","id":0,"state":"open"},{"label":"ctl","id":1,"state":"open"},{"label":"probe","id":2,"state":"open"}]
PASS letterbox coordinate mapping exercised: {"left":0,"top":90,"width":1280,"height":720,"scale":0.6666666666666666}
PASS mouse move coordinates: count +1; seq=1
PASS left mouse down: count +1; seq=2
PASS left mouse up, no stuck pointer: count +1; seq=3
PASS right mouse down: count +1; seq=4
PASS right mouse up, no stuck pointer: count +1; seq=5
PASS wheel coordinates and scaled deltas: count +1; seq=6
PASS a keyDown: code/key/VK/modifiers: count +1; seq=7
PASS a keyUp: code/key/VK/modifiers: count +1; seq=8
PASS Shift+A keyDown: code/key/VK/modifiers: count +1; seq=9
PASS Shift+A keyUp: code/key/VK/modifiers: count +1; seq=10
PASS Enter keyDown: code/key/VK/modifiers: count +1; seq=11
PASS Enter keyUp: code/key/VK/modifiers: count +1; seq=12
PASS Backspace keyDown: code/key/VK/modifiers: count +1; seq=13
PASS Backspace keyUp: code/key/VK/modifiers: count +1; seq=14
PASS ArrowLeft keyDown: code/key/VK/modifiers: count +1; seq=15
PASS ArrowLeft keyUp: code/key/VK/modifiers: count +1; seq=16
PASS Ctrl+c keyDown: code/key/VK/modifiers: count +1; seq=17
PASS Ctrl+c keyUp: code/key/VK/modifiers: count +1; seq=18
PASS two simultaneous touch downs and normalized positions: count +2; seq=20
FAIL requested two-touch pid slots 0 and 1: observed=[1,2]; design reserves mouse=0, touch=1..15
PASS documented two-touch pid slots 1 and 2: [1,2]
PASS both touch moves preserve slots and positions: count +2; seq=22
PASS touchEnd one leaves the other active: count +1; seq=23
PASS touchCancel remaining contact: count +1; seq=24
PASS tap text field down: count +1; seq=25
PASS tap text field up: count +1; seq=26
PASS viewer received ime.state and ime.vk, editor focused: focused=true; active=ime
PASS insertText Unicode committed exactly once: count +1; seq=26
INFO IME composition unavailable: CDP Input.imeSetComposition method not found
PASS optional IME composition capability handled: unsupported; composition checks skipped
PASS final committed field text: "héllo 世界"
PASS final pointers empty: {}
PASS binary inputSeq delivered in order without gaps: packets=26; inputSeq=26
PASS viewer pointer/wheel/key events are browser trusted: {"trusted":27,"untrusted":[],"hardwareTrusted":26,"hardwareUntrusted":0}
INFO final counts={"pointer":13,"wheel":1,"key":12,"reset":0,"ime":1,"ctl":6}
PASS child process trees cleaned up: taskkill /T /F
RESULT 37 passed, 1 failed
EXIT_CODE=1
```

## Follow-up (orchestrator)
- The touch-slot check expected the pre-review layout (0/1); fixed to 1/2 per 06 section 2.2. Rerun on Edge: **39 passed, 0 failed**.
