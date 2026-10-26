# Common context (Stage 5 / M4 latency tuning; keep it short)
Project "remotekit" (C:\Users\Flysoft\Desktop\remotekit, git main): cloud browser on Windows = patched Chromium 157.0.8103.15
(`--headless=new --prts-remote`) -> viz FrameSinkVideoCapturer (D3D11 keyed-mutex shared textures) -> Mojo -> in-tree C++ `prts_endpoint`
(oneVPL zero-copy encoder `endpoint/vpl_encoder.*`, OpenH264 fallback, libwebrtc) -> WebRTC -> `viewer/` (served by `control-plane/`).
State and numbers: design/STATUS.md (section "M3 handoff"), research/m3/stage-table.md, research/m3/trace.md, tools/latency/PRESETS.md.
Chromium checkout: C:\src\chromium\src, branch `prts` (our patches are commits on top of the pin). `C:\src\chromium\src\prts_endpoint` is a symlink to
remotekit\endpoint. Out dir for experiments: out\dev (component, dcheck). out\rel is being built in the background by the orchestrator: never touch it,
never kill siso/ninja/clang processes that belong to out\rel.

Scripts (Git Bash): `bash /c/src/tmp/m1/kill.sh` (kill test chrome/endpoint), `bash /c/src/tmp/m1/be.sh` (kill + incremental build of prts_endpoint),
`bash /c/src/tmp/m1/be.sh chrome` (kill + incremental build of chrome). Latency runs: `node tools/latency/run.mjs --preset m3-combined --scenario marker|S|V|G
--port 8300 --secs 60 [--netem d25j5-c12] [--remote-cdp 9333]` with ENDPOINT_PATH/ENDPOINT_ARGS/PTRACK_TMP set as in tools/latency/PRESETS.md (every report
JSON has a `pipeline` block; P50/P80/P95/P99 for capture->display and input->flip; PRTS_TRACE=1 per-stage trace, research/m3/trace.md).
Never run gn clean, full builds or test suites. Never use C:\src.old. Never touch out\rel.

Rules:
- Do ONLY your task's deliverables. Notes/evidence/drafts in your task dir C:\src\tmp\m4\<task>\ ; create NOTES.md there within 5 minutes and fill it
  incrementally (a hard timeout kills you). NOTES: what you did, files changed, evidence (log lines, numbers), API citations [V]=read at pin / [I]=inference,
  open risks. No git commits (the orchestrator reviews and commits).
- Chromium C++ style; upstream hook lines get a `// PRTS:` comment and are guarded by `ui::IsPrtsRemoteMode()` (or a PRTS switch); minimal changes.
- Treat run-to-run differences below ~20 ms as noise unless repeated (capture phase locks per run).
- Final message: 5-8 lines summary (results, files changed, what is unverified).
