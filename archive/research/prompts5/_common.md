# Common context (Stage 3 / M1 drafting task; keep it short)
Project "remotekit" (C:\Users\Flysoft\Desktop\remotekit, git main): cloud browser on Windows = patched Chromium 157.0.8103.15
(`--headless=new --prts-remote`) -> in-tree C++ `prts_endpoint` (libwebrtc) -> WebRTC -> `viewer/` (served by `control-plane/`).
Design: design/01-architecture.md (D3 IPC, D4/D4b capture, D7/D8 input, D9 IME, D11 branding, D12 endpoint), design/02-browser-patches.md
(patch list 01-14 + section 2), design/06-protocol.md (signalling, DataChannels, binary input format), design/03-video-pipeline.md.
Shared Mojo contract (draft, the interface between browser and endpoint): C:\src\tmp\m1\contract\prts.mojom.

Chromium checkout C:\src\chromium\src at the pin: STRICTLY READ-ONLY (a full build is running in out\dev). Never write, never
`git checkout/apply/stash/worktree` there, never run gn/ninja/siso there. Allowed read-only commands: grep/rg, cat, `git -C C:\src\chromium\src show/log/grep`,
and `git -C C:\src\chromium\src apply --check <diff>` to validate a patch. Keep CPU use modest.
To produce a diff: copy the upstream file into your task dir under the same relative path (e.g. C:\src\tmp\m1\<task>\a\ui\base\x.cc and ...\b\ui\base\x.cc),
edit the b copy, then build a git-style diff with paths a/<rel> b/<rel> (`git diff --no-index` and fix the path prefixes), and verify it with
`git -C C:\src\chromium\src apply --check`.

Rules:
- Do ONLY your task's deliverables, inside your task's output dir under C:\src\tmp\m1\. Do not touch the remotekit repo, do not git commit.
- Code style: Chromium C++ (base/, mojo, Views/Aura idioms, `// PRTS:` comment on every upstream hook line group,
  every upstream hook guarded by the runtime switch `ui::IsPrtsRemoteMode()` declared in ui/base/ui_base_switches.h by patch 01).
  It must COMPILE against the pin: check every API you call by reading its header at the pin (cite file:line in NOTES).
- Prior art C:\Users\Flysoft\Desktop\chromium (read-only, `git -C ... show <rev>:<path>`) may be looked at for hints only; do not copy wholesale.
- Create NOTES.md in your output dir within the first 5 minutes (headings), fill it incrementally (a hard timeout kills you).
  NOTES: file list, API citations [V]=read at pin / [I]=inference, open risks, exact integration steps (BUILD.gn deps, where hooks go).
- Final message: 5-8 lines summary.
