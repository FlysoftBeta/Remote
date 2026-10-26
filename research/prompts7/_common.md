# Common context (Stage 4 / M2; keep it short)
Project "remotekit" (C:\Users\Flysoft\Desktop\remotekit, git main): cloud browser on Windows = patched Chromium 157.0.8103.15
(`--headless=new --prts-remote`) -> in-tree C++ `prts_endpoint` (libwebrtc, OpenH264 for now) -> WebRTC -> `viewer/` (served by `control-plane/`).
M1 works end to end (30 fps, input, popups). Design: design/01-architecture.md, design/02-browser-patches.md (patch list), design/06-protocol.md
(signalling, DataChannels, ctl JSON messages incl. resize/cursor/ime, binary input), design/STATUS.md (current state, scripts).
Chromium checkout: C:\src\chromium\src, branch `prts` (our patches are commits on top of the pin; Mojo contract in chrome/browser/prts/mojom/).
`C:\src\chromium\src\prts_endpoint` is a symlink to remotekit\endpoint. Out dir: out\dev (component, dcheck).

Scripts (Git Bash): `bash /c/src/tmp/m1/kill.sh` (kill test chrome/endpoint), `bash /c/src/tmp/m1/be.sh` (kill + incremental build of prts_endpoint) /
`bash /c/src/tmp/m1/be.sh chrome` (kill + incremental build of chrome), `bash /c/src/tmp/m1/cp.sh 8300 --url=<u> --cr-enable-logging [--cr-<switch>]`
(control-plane + endpoint + PRTS chrome; chrome log C:\src\tmp\m1\prof-8300\chrome_debug.log), `bash /c/src/tmp/m1/test.sh <secs> [args]` (restart chain +
latency harness `node tools/latency/run.mjs --no-mock`). Viewer: http://localhost:8300/ in headless Edge
("C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe" --headless=new --remote-debugging-port=<p>); drive it with CDP (examples:
C:\src\tmp\m1\debug-frames\run-aligned.mjs, C:\src\tmp\m1\input-validate\*.mjs). Never run gn clean, full builds or test suites. Never use C:\src.old.

Rules:
- Do ONLY your task's deliverables. Notes/evidence/drafts in your task dir C:\src\tmp\m2\<task>\ ; create NOTES.md there within 5 minutes and fill it
  incrementally (a hard timeout kills you). NOTES: what you did, files changed, evidence (log lines, screenshots), API citations [V]=read at pin / [I]=inference,
  open risks. No git commits (the orchestrator reviews and commits). Keep CPU use modest.
- Chromium C++ style; every upstream hook line group gets a `// PRTS:` comment and is guarded by `ui::IsPrtsRemoteMode()`; new PRTS code lives in
  chrome/browser/prts/. Check every API you call against its header at the pin. Minimal, non-intrusive upstream changes.
- Final message: 5-8 lines summary (results, files changed, what is unverified).
