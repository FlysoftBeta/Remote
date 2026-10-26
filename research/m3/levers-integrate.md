# m3-levers-integrate

Started 2026-10-26. Reviewing GPU recovery and timer/pacer drafts and VPL integration. No commits. Evidence and results will be appended incrementally.

## Files / merge decisions
Integrated reviewed GPU recovery and levers drafts while preserving VPL. Final source files: Chromium chrome/browser/prts/prts_capture.cc, new prts_fake_vsync_clock.h, content/browser/gpu/gpu_process_host.cc, ui/gl/{vsync_thread_win.cc,BUILD.gn,DEPS}; endpoint/browser_session.cc, d3d_frame_source.{cc,h}, main.cc, peer.cc. Some merged endpoint changes entered HEAD through external orchestration during this task; this agent made no commits. See source-status.txt and final diffs.

## Evidence / results
Complete: incremental chrome and endpoint builds, two forced GPU deaths with continuous same-session decode/input recovery, fourteen60s marker measurements (two/config), one60s scrolling measurement, raw JSON/CSV and scoped CPU/GPU/stage statistics. See MEASUREMENTS.md, final-validation.json, recovery-evidence.txt and per-run directories.

## API citations
[V] = checked against Chromium pin headers; [I] = inference.

## Open risks
Remaining: long soak/driver TDR or device removal, multi-adapter migration, recovery during popup/resize/teardown races, power/battery cost, shaped-link pacer A/B, physical scanout. VPL60 startup/rate handling remains inconsistent and the scrolling marker has7.1% unreadable frames. Historical work-log entries below preserve intermediate failures and superseded findings.

## Merge and API review
- Applied both GPU recovery patches via git apply -3 cleanly, preserving existing VPL/native source edits. Levers diffs lack index hashes: git apply -3 fell back to direct application successfully.
- Corrected draft timer: base/time/time_win.cc g_high_res_timer_enabled starts false; EnableHighResolutionTimer(true) is required before Activate. Paired deactivation on shutdown; logs active state. [V] time.h:744-753; time_win.cc:106,262-293.
- Corrected draft static clock exit-time destructor with base::NoDestructor. Moved new clock code into chrome/browser/prts/prts_fake_vsync_clock.h and added small header target. ui/gl hook now guarded by ui::IsPrtsRemoteMode. GPU launch forwards --prts-remote under the same guard. [V] ui_base_switches.h/.cc; gpu_process_host.cc:1392 switch forwarding; ui/gl/BUILD.gn already depends on ui/base on Windows.
- [V] components/viz/host/client_frame_sink_video_capturer.h and cc: constructor callback, Start, StopAndResetConsumer, restart replay and 100ms retry read at pin. [V] base/task/bind_post_task.h repeating closure and task runner contracts.
- Added five-second sender stats snapshots via GetStats and RTCStatsReport::ToJson; cumulative totalPacketSendDelay permits offline interval deltas. [V] rtc_stats_collector_callback.h, rtc_stats_report.h, rtcstats_objects.h.
- Initial compile rejected the draft static object (-Wexit-time-destructors); second incremental Chromium build pending. Python store alias did not execute task script; used Node instead, no source side effects from alias.

- Chromium incremental build passed (build-chrome-4.log). GN assert_no_deps forbids //chrome target dependencies from ui/gl; header is listed as a source of ui/gl instead of a chrome target dependency, keeping new implementation in chrome/browser/prts. Initial header-only constructor/destructor style diagnostic resolved with explicit out-of-class inline defaults. No assertion or layering policy relaxed.
- Measurement plan: two interleaved passes of baseline(no hires), timer, factor2.5 pacing+timer, requested60 default-vsync, precise60/requested60, precise120/requested30, combined precise60/requested60+pacer+timer. VPL remains default, loopback/headless Edge. Extra scrolling run follows.

- recovery-graphite: {"error":"AssertionError [ERR_ASSERTION]: The expression evaluated to a falsy value:\n\n  assert(target)\n"}; evidence recovery-graphite/.

- recovery-ganesh: {"gap":545.5,"resumed":true,"keyframe":true,"input":true}; evidence recovery-ganesh/.

- Endpoint incremental build passed (build-endpoint-3.log). Stats callback must not be final (make_ref_counted supplies ref-count implementation) and single-argument PeerConnection::GetStats takes a raw refcounted callback pointer at pin; corrected to .get(). [V] peer_connection_interface.h:975 and make_ref_counted.h.
- Ganesh forced GPU crash passed: max rVFC frame gap545.5ms, decoded keyframes increased, same connected PeerConnection, remote fixture input counter increments after recovery. Graphite initial startup had no CDP within10s and no capture; excluded, retry underway in separate directory. No GPU death was injected into invalid startup.

- recovery-graphite-retry: {"gap":109.90000000000055,"resumed":true,"keyframe":true,"input":true}; evidence recovery-graphite-retry/.

## Recovery verified (full rVFC trace)
- GraphiteDawnD3D11: injected taskkill /PID on uniquely confirmed out/dev GPU child of this profile browser. Full trace longest viewer gap546.1ms (wall2350590.2->2351136.3 relative to1793000000000); killWall1793002350501. Final decoded frames141->205, keyframes14->19, same connected peer and input counter increments. GaneshGL: gap545.5ms; killWall1793002269698, resumed display1793002270346.7 (~649ms after kill command start), frames138->224, keys10->13. Both comfortably within2s.
- Earlier Graphite provisional109.9ms gap was sampled before the complete recovery trace. Corrected from final trace; use546.1ms. Rate-control reinit emits independent IDRs, so keyframe count alone cannot isolate recovery IDR; browser reset logs and encoder IDR timestamps provide additional evidence. Same sessions continued without renegotiation.
- Graphite screenshot after.png visually reviewed: intact marker, tab strip/page, text and input-probe color. Both backend names read from CDP SystemInfo, hardware ANGLE D3D11 Intel UHD630 driver30.0.100.9805. Detailed process commands, system info, rVFC traces, decoded keyframes, screenshots and logs in recovery-{graphite-retry,ganesh}/.
- Added ui/gl/DEPS narrow include exceptions for the existing guard header and task-local Chrome clock header; no broad dependency policy exception.

- Completed baseline-1: harness exit 0, evidence baseline-1/ (config, report, CSV, chain/Chrome logs, CPU/GPU samples).

- Completed timer-1: harness exit 0, evidence timer-1/ (config, report, CSV, chain/Chrome logs, CPU/GPU samples).

- Completed pacer-1: harness exit 0, evidence pacer-1/ (config, report, CSV, chain/Chrome logs, CPU/GPU samples).

- Completed fps60-1: harness exit 0, evidence fps60-1/ (config, report, CSV, chain/Chrome logs, CPU/GPU samples).

- Important60fps result: default VPL initially initializes AVC level4.2 successfully, but first GCC rate reset rebuild Query returns cap_bytes=0; compatibility rejection triggers existing OpenH264 software fallback. fps60-1 actually measures OpenH264 after fallback (53.02 decoded/source53.03fps). Precise-vsync60 run also falls back and capture reaches59.9fps. This is a discovered encoder limitation, not a native-VPL60 result; no unrelated VPL changes made. Tables now identify actual encoder per run, and pipeline reports preserve fallback evidence. Requested default VPL retained as required.

- Completed vsync60-1: harness exit 0, evidence vsync60-1/ (config, report, CSV, chain/Chrome logs, CPU/GPU samples).

- Completed vsync120-1: harness exit 0, evidence vsync120-1/ (config, report, CSV, chain/Chrome logs, CPU/GPU samples).

- Completed combined-1: harness exit 0, evidence combined-1/ (config, report, CSV, chain/Chrome logs, CPU/GPU samples).

- Completed baseline-2: harness exit 0, evidence baseline-2/ (config, report, CSV, chain/Chrome logs, CPU/GPU samples).

## Additional pin/API checks
- [V] Windows SDK10.0.26100.0/um/synchapi.h:543 SetWaitableTimer HANDLE/LARGE_INTEGER relative due time, :641 CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, :648 CreateWaitableTimerExW. Windows SDK processthreadsapi.h:1217-1229 throttling version1, EXECUTION_SPEED=1 and IGNORE_TIMER_RESOLUTION=4; StateMask0 opts out. base/win/win_util.cc uses the same flags.
- [V] base/no_destructor.h lifetime and static storage pattern checked. [V] api/stats/rtc_stats.h ToJson; peer_connection_interface.h:975 ownership comment checked. Weak session polling stops after Close and asynchronous stats callbacks own no Session reference.
- First-pass precise60 delivered59.92fps (source59.91); default-vsync60 delivered53.02(source53.03). Both actual software fallback. First-pass timer remains VPL and lowered native_vpp median window P50/P95 from9.30/16.30ms to2.15/3.27ms. Pacer per-packet mean queue dwell ~0.0013ms is too small for meaningful gain on this fixture.
- All first-pass7 reports have0 marker decode failures,0 input timeouts,0 >200ms freezes. Full tables/configuration and scoped CPU/GPU costs are generated incrementally in MEASUREMENTS.md; validation.json retains numerical results. Second pass in progress.

- Completed timer-2: harness exit 0, evidence timer-2/ (config, report, CSV, chain/Chrome logs, CPU/GPU samples).

- Refined60fps encoder finding: combined-1 falls back during initial1920x1080 startup, then viewport resize reinitializes VPL at1890x938 with inherited software start/min/max2000/2000/12000kbps; measurement is nativeVPL60 (59.82 decoded/source60.00). Previous fps60-1/vsync60-1 initialize directly at1890x938 start1885/min30/max3608 and rate reset below~2Mbps rejects cap_bytes=0, remaining software. Thus nativeVPL60 IS possible, but startup/bitrate-dependent selection prevents a clean combined-vs-vsync pacer attribution. All per-run effective bitrates/actual encoder are in tables; do not claim combined improvement is caused by pacing. Recommend retaining conservative30fps default unless repeats resolve the instability.


- Completed pacer-2: harness exit 0, evidence pacer-2/ (config, report, CSV, chain/Chrome logs, CPU/GPU samples).

- Completed fps60-2: harness exit 0, evidence fps60-2/ (config, report, CSV, chain/Chrome logs, CPU/GPU samples).

## Provisional default decisions (two timer and pacer runs complete)
- Keep high-resolution endpoint timer ON by default (draft switch --no-hires-timer remains A/B opt-out). Timer captureP50 59.45/60.05ms vs no-timer84.27/64.38; inputP50 67.6/68.3 vs95.7/81.4. NativeVPP rolling-window median P50 near2.15ms vs9-10ms. Baseline run variation is large; do not promise a fixed25ms gain.
- Keep30fps and no injected fake-vsync by default. Precise60 works, but current VPL driver startup/rate path inconsistently falls back, and GPU cost rises substantially. Keep pacer trials opt-in: no repeatable marker benefit and steady-state packet dwell is only microseconds.
- Updated main.cc stale pre-VPL fps comment to explain measured60fps instability. Narrowed final sender stats logger to outbound RTP only (same five-second cadence/metrics); avoids writing certificates/candidates to ordinary logs. Final rebuild+scroll test will validate this diagnostic-only change. Matrix uses earlier full-stat logger; media settings remain identical.
- Branch history advanced externally during task (current remotekit HEAD b6ac968 includes previously staged recovery/levers portions). This agent made NO commits, resets, stashes or rebases. Chromium recovery and initial levers patches remain staged from git apply -3; final working-tree changes must be reviewed with git diff HEAD, not cached diff alone.

- Completed vsync60-2: harness exit 0, evidence vsync60-2/ (config, report, CSV, chain/Chrome logs, CPU/GPU samples).

- Completed vsync120-2: harness exit 0, evidence vsync120-2/ (config, report, CSV, chain/Chrome logs, CPU/GPU samples).

- Completed combined-2: harness exit 0, evidence combined-2/ (config, report, CSV, chain/Chrome logs, CPU/GPU samples).

- All14 formal60s loopback marker runs completed (two per7 configs). Final incremental chrome+endpoint build started at16:30SGT, concurrency4, after matrix exited. Changes since matrix binary: header whitespace, source Chromium formatting/fps comment, outbound-only five-second stats logging. Media/recovery/timer/pacer behavior unchanged. Scrolling scenario S will run on final binary.

- Final incremental chrome+endpoint build succeeded (build-final.log, finished successfully). Scrolling60s run started on this final binary with default VPL30 + hires timer and no pacer/vsync switches. No full build, gn clean, full suite or commits.

- Completed scroll-1: harness exit 0, evidence scroll-1/ (config, report, CSV, chain/Chrome logs, CPU/GPU samples).

## Final results and defaults (16:35 SGT)
- Recovery: GraphiteDawnD3D11 gap546.1ms and resumed display635.3ms after taskkill began; GaneshGL gap545.5ms and resumed display648.7ms after taskkill. Same connected peer, increasing decoded frames/keyframes, and post-recovery input succeeded. Graphite reset16:12:30.737 -> VPL SPS/PPS/IDR16:12:31.132; Ganesh browser re-establish16:11:09.947. This fixes capture loss after GPU-process restart, not the underlying Graphite cache assertion. No default Graphite/Ganesh policy change.
- Final native-VPL combined60 runs:59.82/59.97 decoded fps; capture P50/P80/P95/P99 43.90/48.54/51.50/53.03 and43.83/48.31/51.45/52.53ms. Input44.0/49.3/53.4/56.2 and43.7/52.0/56.2/64.8ms. Endpoint CPU0.09/0.12 cores; Chrome0.46/0.47; Edge0.69/0.72; busiest sampled GPU engine mean60.27/60.36%. These runs had pre-measurement fallback then hardware retry and software-inherited bitrate limits; do not attribute this improvement to the pacer trial alone.
- Requested60 without fake-vsync:52.83-53.02 decoded fps, source53.03-53.27; one native run and one software fallback. Precise60 without pacer:59.92/59.95fps, both OpenH264 fallback. Thus the vsync clock rate fix is repeatable across actual encoders. Software precise60 costs0.40-0.45 endpoint cores and busiest engine mean73.18-74.27%; native combined60 is cheaper but not reliably selected at identical nominal VPL defaults.
- Default decisions encoded/retained in endpoint/main.cc: VPL remains default; hires timer ON (correct Enable+Activate pair, shutdown release), fps30 retained, field trials empty unless explicitly supplied. No default fake-vsync. Two timer runs capture P50 59.45/60.05ms vs no-timer84.27/64.38; input67.6/68.3 vs95.7/81.4. Native VPP P50 about2.15ms with timer versus9-10ms without. End-to-end gain varies with baseline conditions.
- Pacer factor2.5 + upper-capacity guard stays opt-in: two30fps native runs have capture P50 60.02/59.52ms versus timer-only59.45/60.05; no persuasive improvement. Per-packet mean sender queue dwell about0.0013ms. No shaped-link or factors2.0/3.0 A/B in this task. Precise120 at30fps has variable tail benefit and raises Chrome CPU to~0.81-0.82 cores and busiest engine72-78%; keep opt-in.
- Scrolling S on final binary: default VPL30 + hires,29.08 decoded/source29.62fps; capture79.57/84.53/89.24/150.24ms and input90.2/102.5/111.7/183.5ms (110 samples,0 timeouts). 123/1734 markers unreadable (7.1%); latency distribution is conditional on1611 decoded markers. No >200ms viewer freezes. Scenario updates497 and scrollTop6334 recorded via remote CDP; remote-page.png visually checked with article/images/probe intact. This is source-page visual evidence, not a decoded-viewer screenshot.
- All14 marker runs:0 marker decode failures,0 input timeouts,0 >200ms freezes,>1500 capture samples/run,119/120 input samples/run. Full P50/P80/P95/P99, stages, configuration/bitrate/backend blocks and CPU/GPU tables follow in MEASUREMENTS.md. Stage summaries are medians of five-second rolling quantiles, not merged full-run per-frame quantiles. GPU sampling is per-engine utilization; Edge CPU may include unrelated Edge processes. rVFC is compositor display estimate, not physical scanout.
- Final build-final.log succeeded; build-final-status.log is an up-to-date incremental check with NATIVE_EXIT=0. No-op status check overlapped early scrolling startup; no compiler/link work ran then. Final-validation.json passed. First validator attempt misread PowerShell UTF16 build log as UTF8; parser corrected, no media/report mutation. No full builds, gn clean, suites or commits. Test CP/endpoint/remote Chrome stopped.
- Final diagnostics differ from matrix binary only by outbound-only sender JSON filtering plus formatting/comment changes; scrolling tests final binary. No media settings changed after marker matrix.

## Deliverables and review notes
- MEASUREMENTS.md: complete15-run latency/FPS, stage, CPU/GPU, packet dwell, pipeline/bitrate and path-count tables. validation.json: numerical stage/CPU summaries. final-validation.json: assertions across all runs/recovery/build. Per-run latency-*.json and CSV are untouched raw measurements with pipeline blocks.
- recovery-{graphite-retry,ganesh}/: GPU PID/parent command verification, actual CDP backend/adapter info, rVFC traces, decoded keyframe/frame counts, screenshots and chain/Chrome logs. recovery-graphite/ is the invalid initial startup attempt, excluded.
- chromium-final.diff includes full selected-file HEAD diff plus new clock header; remotekit-final.diff is against current HEAD b6ac968 (which already contains externally committed merged recovery/levers/VPL changes). Original drafts remain at the user-provided paths. source-status.txt records dirty/staged state; review Chromium with git diff HEAD because git apply -3 left earlier patch contents staged. No index cleanup/reset was performed.
- The header-only clock implementation lives under chrome/browser/prts as required and is listed in ui/gl sources with narrow DEPS exceptions. A Chrome target dependency was rejected by GN assert_no_deps and removed; no GN assertion was weakened. This file-placement/layering choice merits normal orchestrator review.
- [I] The large VPL30 tails correspond to frequent rate rebuilds/IDRs (~100/60s) and CPU bridge frames. Low bitrate60fps query cap rejection and startup viewport/bitrate state are the follow-up encoder issues; no VPL implementation edits were made here.
- Unverified: physical GPU device loss/TDR, LUID change, software-compositor fallback, repeated/soak GPU crashes, popup/resize/retry teardown races, real scanout, release build, network-shaped pacer behavior, battery/power cost, and high-motion marker reliability/selection bias.

- Final review checks: both final diffs pass git apply --reverse --check against the final working trees. Chromium staged/unstaged and remotekit whitespace checks pass with cr-at-eol (preserving existing Windows line endings). No further source edits after final build/scroll validation. Completed16:37SGT,2026-10-26.
