# Common context (implementation task; keep it short)
Project "remotekit" (C:\Users\Flysoft\Desktop\remotekit, git main): cloud browser on Windows = patched Chromium 157
(`--headless=new`) -> in-tree C++ `prts_endpoint` (libwebrtc + oneVPL encoder, design/REVIEW.md) -> `control-plane/` (Node).
Prior art (READ-ONLY, never modify, never checkout branches there; use `git -C C:\Users\Flysoft\Desktop\chromium show <rev>:<path>`):
C:\Users\Flysoft\Desktop\chromium. Its summary: research/a-prior-art.md (skim only sections named in the task).
Machine: Windows 11, 32 logical cores, Intel UHD 630 (vendor 0x8086 device 0x9bc5), VS 2026 Community
(C:\Program Files\Microsoft Visual Studio\18\Community, use vcvars64.bat / cargo's MSVC detection), Rust 1.99, Node 24.

Rules:
- Do ONLY your task's deliverables, inside the paths your task allows. Do not touch other files, do not git commit,
  do not touch C:\src\chromium (a Chromium build may be running: keep CPU use modest, e.g. cargo -j 8).
- Create your report file within the first 5 minutes with headings, fill it incrementally (a hard timeout kills you).
- Reports: concise, tables over prose, 60-200 lines; mark [V] measured/verified vs [I] inference.
- Prior art may be referenced only PARTIALLY (the user disagrees with parts of it): never copy wholesale; port only pieces
  you evaluated against our design docs, and say in the report what you took and why.
- Temporary files go ONLY under C:\src\tmp\<task>\ (never C:\src.old).
- No downloads except crates/npm packages strictly required by the task.
- Final message: 5-8 lines summary.
