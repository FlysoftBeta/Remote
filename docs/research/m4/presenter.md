# M4 presenter experiments (Chrome 89)

2026-10-27. Completed 20 comparison runs and four separate diagnostic traces; best effort software results only; evidence under `C:/src/tmp/m4/presenter/`. Viewer defaults remain unchanged. No builds or commits requested; out/rel is untouched except setting background build processes to Idle before each run.

## Environment and scope

Genuine Chrome 89.0.4389.90, headed, oneVPL sender, m3-combined preset, 60 s per run plus existing 3 s warmup. Same-session interleaving: control, kiosk, clean, canvas, repeated three times for loopback marker and twice for d25j5-c12 S. Each `build.json` records clang-cl running/count and Idle priority before launch.

**The physical-display requirement is unverified.** `query session` shows this agent in disconnected Flysoft session 2; console session 1 is separate. Chrome reports visible/focused, screen 1680x1050, DPR1; WMI reports 1024x768 at60 Hz. Those observations cannot establish an attached physical 60 Hz monitor or scanout in the viewer's session. User notified during work. Do not present this matrix as real glass measurements.

Control is the current viewer with overlaid stats. Kiosk adds only `--kiosk`, fills 1680x1050, and retains the current DOM (the user allowed a kiosk alternative to the Fullscreen API). Clean hides stats/status/keyboard/IME elements and rounds the video box down to even physical pixels; on this desktop its 1684x938 video is 1:1 at DPR1, versus control1684x940 scaled into1684x939. There is no baseline video CSS transform or opacity to remove; the status label has a transform. Canvas includes the same cleanup, adds a native-size canvas above the video, and calls `getContext('2d', {desynchronized:true})`. Runtime `getContextAttributes()` confirms desynchronized=true. Kiosk changes source dimensions (1680x1050), so it is a practical layout comparison, not a fixed-resolution isolation of presentation alone.

Both marker and input colour sampling in canvas mode use the visible canvas **after** `drawImage(video)` in the same rVFC. Input remains the normal binary DataChannel keydown/up every500ms and detects the fixture's same right-side colour pixel, using the same thresholds and readback as control. This is an input-to-canvas-pixel callback observation, not input-to-photon. Canvas readback can itself synchronize GPU work; retain it for comparability with the existing pixel method. The video stays beneath the opaque full-frame canvas to preserve rVFC scheduling and input coordinate geometry. No canvas presentation feedback is available: glass/decoded numbers still use the source video's rVFC timestamps and are labelled accordingly.

## Measurement truth

Capture starts at the embedded source page draw, as in M3, rather than capture_end. Primary `latDecoded` / `i2decoded` retain the requested rVFC `presentationTime` endpoint. It is decoded-frame installation/submission for composition, not the decoder's exact internal completion instant. Legacy `lat` / `i2display` use `expectedDisplayTime`; old `i2p` is callback-time colour-flip detection. Canvas output does not acquire a new expectedDisplayTime: source-video metadata cannot measure its additional draw/compositor/scanout delay. Thus a canvas glass-metric improvement alone cannot demonstrate a presentation gain.

[V M89 source] `api-source/video_frame_metadata.idl` and `rvfc_impl.txt` in the prior Chrome89 task define/map both timestamps separately. `media/blink/video_frame_compositor.cc:166-175` installs the frame and stores NowTicks independently of supplied expected display time. Its PaintSingleFrame path passes NowTicks as expected time (238), supporting a possible explanation for equality [I], without proving the observed runtime uses that path. [Pinned rVFC implementation](https://chromium.googlesource.com/chromium/src/+/89.0.4389.90/third_party/blink/renderer/modules/video_rvfc/video_frame_callback_requester_impl.cc), [metadata IDL](https://chromium.googlesource.com/chromium/src/+/89.0.4389.90/third_party/blink/renderer/modules/video_rvfc/video_frame_metadata.idl).

[V M89 source fetched into this task] `html_canvas_element.cc:380-391,746-748` creates a separate resource dispatcher for desynchronized contexts; `canvas_rendering_context_2d.cc:1132` reports its low-latency state via getContextAttributes. This confirms the requested mode exists and the runtime reports it enabled; it does not prove scanout bypasses DWM. [Pinned canvas implementation](https://chromium.googlesource.com/chromium/src/+/89.0.4389.90/third_party/blink/renderer/core/html/canvas/html_canvas_element.cc).

## GPU and presentation evidence

Four short CDP traces (categories `gpu,viz,cc,media,benchmark,disabled-by-default-gpu.service`) and `chrome://gpu` text are saved under `diagnostic-{control,fullscreen,clean,canvas}`. Each requested six seconds; decode-event spans are approximately6.0s except clean7.6s, so raw event counts are not normalized rates. These are outside the 20 comparison rows. Each diagnostic trace contains GPU-process `DXVAVideoDecodeAccelerator::Decode`/`DoDecode` calls: actual hardware DXVA decoding is confirmed for those runs. All matrix rows used the same browser/flags/decoder adapter; hardware use there is supported by these diagnostic repetitions [I], not independently traced per measurement. `chrome://gpu` reports hardware video decode/compositing, Direct composition=true, Supports overlays=false, NV12/YUY2=NONE.

No video quad promotion was observed in any diagnostic: **all DCLayerTree commits have num_pending_overlays=0**, and no `SwapChainPresenter::PresentToSwapChain`/decode-swap-chain video events occur. This is stronger than capability reporting alone. Clean-mode computed styles confirm overlays/status/IME/keyboard all display:none and the video has opacity1/transform:none; canvas screenshots/layout confirm the visible canvas covers the video at native dimensions. Hardware overlay behavior on the connected physical console remains unverified.

PresentMon was not found on PATH or the checked conventional installation paths; nothing was downloaded. The trace provides `DirectCompositionChildSurfaceWin::PresentSwapChain` (interval=1) and platform-feedback timing. [V M89] `cc/metrics/compositor_frame_reporter.cc:1201-1203` defines `SwapEndToPresentationCompositorFrame` as swap-end→`presentation_feedback.timestamp`; this is a useful separate platform presentation estimate. These events lack hardware-clock/scanout flags and RTP-frame correlation, and come from a disconnected desktop, so they cannot establish physical scanout or be added to/subtracted from end-to-end quantiles. The frame-feedback pairs are compositor reports, not a one-to-one count of unique video frames. [Pinned frame reporter](https://chromium.googlesource.com/chromium/src/+/89.0.4389.90/cc/metrics/compositor_frame_reporter.cc).

| Diagnostic | DXVA Decode calls | DCLayer commits / pending overlays | DrawAndSwap CPU duration P50/P80/P99 ms | swap-end→platform feedback P50/P80/P99 ms (pairs) | build running |
|---|---:|---|---|---|---|
| control | 362 | 237 / 0 | 0.48/0.58/1.01 | 18.49/28.17/28.68 (42) | yes (16, Idle) |
| fullscreen | 362 | 229 / 0 | 0.40/0.60/1.03 | 19.78/28.19/29.54 (44) | yes (16, Idle) |
| clean | 457 | 294 / 0 | 0.48/0.60/1.09 | 21.70/29.10/30.12 (44) | yes (16, Idle) |
| canvas | 361 | 194 / 0 | 0.38/0.50/0.77 | 29.45/29.77/30.08 (26) | yes (16, Idle) |

DrawAndSwap durations are CPU work, and API Present duration is submission/blocking time; neither is present→scanout. The roughly 28–32 ms typical DrawAndSwap spacing here also does not establish a physical 60Hz presentation path. M89 rVFC presentation→expected-display P50/P80/P95/P99 is zero in **all 20** comparison reports, while these platform-feedback intervals are nonzero. Thus the old 17–24 ms stage-19 estimate cannot be optimized or verified from this M89 rVFC difference alone.

## Results

All values P50/P80/P99 ms. Build column is clang-cl count at batch start, set Idle.

| Run | capture→decoded | input→decoded | capture→display glass | input→display glass | input→flip callback | build running |
|---|---|---|---|---|---|---|
| d25-control-r1 | 72.5/82.7/118.3 | 146.2/161.5/217.9 | 72.5/82.7/118.3 | 146.2/161.5/217.9 | 166.3/182.0/234.1 | yes (16) |
| d25-fullscreen-r1 | 59.0/68.2/97.5 | 139.1/146.5/175.4 | 59.0/68.2/97.5 | 139.1/146.5/175.4 | 163.0/170.0/199.8 | yes (16) |
| d25-clean-r1 | 76.6/84.8/109.9 | 145.3/164.5/210.1 | 76.6/84.8/109.9 | 145.3/164.5/210.1 | 167.7/185.7/236.0 | yes (16) |
| d25-canvas-r1 | 76.5/81.9/110.5 | 128.2/135.5/216.8 | 76.5/81.9/110.5 | 128.2/135.5/216.8 | 146.7/153.2/234.0 | yes (16) |
| d25-control-r2 | 58.9/68.5/94.8 | 131.2/146.2/259.6 | 58.9/68.5/94.8 | 131.2/146.2/259.6 | 155.9/166.8/278.3 | yes (16) |
| d25-fullscreen-r2 | 57.9/67.3/118.8 | 140.7/150.5/217.0 | 57.9/67.3/118.8 | 140.7/150.5/217.0 | 164.0/174.6/251.6 | yes (16) |
| d25-clean-r2 | 62.2/71.1/100.1 | 128.0/144.8/247.6 | 62.2/71.1/100.1 | 128.0/144.8/247.6 | 150.2/165.3/264.9 | yes (16) |
| d25-canvas-r2 | 88.5/93.7/125.3 | 138.6/158.0/254.1 | 88.5/93.7/125.3 | 138.6/158.0/254.1 | 155.8/172.5/265.7 | yes (16) |
| loop-control-r1 | 17.0/22.9/34.8 | 62.0/66.2/80.1 | 17.0/22.9/34.8 | 62.0/66.2/80.1 | 82.6/86.5/99.4 | yes (16) |
| loop-fullscreen-r1 | 16.8/19.5/31.8 | 61.4/66.9/79.3 | 16.8/19.5/31.8 | 61.4/66.9/79.3 | 82.8/88.9/103.0 | yes (16) |
| loop-clean-r1 | 17.1/23.2/35.4 | 61.8/64.2/80.4 | 17.1/23.2/35.4 | 61.8/64.2/80.4 | 82.3/84.0/100.3 | yes (16) |
| loop-canvas-r1 | 32.1/33.1/35.8 | 45.8/61.8/68.1 | 32.1/33.1/35.8 | 45.8/61.8/68.1 | 61.0/79.8/85.0 | yes (16) |
| loop-control-r2 | 17.1/23.3/35.0 | 62.3/66.8/81.4 | 17.1/23.3/35.0 | 62.3/66.8/81.4 | 82.9/87.3/100.1 | yes (16) |
| loop-fullscreen-r2 | 16.7/19.4/32.8 | 62.0/68.0/82.4 | 16.7/19.4/32.8 | 62.0/68.0/82.4 | 82.6/89.2/104.4 | yes (16) |
| loop-clean-r2 | 17.1/25.9/35.8 | 62.5/65.3/80.9 | 17.1/25.9/35.8 | 62.5/65.3/80.9 | 82.8/87.2/101.2 | yes (16) |
| loop-canvas-r2 | 32.7/33.8/37.3 | 64.5/66.1/67.7 | 32.7/33.8/37.3 | 64.5/66.1/67.7 | 80.1/82.4/84.4 | yes (16) |
| loop-control-r3 | 16.8/21.7/34.2 | 63.2/67.2/79.4 | 16.8/21.7/34.2 | 63.2/67.2/79.4 | 82.8/87.7/102.1 | yes (16) |
| loop-fullscreen-r3 | 16.6/19.2/33.0 | 62.4/67.2/82.6 | 16.6/19.2/33.0 | 62.4/67.2/82.6 | 83.0/90.2/100.4 | yes (16) |
| loop-clean-r3 | 17.1/29.8/34.4 | 63.0/68.6/80.9 | 17.1/29.8/34.4 | 63.0/68.6/80.9 | 83.6/91.9/101.4 | yes (16) |
| loop-canvas-r3 | 32.2/33.0/35.9 | 62.7/64.0/67.9 | 32.2/33.0/35.9 | 62.7/64.0/67.9 | 78.8/81.7/88.9 | yes (16) |

Gains below are arithmetic means of paired control-minus-option run quantiles, **not pooled sample quantiles**. Positive values mean lower latency than control; <~20 ms requires repeat-backed evidence and remains noise here.

| Network | option (pairs) | capture decoded gain | input decoded gain | capture glass gain | input glass gain | input flip gain | build running |
|---|---|---|---|---|---|---|---|
| loop | fullscreen (3) | 0.3/3.3/2.1 | 0.6/-0.7/-1.1 | 0.3/3.3/2.1 | 0.6/-0.7/-1.1 | 0.0/-2.3/-2.1 | yes |
| loop | clean (3) | -0.1/-3.7/-0.5 | 0.1/0.7/-0.4 | -0.1/-3.7/-0.5 | 0.1/0.7/-0.4 | -0.1/-0.6/-0.4 | yes |
| loop | canvas (3) | -15.4/-10.6/-1.6 | 4.9/2.7/12.4 | -15.4/-10.6/-1.6 | 4.9/2.7/12.4 | 9.5/5.8/14.4 | yes |
| d25 | fullscreen (2) | 7.2/7.8/-1.6 | -1.2/5.4/42.5 | 7.2/7.8/-1.6 | -1.2/5.4/42.5 | -2.5/2.1/30.5 | yes |
| d25 | clean (2) | -3.7/-2.4/1.6 | 2.1/-0.8/9.9 | -3.7/-2.4/1.6 | 2.1/-0.8/9.9 | 2.1/-1.1/5.8 | yes |
| d25 | canvas (2) | -16.8/-12.2/-11.3 | 5.3/7.1/3.3 | -16.8/-12.2/-11.3 | 5.3/7.1/3.3 | 9.8/11.5/6.4 | yes |

| Run | capture samples / failed reads | input samples / attempts / timeouts | decoded FPS mean | rVFC FPS | dimensions | overlay promoted | build running |
|---|---|---|---|---|---|---|---|
| d25-control-r1 | 1527 / 171 | 104 / 104 / 0 | 55.86 | 28.57 | 1684×940 | no hardware overlay support | yes |
| d25-fullscreen-r1 | 1416 / 172 | 103 / 104 / 0 | 55.77 | 26.78 | 1680×1050 | no hardware overlay support | yes |
| d25-clean-r1 | 1503 / 182 | 103 / 104 / 0 | 55.86 | 28.37 | 1684×938 | no hardware overlay support | yes |
| d25-canvas-r1 | 1641 / 180 | 104 / 104 / 0 | 55.91 | 30.37 | 1684×938 | no hardware overlay support | yes |
| d25-control-r2 | 1464 / 169 | 105 / 105 / 0 | 55.92 | 27.53 | 1684×940 | no hardware overlay support | yes |
| d25-fullscreen-r2 | 1367 / 170 | 104 / 104 / 0 | 55.67 | 25.97 | 1680×1050 | no hardware overlay support | yes |
| d25-clean-r2 | 1508 / 171 | 105 / 105 / 0 | 55.53 | 28.35 | 1684×938 | no hardware overlay support | yes |
| d25-canvas-r2 | 1619 / 172 | 103 / 103 / 0 | 55.88 | 29.85 | 1684×938 | no hardware overlay support | yes |
| loop-control-r1 | 1787 / 0 | 120 / 120 / 0 | 60.10 | 29.78 | 1684×940 | no hardware overlay support | yes |
| loop-fullscreen-r1 | 1677 / 0 | 120 / 120 / 0 | 60.02 | 27.95 | 1680×1050 | no hardware overlay support | yes |
| loop-clean-r1 | 1807 / 0 | 120 / 120 / 0 | 60.00 | 30.13 | 1684×938 | no hardware overlay support | yes |
| loop-canvas-r1 | 1937 / 0 | 120 / 120 / 0 | 60.01 | 32.28 | 1684×938 | no hardware overlay support | yes |
| loop-control-r2 | 1800 / 0 | 119 / 120 / 0 | 60.01 | 30.00 | 1684×940 | no hardware overlay support | yes |
| loop-fullscreen-r2 | 1708 / 0 | 120 / 120 / 0 | 60.08 | 28.47 | 1680×1050 | no hardware overlay support | yes |
| loop-clean-r2 | 1768 / 0 | 120 / 120 / 0 | 60.05 | 29.47 | 1684×938 | no hardware overlay support | yes |
| loop-canvas-r2 | 1931 / 0 | 119 / 120 / 0 | 60.00 | 32.18 | 1684×938 | no hardware overlay support | yes |
| loop-control-r3 | 1755 / 0 | 119 / 120 / 0 | 60.08 | 29.25 | 1684×940 | no hardware overlay support | yes |
| loop-fullscreen-r3 | 1690 / 0 | 120 / 120 / 0 | 60.07 | 28.17 | 1680×1050 | no hardware overlay support | yes |
| loop-clean-r3 | 1728 / 0 | 119 / 120 / 0 | 59.86 | 28.80 | 1684×938 | no hardware overlay support | yes |
| loop-canvas-r3 | 1929 / 0 | 120 / 120 / 0 | 60.01 | 32.15 | 1684×938 | no hardware overlay support | yes |

Capture quantiles condition on valid unique pixel markers. Loopback has no failed reads; shaped S has failed reads retained in the table, so its capture tail is conditional. Input-colour detection is independent of marker validity. No input timeouts occurred.

| Shaped run | NACK / lost | downlink queue drops | >200 ms rVFC gaps | mean sender Mbps | build running |
|---|---|---:|---:|---:|---|
| d25-control-r1 | 29/89 | 61 | 0 | 8.28 | yes (16, Idle) |
| d25-fullscreen-r1 | 1/3 | 3 | 0 | 8.26 | yes (16, Idle) |
| d25-clean-r1 | 13/65 | 25 | 0 | 8.43 | yes (16, Idle) |
| d25-canvas-r1 | 7/28 | 17 | 0 | 8.89 | yes (16, Idle) |
| d25-control-r2 | 8/57 | 16 | 0 | 7.86 | yes (16, Idle) |
| d25-fullscreen-r2 | 27/165 | 69 | 0 | 8.70 | yes (16, Idle) |
| d25-clean-r2 | 13/90 | 57 | 0 | 8.56 | yes (16, Idle) |
| d25-canvas-r2 | 13/90 | 61 | 0 | 8.70 | yes (16, Idle) |

Queue/NACK/loss counters include warmup and the whole connection, as in existing reports; send rate uses the saved measurement-window snapshots. They show the stochastic transport context of the tail comparisons, rather than isolating presentation.

## Interpretation and default recommendation

**Keep the current video default.** Clean mode has no established gain. Its paired mean shaped capture P80 gain is−2.4ms and input→flip P80 gain−1.1ms (negative means slower), both noise; it also suppresses connection status and IME/keyboard UI. Canvas is confirmed desynchronized, but first-pair shaped input P80 gains of26.1ms decoded/glass and28.7ms callback reverse in the second pair (−11.8/−5.7ms). Across both pairs its mean capture decoded/glass P80 gain is−12.2ms and input P80 gain+7.1ms (+11.5ms callback), without a repeat-backed≥20ms gain or canvas scanout measurement. Clean and canvas remain URL-only lab modes; no default flags/styles changed.

Kiosk has a repeat-backed software input-tail association: decoded/glass P99 improves42.5/42.6ms in the two pairs, and callback P99 improves34.3/26.7ms (paired means42.5/30.5ms). However input P80 changes+15.1/−4.3ms, and capture P99 changes+20.8/−24.0ms. Kiosk changes source dimensions and remains on the same unsupported-overlay disconnected desktop. These results merit a connected-console repeat; they do not establish overlay promotion, a physical presentation gain, or a reason to force kiosk by default.

All8 shaped capture→decoded rows meet P80<100/P99<200ms. **0/8 input→decoded rows meet acceptance**; every input P80 remains>100ms. Chrome89 decoded and old glass quantiles are numerically identical throughout; both remain reported separately to preserve the API definitions. The background release build ran in every row (16 clang-cl, set Idle before every launch).

The same-session control capture decoded/glass P80 is82.7/68.5ms, versus M3 d25j5-c12 S display P80=108.6/124.0ms (headed reference111.5ms). Current control callback input P80=182.0/166.8ms, versus M3=145.4/164.7ms (headed151.7ms). Thus the lower M89 capture estimate does not imply a corresponding input improvement. M3 mostly used headless Edge at1898x1020 and the earlier capture path; this task uses M89 at1684x940, shared-fence capture, a disconnected desktop and an ongoing build. M4 capture-phase also reported135–143ms capture decoded P80 under build load. Browser timestamp behavior, size, source phase, capture changes and load confound these comparisons; no presenter-specific gain can be assigned from them. See [M3 measurements](../../../archive/research/m3/matrix2-measurements.md), [M3 matrix context](../../../archive/research/m3/matrix2.md), and [capture-phase investigation](capture-phase.md).

## Reproduction, changes and validation

Use the Chrome89 launch switches and m3-combined environment from [PRESETS](../../../tools/latency/PRESETS.md), with headed Chrome89, `--viewer-window-size 1928,1162`, and a fresh dedicated profile per run. Add `--presenter clean` or `--presenter canvas`; for kiosk, append `--kiosk` inside the existing `--viewer-flags` string without removing the M89 compatibility switches. Viewer URLs accept `?presenter=clean` / `?presenter=canvas`. Task `batch.mjs` records full args, per-run build state, snapshots, logs and JSON/CSV; `diagnostics.mjs` records trace/flags/session/GPU text/screenshots separately. `summarize.mjs` and `analyze-traces.mjs` generate the tables; `validation.json` verifies all20 reports and separately records acceptance failure and unverified physical display.

Files changed for this task: `viewer/presenter.js` (new opt-in modes), `viewer/index.html` (load it), `tools/latency/measure.js` (draw/read visible canvas and report mode), `tools/latency/run.mjs` (`--presenter` argument), and this report. Pre-existing working-tree edits in viewer/viewer.js, PRESETS, the field launcher and other research files were preserved. Node syntax checks passed; runtime validation confirms Chrome89, H264, VPL throughout without fallback, all run durations/groups, finite samples for all five timing metrics, desynchronized canvas and shaped relay/relay pairs. `git diff --check` passed. No full builds, test suites, commits, downloads or console-session changes were made.

Unverified: physical 60Hz display/scanout, connected-console overlay eligibility/promotion, same-frame canvas/video/rVFC correlation on M89 via VideoFrame, objective visual quality, build-free timing and fixed-resolution kiosk A/B. The optional user reconnection request received no response; continued best effort in the recorded disconnected session.

Concurrent workspace change: HEAD advanced to `6caa58e` during this task; that external commit already contains this task's measure.js/run.mjs edits alongside other M4 work. This task issued no commit commands and did not revert that commit. The presenter script, its index.html load and this report remain for review in the working tree.

## Phone high-speed glass check (user procedure)

The current sender is headless and has no source glass. A literal two-display glass-to-glass test requires a separately documented headed source-reference setup; that changes the sender environment. For the current chain, the input/LED-to-viewer test below directly checks input-to-photon without claiming a physical source-display reference.

1. Attach the viewer to the intended physical 60Hz display in the active console session. Keep both source and viewer visible on physical displays. Fix brightness/refresh, disable adaptive refresh, and place their compared patches at the same vertical scanline to reduce rolling-scan bias.
2. Film both displays simultaneously at240fps or higher with fixed exposure/focus, short shutter and enough light. Render a large alternating black/white patch with a unique counter on the source and its remote copy. Record at least100 transitions per mode; use the visible counter to match transitions and detect dropped/repeated frames.
3. For source-glass-to-viewer-glass, count camera frames from first visible source transition to first visible matching viewer transition and multiply by1000/camera_fps. Source-panel latency changes the reference instant, so this is not numerically identical to the embedded source-draw clock metric. At240fps one camera frame is4.17ms; report quantization and rolling-shutter uncertainty.
4. For input-to-glass, film a physical button/LED indicator and the remote response together, preferably using an LED tied electrically to the input edge. Count to the first visible colour change. A filmed finger/contact alone has uncertain electrical-event timing; do not treat it as a calibrated keydown timestamp.
5. Repeat interleaved control/option runs with identical resolution/network/content and compare P50/P80/P99 plus missed transitions. A few events cannot establish P99. Retain original high-speed files, actual frame timestamps (some phones write slow-motion variable-rate video), scanline positions, display settings and session state. Screen recording and rVFC traces cannot replace this check.
