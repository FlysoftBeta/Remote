# Task a: prior-art survey
Write: C:\Users\Flysoft\Desktop\remotekit\research\a-prior-art.md

Survey C:\Users\Flysoft\Desktop\chromium (read-only). Start with README.md, docs/prts/STATUS.md, docs/prts/PLAN.md and the
other docs, then patches/ (36 format-patch files against PIN), prtsd/ (Rust daemon), server/, scripts/, cr.cmd, args.gn.
Skip build outputs (target/, node_modules/, out/, any large binaries).

Report:
1. What it is, architecture, Chromium version (PIN commit -> milestone), status: what works, what is WIP/broken, known gaps.
2. Patch-by-patch table: patch file, purpose, touched Chromium files/layers (aura/views/viz/headless/content/chrome),
   approximate size (lines changed), and an assessment: reusable as-is / reusable idea / drop. Group by theme
   (target registry, pipe transport, frame capture CPU + D3D11 keyed texture, input via Aura, touch, popups/flyouts,
   branding/UA, misc).
3. Frame-export design actually used (FrameSinkVideoCapturer? GpuMemoryBuffer? keyed mutex? CPU readback?) with concrete
   classes, and its measured cost/latency if documented. Why it was chosen; what failed.
4. Input design (where injected, touch handling, focus/activation), and whether editable-field focus detection exists.
5. Branding/UA patches: exactly what they change.
6. prtsd daemon design: modules, encoder (QSV/OpenH264 and settings), WebRTC stack (which crate/lib), pacing, measured
   latency if documented. Control-plane and viewer protocol.
7. Patch-maintenance workflow (scripts, cr.cmd, how patches are applied/exported/rebased, checkout layout, args.gn,
   build times if documented).
8. Lessons learned documented in docs (pitfalls, dead ends): the gist, briefly.
9. Recommendation: what the new project should reuse (code, scripts, ideas) and what to redo differently, with reasons.
   Be candid about code quality and complexity (e.g. 36 patches: which could be collapsed).
