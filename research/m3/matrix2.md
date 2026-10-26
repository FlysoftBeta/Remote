# m3-matrix2 〞 completed

2026-10-26. All 20 requested 60-second runs completed sequentially: 18 acceptance rows (four profiles ℅ S/V/G; c12 and c12-wifi repeated), one d50j10-c12 S stress row, one headed d25j5-c12 S row. No commits. Required incremental Chrome and endpoint builds both succeeded; no build fixes, C++ edits, full builds, clean or suites.

## Results

- Overall acceptance FAIL: 1/18 acceptance capture rows passes both P80<100/P99<200; 0/18 input rows passes. Including stress/headed: capture1/20, input0/20. Only d25j5-c16 V capture passes (P80/P99 97.60/110.52 ms). No row passes both metrics.
- Acceptance capture P80 97.60每130.13 ms; input P80 137.90每164.70 ms. Nineteen capture P99s pass; input P99 fails in c10 S, five Wi-Fi rows and stress. Each individual failing threshold is named in the full table.
- Actual decoded mean57.37每59.32 fps, sender2.49每9.73 Mbps. These are means across the measurement, not the CLI's last interval. Existing3s warmup retained for baseline parity; first few measured seconds can still ramp from~30 to60 fps. V/G content remains the baseline30fps canvas workload while marker/capture request60.
- Every cold start initializes VPL60 at1920℅1080, then viewport resize initializes1898℅1020 (headed1930℅1072). Original startup fallback did not reproduce. Nineteen runs stay hardware throughout; one Wi-Fi video row switches to software late in measurement. No rate rebuild or steady-state sender IDR observed in any row; zero input timeouts. Sixteen all-frame >200ms freezes total; exact attribution of isolated V/G early gaps remains unverified. Marker failure fraction0.84每5.86%; C quantiles are conditional on valid unique markers, whereas input/freezes are independent.
- Headed C P50/P80/P95/P99=104.63/111.52/117.82/123.12 ms; I=140.5/151.7/163.3/169.4 ms. Both P80 targets fail. It measures compositor estimates, not monitor scanout.
- Stress C=126.33/135.62/143.52/150.95 ms; I=197.8/210.2/223.2/233.2 ms. C fails P80; I fails P80/P99.

## Comparison and remaining failures

Most full-pipeline quantiles improve strongly over the matched OpenH26430 baseline, especially game decode (~10每11 ↙~59 fps). Improvements do not establish matched visual quality or an isolated codec/pacer effect. All20 matched rows, send rates and FPS appear in BEFORE-AFTER.md.

1. **Constrained-link bursts:** d25j5-c10 S has480 queue drops, max queue49.44ms. All480 occur inside measurement (~16.6/22.6/23.6s), not setup. Its capture P99 worsens184.11↙265.09ms and input248.4↙357.0ms despite mean sender6.97Mbps<10Mbps cap. Log remains native60, no rebuild/fallback/steady IDR. This supports burst queue/loss as a major tail contributor [I]; exact per-frame causal attribution was not recorded.
2. **Wi-Fi tails:** c12-wifi S r2 input P99 worsens249.0↙358.5ms; its relay has476 down loss drops and7 up loss drops, no queue drops. Wi-Fi input P99 exceeds200ms in five of six rows. Stochastic seeded packet loss and delay spikes plus input's two-way path are relevant; no exact per-input packet correlation was collected [I]. Capture P99 stays below200ms across Wi-Fi rows.
3. **New runtime VPL compatibility failure:** c12-wifi V r1 at17:24:51.600 requests Reset2000/12000kbps, returns status5; GetVideoParam returns2005/12000 with status0. Strict TargetKbps equality rejects this and activates OpenH264 for the final~3s (3375 native/185 I420 delivered frames). The row is mixed-backend and is not evidence of hardware VPL for the entire minute. Second Wi-Fi V run stays hardware, including60fps after rate changes. Evidence: d25j5-c12-wifi-V-r1-chain.log:248每253; PIPELINES/measurements preserve the mismatch. Warning5 is resolved incompatibility per the SDK, not a negative fatal Reset status [V]; quantization is the visible incompatibility, other unlogged fields were not independently dumped [I]. No encoder patch was made because vpl_encoder.cc is outside this task's allowed edit files.
4. **Residual scheduling:** endpoint median rolling-window P50 ranges: Viz15.44每16.84ms, mutex hold0.047每0.059ms, VPP submit0.244每0.297ms, encode6.35每11.27ms, capture-end↙encoded7.88每12.60ms. VPP completion observation6.85每11.69ms includes encode/sync and is only an upper bound. Viewer mean jitter7.22每21.50ms, mean decode2.72每4.02ms, receive↙display P5020.9每22.4ms, presentation↙display17.6每18.4ms. Sender per-packet queue dwell mean0.0026每0.2733ms. Nominal link adds25㊣5ms per direction (stress50㊣10); min probe RTT and relay timers are in NETWORK.md. These overlapping/different frame populations do not justify summing/subtracting quantiles into an exact budget. Remaining marker/render/capture/network phase is not fully separated.

## Recommended next levers

1. Fix reset-readback handling for small driver TargetKbps quantization while preserving strict geometry, low-delay/GOP/VBV and negotiated ceiling checks; separately validate warning5 at the2Mbps floor on this driver. No claim of a universally safe tolerance or tested workaround. Keep the software fallback visible. All20 starts succeeded with the landed fix; no extra startup workaround is needed on this host.
2. Tune network-aware VBR ceiling/frame-size cap and packet burst pacing on c10 S. Compare queue/drop tails at comparable quality, rather than relying on whole-minute bitrate averages. Initializing at negotiated12Mbps and hysteretic adaptation can overshoot a10Mbps shaped link; late bursts also occurred, so startup tuning alone is insufficient [I].
3. Add frame-correlated marker/render↙capture↙encode↙pacer↙receive instrumentation and test viewer compositor phase/jitter strategy. Current mean sender dwell is small, while receiver display adds~21ms and input P80 misses by38每65ms. Trace the remaining budget before selecting another pacing factor.
4. Follow up isolated >200ms V/G gaps and marker failures with a longer, explicitly stable warmup as a separate experiment. Do not replace these baseline-comparable results. Validate visual quality and real display scanout separately.

## Files changed

- tools/latency/run.mjs: --preset m3-combined launches real endpoint with VPL60, precise fake-vsync60, default hires timer and combined factor2.5/upper-capacity pacing trials; --scenario marker/S/V/G. Rejects stale running CP, conflicting FPS and disabled hires timer. Uses existing ENDPOINT_PATH/ENDPOINT_ARGS launch contract; records preset. No endpoint defaults changed.
- tools/latency/pipeline.mjs: describe leased-private-texture VPP; retain latest Reset-readback, runtime fallback reason and mixed-backend path; logged fallback overrides stale sender snapshot; printed backend reflects actual fallback.
- tools/latency/PRESETS.md: exact PowerShell reproduction and observed runtime caveat.
- All evidence, drivers and reports are task-local. Original raw CSV/JSON/logs are retained; aggregated pipelines correct obsolete path wording in early raw reports. Final collector diagnostics differ from measured collectors only in wording/backend metadata, verified against the captured fallback fixture.

## Evidence / validation

- build-evidence.txt: both required be.sh commands printed `The build has finished successfully.` and exited0 before measurements.
- MEASUREMENTS.md: every requested quantile, send/drop/maxQ/freeze/FPS/threshold, per-stage table and adaptation/marker/input counters.
- BEFORE-AFTER.md:20 matched baseline/new rows. NETWORK.md: configured delay, measured minimum RTT, sender dwell, relay timer/queue and marker↙receive/display stages.
- PIPELINES.md and measurements.json: per-row GPU/path/backend/rates/readback/viewer/display/relay blocks; summary.json numerical overview.
- final-validation.json: completeness/data-quality checks, including one expected documented runtime fallback; **this is not an acceptance PASS**. All20℅60s,19 headless+1 headed, selected relay/relay pairs, preset/hires/pacing/FPS, finite metrics, adequate samples/stage windows, progressing source fixtures.
- tool-validation.json: preset argument rejection and recorded mixed-backend regression fixture, including stale sender-stat precedence. First fixture-test draft assumed report.sender was non-null; corrected fixture setup, then passed. node --check on changed JS and git diff --check pass. No suite run.
- Exact argv/env, fixture metadata at12/50s, report/CSV and endpoint/Chrome output under each row directory / sibling *-chain.log. First S predates the second fixture sample; no screenshot was taken during measurement. Data/evidence sufficient without adding screenshot-induced stalls.
- Test chain/viewer stopped by harness cleanup; final process check found no task processes. No source changes after final tool validation except documentation.

## Source/API citations

[V] = read locally at the pin/SDK; [I] = inference from data/source.
- [V] control-plane/endpoint.js spawnEndpoint uses ENDPOINT_PATH and space-separated ENDPOINT_ARGS; run.mjs uses that existing interface.
- [V] endpoint/main.cc forwards cr-* switches, defaults hires timer on, exposes fps/encoder/field-trial switches. ui/gl/vsync_thread_win.cc:129每135 guards precise clock with ui::IsPrtsRemoteMode(); ui/gl/prts_fake_vsync_clock.h uses absolute deadline waitable timer (new header location).
- [V] endpoint/d3d_native_frame.cc:200每226: keyed CopyResource/release precedes private-resource VideoProcessorBlt/End/Flush. Pipeline descriptions follow this implementation.
- [V] endpoint/vpl_encoder.cc:259每289: Compatible requires exact TargetKbps/MaxKbps and other low-delay/geometry fields. :779每835 checks Reset/readback and hysteresis, never rebuilds for rate changes. endpoint/third_party/libvpl/api/vpl/mfxdefs.h:306 defines warning5; api/vpl/mfxvideo.h Reset documentation describes resolved incompatibility. This visible TargetKbps mismatch is sufficient to fail the compatibility predicate.
- [V] endpoint/latency_stats.h: rolling5s TimeTicks quantiles; endpoint/peer.cc outbound RTP snapshots. tools/latency/measure.js: CRC-valid unique-marker capture samples, independent colour-flip input and all-rVFC freeze gaps. viewer/viewer.js:3s warmup and1s stats sample reporting. tools/latency/scenarios.js: unchanged360CSSpx/s DOM scroll and30fps V/G fallback workloads.

Unverified: exact per-frame stage/network correlation, isolated GPU VPP duration, decoder implementation, physical scanout, objective visual quality, release builds, other drivers/GPUs and a fix/retest for runtime bitrate quantization. See NOTES-progress.md for incremental history.

