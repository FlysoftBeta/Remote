# m2-latency measured result

All latency values are ms. Valid comparisons: 60s real-chain harness +3s warmup, tab mode, 1898x1068, OpenH264, Chromium157 dev/component sender, Edge156 headless receiver, loopback, concurrent real key/decoded-colour sampling every500ms. Capture->display is the existing fixture draw-time to rVFC expectedDisplayTime estimate; input->flip is one viewer-clock key-send to decoded-pixel detection.

Final accepted defaults: capture->display 50.77 / 60.80 / 67.24 / 75.28 (1443); input->decoded flip 96.30 / 105.20 / 111.40 / 118.40 (120); zero input timeouts, zero marker failures, mean decoded 29.87fps. Improvement vs fresh baseline: capture P50 16.4ms; input P50 10.8ms. Final metadata validation 58 samples matched to <1e-8ms.

## Kept settings

- Receiver target 0 (legacy playoutDelayHint fallback) and negotiated sender zero/zero delay retained; these existed before this task. Sender zero-delay is the meaningful jitter-buffer win.
- New default: track ContentHint::kFluid, with OpenH264 kScreensharing mode and MAINTAIN_RESOLUTION retained. --latency-screen-content restores old kNone hint for A/B.
- Default 30 fps retained; --fps=60 remains opt-in. It requests 16.7 ms capture/encoder pacing but achieved ~51 decoded fps and variable/worse input results at 1080p. No Chromium edits/build.

## Before/after trials

P50 / P80 / P95 / P99 (n). Evidence: each named directory holds JSON/CSV; <trial>-endpoint.log, -harness.log, -cpu.json hold stages/console/CPU.

| Trial | Capture->display | Input->decoded flip | Mean jitter | Mean decoded fps | Endpoint CPU cores |
|---|---|---|---:|---:|---:|
| baseline1080 | 67.14 / 75.77 / 83.31 / 92.63 (1437) | 107.10 / 122.60 / 135.20 / 140.10 (120) | 0.19 | 29.87 | 0.44 |
| baselineRepeat | 67.12 / 75.71 / 81.16 / 89.81 (1446) | 105.60 / 118.50 / 131.80 / 139.10 (120) | 0.20 | 29.88 | 0.44 |
| bothOff | 123.48 / 132.35 / 139.81 / 145.91 (1258) | 150.40 / 166.30 / 187.10 / 210.10 (120) | 39.19 | 29.78 | 0.47 |
| senderOff | 125.52 / 135.62 / 141.62 / 147.82 (1239) | 157.90 / 175.10 / 191.20 / 200.70 (120) | 41.52 | 29.79 | 0.48 |
| receiverOff | 55.99 / 65.64 / 73.78 / 82.14 (1429) | 96.10 / 106.90 / 120.10 / 135.40 (120) | 0.11 | 29.80 | 0.46 |
| fluid | 51.89 / 62.37 / 67.39 / 73.87 (1463) | 96.10 / 106.20 / 115.10 / 126.30 (120) | 0.16 | 29.85 | 0.43 |
| fluidRepeat | 50.38 / 58.79 / 66.29 / 71.39 (1480) | 99.00 / 109.90 / 120.30 / 126.50 (120) | 0.17 | 29.88 | 0.43 |
| maintainFps | 86.08 / 94.00 / 100.48 / 107.86 (1416) | 135.00 / 143.00 / 151.70 / 155.80 (120) | 0.16 | 29.77 | 0.44 |
| fps60 | 44.90 / 53.16 / 63.79 / 74.60 (1868) | 95.50 / 103.20 / 113.70 / 119.50 (120) | 0.15 | 50.91 | 0.90 |
| fps60Repeat | 60.49 / 67.19 / 77.29 / 83.83 (1635) | 121.80 / 131.00 / 134.10 / 137.50 (120) | 0.14 | 51.16 | 0.81 |
| combined60 | 80.66 / 87.97 / 99.24 / 106.18 (1673) | 134.30 / 144.10 / 155.10 / 173.50 (120) | 0.14 | 50.92 | 0.83 |
| final30 | 50.77 / 60.80 / 67.24 / 75.28 (1443) | 96.30 / 105.20 / 111.40 / 118.40 (120) | 0.18 | 29.87 | 0.46 |

(a) Both controls off -> receiver-only: bothOff -> senderOff; no benefit (123.5 ->125.5 P50). (b) Both off -> sender-only: bothOff ->receiverOff;123.5 ->56.0 P50, mean jitter39.19 ->0.11ms. With receiver0 held constant, senderOff ->baseline1080 gives125.5 ->67.1 P50, jitter41.52 ->0.19ms. (c) baseline ->fluid repeated improves capture P50~15-17ms and input P50~7-11ms; maintain-framerate did not help. (d) 60fps isolated and combined trials vary widely and do not sustain60fps, so remain opt-in.

Original runtime reference was85.7/101.7/114.3ms P50/P95/P99 (n1431), input132.9/147.7/155.7(n120). Unchanged-settings fresh baseline repeated67.1ms P50. The original86ms cannot be partitioned retrospectively without its stage data; do not attribute that difference to these quick wins. Initial baseline/ at1890x986 is excluded. receiverOffRepeat is excluded: GPU crash after~35s, capture stopped, partial n818/input67, max4139.6ms.

## Where current latency goes

Endpoint columns are medians of five-second-window P50/P95s (not pooled whole-run percentiles); baseline1080 has 11 interior windows, roughly 1640 frames per stage. Viewer rVFC columns are whole-run callback quantiles. Endpoint and viewer rows cover different frame subsets. Components overlap as labelled and their quantiles are not additive.

| Stage (baseline1080) | P50 | P95 | Evidence/meaning |
|---|---:|---:|---|
| Viz capture_begin -> capture_end | 12.70 | 16.50 | GPU/Viz capture completion |
| capture_end -> endpoint receive | 0.49 | 0.70 | Mojo/browser/endpoint transfer |
| receive -> private copy submitted | 0.17 | 0.24 | Validation, allocation, AcquireSync, CopyResource submission; GPU completion is later |
| copy submission -> conversion start | 0.08 | 0.12 | No whole-frame queue stall |
| conversion start -> Map returned | 5.47 | 9.16 | Staging allocation/copy + blocking GPU readback |
| Map returned -> I420 ready | 1.81 | 2.85 | I420 allocation + libyuv conversion + Unmap |
| I420 source push -> encode start | 0.10 | 0.22 | No whole-frame screenshare cadence hold |
| encode start -> output callback | 9.66 | 29.98 | OpenH264, includes large colour-flip updates |
| encode output -> libwebrtc callback entry | 0.01 | 0.01 | Handed to libwebrtc, not socket transmission |
| libwebrtc output callback duration | 0.07 | 0.11 | Packetization/send enqueue, not pacer drain |
| capture_end -> encoded (overlaps above) | 18.21 | 38.25 | Combined endpoint path, useful M3 comparison |
| last packet received -> submitted for composition | 1.10 | 7.20 | rVFC n=1722 |
| submitted -> expected display | 18.60 | 28.70 | rVFC n=1722 |
| last packet -> expected display (overlaps) | 20.60 | 31.80 | rVFC n=1722 |
| rVFC decoder submission -> decoded-ready (overlaps) | 0.60 | 7.00 | rVFC n=1722 |

getStats baseline interval means: jitter=0.19ms; decode=2.07ms; totalProcessingDelay/frame=2.30ms (includes jitter/decode; NOT an additional stage). framesDropped delta sum=0. Marker draw ->last-packet receive P50=49.17ms; this includes source draw/commit/capture, endpoint encode and sender pacing/network. Rough budget:12.7ms Viz +8.4ms endpoint before encode +9.7ms encode +~20.6ms receive/display =51.4ms; remaining~16ms of67.1 is unpartitioned source scheduling + packet pacing/transport + subset/quantile effects [I]. No evidence of a sender whole-frame queue hold (source->encode~0.1ms). Network one-way/pacer dwell was NOT measured separately; loopback min probe RTT~0.5-0.7ms must not be called a one-way network measurement.

Reference time is forwarded unchanged and can lie after capture_begin/receive; signed deltas are logged. Baseline1080 pre-fix reference deltas dropped negatives and are invalid for interpretation; subsequent runs have full signed reference data. combined60 optional VideoFrame/rVFC media-time validation 57 samples all matched (<1e-8ms); no per-frame logging was added.

## M3 lever and remaining target

At 30 fps, staging/readback5.47ms + I4201.81ms + OpenH2649.66ms ~=16.9ms P50 gross work replaceable by D3D/NV12/oneVPL. Combined capture_end->encoded is18.21ms P50 /38.25ms P95. These are opportunities, not guaranteed net savings: GPU conversion/encoder/synchronization still costs time. M3 should compare the same stage metrics and remove CPU readback/conversion plus software encode tails. Viz~12.7ms and viewer composition/display~18.6ms remain separate levers. Direct per-frame packet-send/receive correlation is needed to split source scheduling from pacer dwell; do not infer them from unrelated percentiles.

No 50 ms WAN emulation was run. Adding 50 ms to measured loopback P80 is only a rough projection and does not establish the product P80<100ms target; current input P80 remains far above 50 ms local. GPU encoding alone is unlikely to close the input budget.

## Validation / limitations

- Incremental endpoint build passed; JS syntax and git diff --check passed. No full builds/test suites or commits. CPU snapshots every5s: logical CPUs=16; endpoint~0.44core at30 vs~0.81-0.90 at 60, whole chain~2.3cores at 60 (~14-15% machine). Get-Process CPU deltas cover processes surviving between first/last snapshot; group totals can include unrelated same-name browser processes.
- Chrome89 legacy API check passed: jitterBufferTarget absent, playoutDelayHint exists and accepts0, rVFC exists, optional VideoFrame absent. Installed Chromium89 lacks H264 in capabilities, so playback compatibility remains unverified (chrome89-api.json and chrome89-harness.log). Failed trial stopped after diagnosis; no report.
- One excluded GPU crash: Skia Graphite ResourceCache.cpp:523 check(this->isInCache(resource)); capture did not recover after GPU restart. Logs preserved. No crash fix attempted outside scope.
- Real network/jitter, physical display photons, encoder visual-quality changes and sustained varied-content1080p60 are unverified.

## Changed files / reproduction

Repo: endpoint/{latency_stats.h,d3d_frame_source.cc,peer.cc,main.cc,BUILD.gn}, viewer/viewer.js, tools/latency/{measure.js,run.mjs}. Prior viewport changes were preserved; another task's commit5a4608c landed during work. No Chromium source edits.

Task-local trial.mjs launches cp.sh + run.mjs and saves complete evidence; series.mjs/repeats.mjs record historical flags. Final build baseline reproduction must add --latency-screen-content (the historical --latency-fluid is now the default). Example final trial: node C:/src/tmp/m2/latency/trial.mjs rerun30 30 0. Baseline: same with --latency-screen-content. Old trial source snapshots peer-measured.cc, *.initial.* preserve original conditions. API citations and chronological evidence follow.

---

# m2-latency
Started 2026-10-26. Scope: stage timings, isolated 60 s trials, latency quick wins. No commits.

## Work log
- Inspecting capture, encoder, viewer, and existing harness. No behavior changes yet.

## Files changed
- Pending.

## Evidence / API citations
- Pending; [V] = read at pinned Chromium source, [I] = inference.

## Breakdown / trials
- Pending.

## Open risks
- 45 minute task budget; GPU path unavailable in this task.
## Inspection / instrumentation (10:47-10:54 SGT)
- Initial dirty changes in endpoint/browser_session.cc, peer.cc (InitEncode size log), viewer/viewer.js (viewport/DPR) belong to earlier work and are preserved. Snapshots in *.initial.*.
- Receiver delay=0, sender EncodedImage Minimal(), extmap negotiation, screenshare encoder mode, and MAINTAIN_RESOLUTION already existed at task start. Baseline is those settings, not an invented unoptimized baseline.
- Added bounded TimeTicks stage samples (5 s rolling P50/P95, n, last absolute from/to microseconds), rVFC receive/presentation/display deltas and getStats interval samples. No behavior changes for initial trial.
- Existing endpoint --fps=30 default feeds CaptureParams.max_fps and sender max_framerate; Chromium SetMinCapturePeriod(1/fps) already obeys it. No Chromium build or capture edits needed.
- Trial switches added: --latency-no-playout-delay suppresses EncodedImage delay while keeping negotiated capability; --latency-maintain-framerate selects MAINTAIN_FRAMERATE. These are attribution controls.
## First instrumented run (10:52 SGT)
- Completed 60s, but decoded 1890x986 because earlier viewport work now resizes to the Edge viewer CSS viewport. P50/P80/P95/P99 65.8/74.3/81.4/92.6ms (n=1431); input 105.4/120.9/136.3/157.3ms (n=120). This is NOT an improvement attributable to this task against the runtime 1898x1068 reference.
- Changing only harness viewer outer size to 1928x1162 to restore 1898x1068 for all controlled trials. First run retained in baseline/ but excluded from before/after table.
- Breakdown first look: source_to_encode ~0.1ms; no whole-frame sender cadence stall. GPU staging Map ~5.5ms; conversion ~1.8ms; encode ~9ms P50/~28ms P95; Viz ~12ms. Receiver jitter ~0.1-0.3ms with existing zero-delay controls.
## Verified pin APIs (10:55 SGT)
- [V] base/time/time_win.cc:162-165,722+: TimeTicks uses QPC; consistent across processes on Windows. Endpoint already CHECKs IsConsistentAcrossProcesses.
- [V] media/base/video_frame_metadata.h:54-55,134: optional capture_begin_time/capture_end_time/reference_time. chrome/browser/prts/prts_capture.cc forwards these unchanged in FrameInfo. Missing or negative-order deltas are omitted rather than treated as zero.
- [V] third_party/webrtc/api/video/video_frame.h:175+: timestamp_us monotonic clock; video/video_stream_encoder.cc:1580+ preserves it except future timestamps and assigns RTP timestamp; api/video/encoded_image.h:100-101 provides RtpTimestamp for callback correlation.
- [V] api/video/encoded_image.h:157-163 SetPlayoutDelay(optional<VideoPlayoutDelay>); api/video/video_timing.h:108-125 says min=max=0 means render ASAP; Minimal() returns exactly zero/zero.
- [V] api/rtp_transceiver_interface.h:159-175 Get/SetHeaderExtensionsToNegotiate and GetNegotiatedHeaderExtensions. Existing endpoint sets kPlayoutDelayUri sendrecv before offer and verifies answer. modules/rtp_rtcp/source/rtp_sender_video.cc:422 serializes PlayoutDelayLimits; 1034+ takes header.playout_delay. video/rtp_video_stream_receiver2.cc:813 parses it. remoting/protocol/webrtc_video_encoder_wrapper.cc:412 uses identical Minimal() pattern.
- [V] third_party/blink/renderer/modules/peerconnection/rtc_rtp_receiver.idl:12-14 has nullable playoutDelayHint and feature-gated jitterBufferTarget. Existing JS feature detection uses ms for new API / seconds for old API. Actual Chrome 89 binary not yet tested.
- [V] api/media_stream_interface.h:188,198-199 ContentHint and set_content_hint; pc/rtp_sender.cc:1642-1667 kFluid overrides is_screencast=false. Added isolated --latency-fluid test.
- [V] video/frame_cadence_adapter.cc:500-520 zero-Hz can delay one frame, but 990+ requires source_constraints min_fps=0. Measured source_to_encode ~0.1ms means this is not an active stall here.
## Correct-sized baseline (10:55 SGT)
- 60s tab-mode OpenH264 Edge156 1898x1068@30; capture->display P50/P80/P95/P99 = 67.1/75.8/83.3/92.6ms n=1437. Input->decoded flip = 107.1/122.6/135.2/140.1ms n=120, zero timeouts. No latency setting changed from task start; the difference from the earlier runtime 85.7ms reference is unexplained run/environment variation, NOT a claimed quick-win gain.
- Instrumentation fix before isolated trials: reference_time is often later than capture_begin and occasionally later than receive; preserve signed reference deltas. Prior reference_to_receive samples in baseline1080 omit negative deltas and must not be read as representative. Other latency deltas unaffected.
- [V] components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.cc:974 sets metadata.reference_time=event_time; media/base/video_frame_metadata.h:127-134 says event/target playout time for synchronization, not high-resolution presentation time.
- Added report answer extmap evidence and compact viewer 5s P50/P95 of 1s interval means; full interval series still exported.
## Isolated trials so far (10:59 SGT)
- receiverOff (sender zero-delay still ON): capture P50/P80/P95/P99 56.0/65.6/73.8/82.1ms n=1429; input 96.1/106.9/120.1/135.4 n=120. Mean jitter 0.11ms. Lower capture latency mostly comes before the receiver, so this is not proof that removing receiver hint helps; repeat needed.
- senderOff (receiver target=0 still ON): capture 125.5/135.6/141.6/147.8ms n=1239; input 157.9/175.1/191.2/200.7 n=120. Mean jitter 41.52ms, vs 0.19ms baseline. Strong reason to retain sender min=max=0. Answer extmap is captured in report.
- All completed controlled runs 1898x1068, zero marker failures, input timeouts=0. getStats means reported above are means of 1s interval means, not per-frame quantiles.

### Completed trial fluid
- Args: fps=30, delay=0, extra=--latency-fluid; duration=60s. Dimensions 1898x1068.
- capture->display {"p50":51.88916015625,"p80":62.37451171875,"p95":67.38916015625,"p99":73.87451171875,"max":86.189208984375,"n":1463}
- input->decoded flip {"p50":96.09999999403954,"p80":106.20000000298023,"p95":115.09999999403954,"p99":126.30000001192093,"max":129.8999999910593,"n":120}; timeouts=0. Evidence: fluid/latency-2026-10-26T03-00-46-855Z.json, fluid-endpoint.log, fluid-cpu.json.

### Completed trial maintainFps
- Args: fps=30, delay=0, extra=--latency-maintain-framerate; duration=60s. Dimensions 1898x1068.
- capture->display {"p50":86.08056640625,"p80":93.9951171875,"p95":100.48046875,"p99":107.857177734375,"max":119.240478515625,"n":1416}
- input->decoded flip {"p50":135,"p80":143,"p95":151.70000000298023,"p99":155.79999999701977,"max":156.70000000298023,"n":120}; timeouts=0. Evidence: maintainFps/latency-2026-10-26T03-02-01-649Z.json, maintainFps-endpoint.log, maintainFps-cpu.json.

### Completed trial fps60
- Args: fps=60, delay=0, extra=; duration=60s. Dimensions 1898x1068.
- capture->display {"p50":44.90283203125,"p80":53.15966796875,"p95":63.7861328125,"p99":74.604248046875,"max":104.302978515625,"n":1868}
- input->decoded flip {"p50":95.5,"p80":103.19999998807907,"p95":113.70000000298023,"p99":119.5,"max":141.20000000298023,"n":120}; timeouts=0. Evidence: fps60/latency-2026-10-26T03-03-16-403Z.json, fps60-endpoint.log, fps60-cpu.json.

### Completed trial baselineRepeat
- Args: fps=30, delay=0, extra=; duration=60s. Dimensions 1898x1068.
- capture->display {"p50":67.12109375,"p80":75.710205078125,"p95":81.16357421875,"p99":89.805419921875,"max":100.013427734375,"n":1446}
- input->decoded flip {"p50":105.59999999403954,"p80":118.5,"p95":131.79999999701977,"p99":139.1000000089407,"max":143.09999999403954,"n":120}; timeouts=0. Evidence: baselineRepeat/latency-2026-10-26T03-05-27-179Z.json, baselineRepeat-endpoint.log, baselineRepeat-cpu.json.
## First 60 fps result (11:03 SGT)
- --fps=60 requests capturer minimum period 16.7ms and sender max_framerate=60. getStats average decoded 50.91fps, endpoint ~52-53fps. It does NOT sustain 1080p60 on this fixture.
- Capture P50/P80/P95/P99 44.9/53.2/63.8/74.6ms n=1868; input 95.5/103.2/113.7/119.5ms n=120, zero timeouts. CPU endpoint ~0.90 logical core vs 0.44 at30; whole measured chain ~2.38 of16 logical cores (14.9% machine). CPU is modest overall but frame deadlines are missed.
- 60fps stages (median 5s-window quantiles): staging Map 4.63/7.03ms; I420 1.55/2.86ms; encoder 12.55/17.83ms P50/P95. Source->encode P95 grows to5.61ms. Need repeats before selecting default.
- Important limits: endpoint stage quantiles are all encoded frames; rVFC/marker stats cover presented/decoded-marker subsets. Their quantiles must not be subtracted as exact frame-matched network latency. In particular marker->receive at 60 has P50 17.4ms while all-frame capture_end->encoded P50 is20.5ms; pixels/metadata association and selection bias need validation. True input->decoded flip uses one viewer clock and is independent of source clock alignment.
- [V] call/rtp_payload_params.cc:226 copies EncodedImage.PlayoutDelay into RTPVideoHeader. rtc_base/experiments/alr_experiment.cc:26,52-56 default screenshare pacing factor1.0; video/video_send_stream_impl.cc:178+,223+ selects different ALR/pacing by content type. This may explain some kFluid difference, but no direct pacer dwell was measured [I].

### Completed trial receiverOffRepeat
- Args: fps=30, delay=off, extra=; duration=60s. Dimensions 1898x1068.
- capture->display {"p50":59.40771484375,"p80":66.435546875,"p95":71.44677734375,"p99":84.87451171875,"max":4139.574462890625,"n":818}
- input->decoded flip {"p50":97.20000000298023,"p80":105.40000000596046,"p95":114.69999998807907,"p99":120.5,"max":120.5,"n":67}; timeouts=0. Evidence: receiverOffRepeat/latency-2026-10-26T03-06-42-940Z.json, receiverOffRepeat-endpoint.log, receiverOffRepeat-cpu.json.
## Repeat validation / GPU failure (11:07 SGT)
- baselineRepeat 60s again yielded capture P50/P80/P95/P99 67.1/75.7/81.2/89.8ms n=1446; input105.6/118.5/131.8/139.1ms n=120.
- EXCLUDE receiverOffRepeat from valid latency comparisons: after ~35s remote Chromium GPU process crashed/reinitialized (receiverOffRepeat-chrome.log:7230-7231), capture then remained at0fps. Partial n=818/input67, max4139.6ms. Capture does not recover after this GPU crash; this is an open risk outside latency changes, not a passing 60s sample.
- Chromium source performance.timeOrigin+performance.now agreed with Date.now and Node wall time within~1ms in a CDP probe (11:05:41 SGT). That probe did not reveal a source wall-clock bias; it does not establish per-frame pixel/metadata matching.

### Completed trial fluidRepeat
- Args: fps=30, delay=0, extra=--latency-fluid; duration=60s. Dimensions 1898x1068.
- capture->display {"p50":50.37841796875,"p80":58.786376953125,"p95":66.286376953125,"p99":71.386474609375,"max":86.424560546875,"n":1480}
- input->decoded flip {"p50":99,"p80":109.8999999910593,"p95":120.29999999701977,"p99":126.5,"max":133,"n":120}; timeouts=0. Evidence: fluidRepeat/latency-2026-10-26T03-07-57-678Z.json, fluidRepeat-endpoint.log, fluidRepeat-cpu.json.
## Content hint repeat (11:08 SGT)
- kFluid at30 repeated capture P50/P80/P95/P99 50.4/58.8/66.3/71.4ms n=1480; input99.0/109.9/120.3/126.5ms n=120. First fluid run51.9/62.4/67.4/73.9. Both beat two stable67.1ms no-hint baselines; retain if the combined60fps trial and Chrome89 compatibility pass.
- MAINTAIN_FRAMERATE alone gave86.1ms capture P50 and135.0ms input P50, no resolution loss. No evidence to change degradation default away from MAINTAIN_RESOLUTION; do not keep it as default.
- GPU crash signature in excluded run: ResourceCache.cpp:523 check(this->isInCache(resource)), Graphite cache cleanup stack; no claim that receiver hint caused it.

### Completed trial fps60Repeat
- Args: fps=60, delay=0, extra=; duration=60s. Dimensions 1898x1068.
- capture->display {"p50":60.489013671875,"p80":67.18896484375,"p95":77.2890625,"p99":83.83251953125,"max":119.765625,"n":1635}
- input->decoded flip {"p50":121.79999999701977,"p80":131,"p95":134.09999999403954,"p99":137.5,"max":138.59999999403954,"n":120}; timeouts=0. Evidence: fps60Repeat/latency-2026-10-26T03-09-12-462Z.json, fps60Repeat-endpoint.log, fps60Repeat-cpu.json.
## Second 60 fps result (11:09 SGT)
- fps60Repeat: capture60.5/67.2/77.3/83.8ms n=1635; input121.8/131.0/134.1/137.5ms n=120; decoded51.16fps. This worsened input vs both30fps baselines and fluid30. Endpoint0.81 core, receiver0.81 core. 60fps outcome is variable, so a default 60 switch is not justified by the isolated trials alone.
- Testing fluid+60 together with optional zero-copy VideoFrame timestamp diagnostics. If no robust improvement, retain default 30 and leave --fps=60 opt-in until oneVPL.

### Completed trial combined60
- Args: fps=60, delay=0, extra=--latency-fluid; duration=60s. Dimensions 1898x1068.
- capture->display {"p50":80.6611328125,"p80":87.966064453125,"p95":99.2373046875,"p99":106.1787109375,"max":120.937255859375,"n":1673}
- input->decoded flip {"p50":134.29999999701977,"p80":144.09999999403954,"p95":155.09999999403954,"p99":173.5,"max":179,"n":120}; timeouts=0. Evidence: combined60/latency-2026-10-26T03-10-39-403Z.json, combined60-endpoint.log, combined60-cpu.json.
## Kept defaults (11:11 SGT)
- Keep receiver jitterBufferTarget=0 / legacy playoutDelayHint=0 and sender zero/zero RTP playout delay (both already existed). Keep MAINTAIN_RESOLUTION and screenshare OpenH264 encoding mode.
- Change only the track content hint default to kFluid, based on two30fps repetitions beating two unchanged baselines. --latency-screen-content restores prior kNone hint for reproducible baselines. --latency-no-playout-delay and --latency-maintain-framerate remain explicit diagnostic controls.
- Retain30fps default. Existing --fps=60 reaches CaptureParams, Viz minimum period, and sender/codec frame rates. Actual1080p throughput~51fps; variable end-to-end results and doubled endpoint CPU argue against default 60 before oneVPL. No Chromium capture edits/builds required.
- combined60 fluid: capture80.7/88.0/99.2/106.2ms n=1673; input134.3/144.1/155.1/173.5ms n=120. No GPU crash. Diagnostic57 samples had VideoFrame.timestamp - rVFC.mediaTime =0 (floating error <1e-8ms). This validates sampled current-pixel/metadata media timestamps in that run. Different endpoint/presented subsets still prohibit exact per-stage percentile subtraction.
- Final incremental build includes new header in BUILD.gn and logs encoder maxFramerate. Main fps default comment records decision. No commits or Chromium source edits.
## Chrome89 API compatibility (11:14 SGT)
- Installed C:/src/tmp/chrome89/chrome-win/chrome.exe is HeadlessChrome/89.0.4389.0; its video capabilities have VP8/VP9 but NO H264. Stream negotiation therefore fails at setLocalDescription before encoder initialization. Cannot claim Chrome89 H264 playback passed; no codec fallback added (outside task scope).
- CDP runtime evidence chrome89-api.json: jitterBufferTarget=false; playoutDelayHint=true; setting playoutDelayHint=0 succeeds and reads0; rVFC=true; VideoFrame=undefined. Optional diagnostic is feature-guarded. This verifies the legacy receiver hint API path, not H264 decode.
- Ended failed compatibility trial early after saving logs/API evidence rather than waiting for a report that cannot arrive. Final Edge trial uses kept defaults, fps30, with media-timestamp validation enabled.

### Completed trial final30
- Args: fps=30, delay=0, extra=; duration=60s. Dimensions 1898x1068.
- capture->display {"p50":50.7685546875,"p80":60.80419921875,"p95":67.244384765625,"p99":75.281005859375,"max":86.781005859375,"n":1443}
- input->decoded flip {"p50":96.29999999701977,"p80":105.20000000298023,"p95":111.40000000596046,"p99":118.40000000596046,"max":121.90000000596046,"n":120}; timeouts=0. Evidence: final30/latency-2026-10-26T03-15-27-475Z.json, final30-endpoint.log, final30-cpu.json.
## Final accepted-default verification (11:15-11:17 SGT)
- final30 at1898x1068: capture P50/P80/P95/P99 50.8/60.8/67.2/75.3ms n1443; input96.3/105.2/111.4/118.4ms n120; zero timeouts and marker failures; decoded29.87fps. Answer extmap: a=extmap:5 http://www.webrtc.org/experiments/rtp-hdrext/playout-delay.
-58 optional VideoFrame/rVFC timestamp samples matched (maximum error7.3e-12ms). Encoder initialization explicitly reports max_fps=30. Mean jitter0.18ms, decode2.27ms, totalProcessing2.49ms, framesDropped delta0.
- Final input P80 + assumed additional50ms network =155.2ms; capture P80+50=110.8ms. These are arithmetic projections, not WAN measurements. P80<100ms target is not achieved.
- Common timing schema: bounded per-frame QPC/TimeTicks pairs, 5s rolling P50/P95/n and last_us=absolute-from:absolute-to; no per-frame stage log lines. Capture-end source timestamp correlates I420-ready to encode-start; RTP timestamp correlates encode-start/end. last_us values for different stages can represent different most-recent frames.

## Handoff
- Completed at approximately11:19 SGT. Test chrome/endpoint/control-plane stopped; no listener8300 remains. Final source differs from measured build only by a comment clarifying the evidence for fluid content.
- All requested notes, trial tables, API citations, logs and raw JSON/CSV are in this task directory. No git commits made by this task.
- [V] Final helper API header checks: base/no_destructor.h:98,112-113 (construction/dereference); base/synchronization/lock.h:153-154 (AutoLock); base/command_line.h:198-199 (HasSwitch); base/time/time.h:249,252 (InMillisecondsF/InMicroseconds). C++ headers and callbacks compiled against the pin.
