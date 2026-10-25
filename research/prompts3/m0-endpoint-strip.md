# Task M0-strip: remove prior-art leftovers from endpoint/ and control-plane/ (keep both building)
Allowed paths: `endpoint/**`, `control-plane/**`, report `research/m0-endpoint-strip.md`. Nothing else.
Context: endpoint/ and control-plane/ are fresh copies of prior-art prtsd/ and server/. research/a-prior-art.md sections on
prtsd/server (search for "v1", "browser.rs", "browser_wire", "prts-autopilot", "Drop") list what is obsolete. M1 will replace the
browser<->endpoint protocol later, so do NOT redesign anything; only delete dead weight.
Do:
1. Remove the legacy browser protocol v1 (envelope major version 1, page-only capture/input, the v1->v2 window/target projection,
   `PRTS_BROWSER_PROTOCOL` switching): v2 becomes the only browser protocol. Keep window/tab/layer/scene (v2) code, transport.rs,
   d3d*.rs, mf_encoder.rs, encoder.rs fallback chain, viewer.rs, shaping.rs, the Synthetic frame source, browser.rs launcher/Job.
2. Delete the prior milestone harness binaries `endpoint/src/bin/browser_m*.rs` and `src/bin/support` unless still needed by a kept
   test; delete tests/e2e files that only exercise v1 (both crates). Remove any prts-autopilot / journal / orchestration references.
3. Keep `cargo build` and `cargo test --no-run` green in endpoint/ (use `-j 8`; a Chromium build may be running) and
   `node --test test/unit.test.js` green in control-plane/. Do not rename the crate or binary (prtsd) in this task.
Report: files deleted/changed with line counts (`git diff --stat` style), what v1 code remained and why, build/test results.
