# M3 latency levers (draft, nothing built or run)

Pin: Chromium 157.0.8103.15 + prts commits (C:\src\chromium\src, working tree clean for ui/gl). [V] = read at pin, [I] = inference.
Evidence logs: C:\src\tmp\m2\latency\{fps60,baseline1080}-endpoint.log, fps60-chrome.log. Numbers are from the dev/component/dcheck build.

## Files
- `chromium.diff` (git -C C:\src\chromium\src apply --check: OK): ui/gl/vsync_thread_win.cc, drift-free pacing for `--fake-vsync-rate`.
- `remotekit.diff` (git -C ...\remotekit apply --check: OK): endpoint/main.cc, (a) 1 ms timer + power-throttling opt-out (default on, `--no-hires-timer` disables), (b) `--webrtc-field-trials=<Trial/Group/...>` (opt-in, default none).
  Base is the working-tree main.cc (staged by the vpl merge). peer.cc is UU (conflicted) so it is NOT touched; the stats snippet for peer.cc is in section 3.
- a/ b/ trees: `browser/{a,b}`, `remotekit/{a,b}`.

## 1. Why viz capture_begin -> capture_end is ~12.7 ms

Capturer config is already the best the API offers [V] chrome/browser/prts/prts_capture.cc:245-257:
SetFormat(ARGB), SetMinCapturePeriod(1/fps), SetMinSizeChangePeriod(0), SetAutoThrottlingEnabled(false), SetAnimationFpsLockIn(false),
SetResolutionConstraints(size,size,true), kPreferMappableSharedImage (GPU texture output, DXGI shared handle; software only as fallback).
Exact size => `request->SetScaleRatio(source, content)` is 1:1 [V] frame_sink_video_capturer_impl.cc:1224-1226, no scaling; letterbox is a no-op.
NV12 is rejected today ([V] prts_capture.cc:241-244 "M1 supports only ARGB").

Stamps [V]: capture_begin_time = clock->NowTicks() in MaybeCaptureFrame right before the frame is reserved (frame_sink_video_capturer_impl.cc:892);
capture_end_time in OnFrameReadyForDelivery after DidCopyFrame (:1490), i.e. after the CopyOutput result callback.

It is NOT "waiting for the next DrawAndSwap". The request is serviced by the same draw that triggered it [V]:
- SurfaceAggregator::PrewalkSurface calls `surface->NotifyAggregatedDamage()` (surface_aggregator.cc:2079) -> CompositorFrameSinkSupport::OnSurfaceAggregatedDamage
  (compositor_frame_sink_support.cc:402) -> capturer OnFrameDamaged (:709) -> MaybeCaptureFrame -> RequestCopyOfOutput (:1447, queued on the sink);
  the comment at surface_aggregator.cc:2074-2076 says exactly that copy requests may be added at this point, and
  `surface->TakeCopyOutputRequestsFromClient()` (:2085) grabs them before `CopyPasses` (:1527, `TakeCopyOutputRequests` :1534) runs. Display::DrawAndSwap has
  `should_draw = have_copy_requests || (have_damage && size_matches)` (display.cc:957).
- RequestCopyOfOutput also calls SurfaceModified(has_damage=true) (compositor_frame_sink_support.cc:1453), which only schedules an extra, normally skipped, draw.
So the 12.7 ms is the remainder of one draw: rest of Aggregate + SkiaRenderer DrawFrame recording on the viz thread + hop to the GPU main thread +
GPU execution of the whole frame (full 1080p damage in the colour-flip fixture) + the blit/CopyOutput into the shared image + GPU-finished callback hop back
(skia_output_surface_impl_on_gpu.cc:975-1115 blit path, FlushSurface finished proc :1166-1230, PostTaskToClientThread :159). [I] Split between those is unknown;
the component+dcheck build and GPU clock ramp-up at a sparse 30 fps load likely dominate. Nothing in the API surface is left to turn.
What could move it (all unverified): a non-dcheck/non-component viz+GPU build, GPU power state (high-performance preference for chrome.exe), NV12 output (see below).
NV12: `blit` NV12 adds one GPU pass in Viz (<1 ms) and removes a VPP BGRA->NV12 pass in the encoder path, a wash; only worth it if the oneVPL agent measures VPP > ~1 ms.
Before spending effort: trace once (section 5, T6) and read the FrameInUse / Graphics.Pipeline.Draw / CopyOutput slices.

## 2. Why 60 fps reaches only ~52 fps

Evidence: fps60 endpoint log "D3D source fps" is a flat 52.5-53.2 for 60 s (about 19.0 ms period, not noise); chrome log has
`vsync_thread_win_dxgi.cc:56 Failed to find DXGI output with matching monitor` / `:89 ... on monitor` repeatedly. At 30 fps the same chain gives 29.87 (token bucket hides it).

Chain [V]: RootCompositorFrameSinkImpl::Create uses ExternalBeginFrameSourceWin when dc_support_level != kNone (root_compositor_frame_sink_impl.cc:214-218);
it subscribes to the global VSyncThreadWin (external_begin_frame_source_win.cc:116). VSyncThreadWin::WaitForVSync: if WaitForVSyncImpl fails -> `Sleep(ceil(interval ms))`
(vsync_thread_win.cc:~140-156 at pin). VSyncThreadWinDXGI::WaitForVSyncImpl re-runs `FindDXGIOutputForMonitor` (CreateDXGIFactory1 + EnumAdapters + EnumOutputs + 2 DLOG(ERROR))
on EVERY tick because `primary_output_` stays null (vsync_thread_win_dxgi.cc:~150-185). So each tick = factory enumeration + Sleep(17) (16.67 ms rounded up) + thread wake => ~19 ms. [I] (matches 52.7 Hz).
Oracle [V]: with ticks >= min period every tick passes SmoothEventSampler (token bucket, smooth_event_sampler.cc:ConsiderPresentationEvent/ShouldSample), so capture rate == tick rate.
Endpoint is not the limiter: the 52.7 is the rate of frames arriving at D3DFrameSource (d3d_frame_source.cc:443 stat) before conversion; convert 6.5-7 ms < 19 ms. [I]

Fix A (no code): launch chrome with `--cr-fake-vsync-rate=N`. The switch is forwarded to the GPU process ([V] gl_switches.cc:217 in kGLSwitchesCopiedFromGpuProcessHost),
skips the DXGI wait entirely (vsync_thread_win.cc fake branch) but still does Sleep(ceil ms), so the period is Sleep(c)+~2 ms: N=60 -> 17+2 = 19 ms (no gain), N=75 -> Sleep(14)+2 ~ 16 ms (~62 Hz; capture drops 1 in ~25 ticks, 60 average, uneven).
Fix B (chromium.diff, 25 lines, only active when --fake-vsync-rate is passed, a test-only switch upstream): wait for absolute deadlines on a CREATE_WAITABLE_TIMER_HIGH_RESOLUTION timer
(falls back to Sleep if unsupported), no DXGI enumeration, no drift. Use `--cr-fake-vsync-rate=60` for stable 60.
Fix C (design/02 patch 10 external BeginFrame, ~200 lines, not implemented in tree: grep shows no PrtsFramePacer): precise too, plus endpoint-driven phase (input-triggered immediate frame saves ~half a tick on input->pixel).
Risks (b3-pacing.md section 5): OOPIF stale frames, late acks, wait_for_all_frame_sinks, video manual submissions, popups need their own pacer.
Safer: B. It touches one fallback branch of one GPU-process function, is reversible by dropping the switch, and does not change the Viz/cc contract. C is the later lever for input latency, not for fps.
Optional higher tick (T3): `--cr-fake-vsync-rate=120` at 30 fps shrinks the tick-phase wait (avg 9.5 ms -> 4.2 ms, P95 19 -> 8.3 ms), cost: cc/raster wakes at up to 120 Hz while the page animates, pages see 120 Hz rAF.

## 3. Sender pacing (libwebrtc at pin)

Facts [V]:
- Pacing rate = max(min_total_allocated, loss_based_target) * factor; factor defaults 2.5 before the first transport feedback, 1.1 after
  (goog_cc_network_control.cc:55-58, 662-666). In our case target ~ 12 Mbps cap (peer.cc:336-352) => 13.2 Mbps = 1.65 MB/s.
- Burst allowance: debt may reach send_burst_interval = min(40 ms default (network_types.h:251), kMaxBurstSize 63 kB / rate) (pacing_controller.cc:377-381, .h:85-91).
  So any frame <= ~63 kB leaves with zero pacer delay (if the pacer is idle); larger frames drain at the pacing rate.
  `RTCConfiguration::pacer_burst_interval` exists (peer_connection_interface.h:697, peer_connection_factory.cc:412) but the 63 kB cap makes it useless here: do not use.
- Factor knob: field trial `WebRTC-Video-Pacing/factor:F[,max_delay:ms]` (video_send_stream_impl.cc:396-399, used when TransportSeqNum ext is negotiated :494-513);
  precedence: ALR experiment > WebRTC-VideoRateControl pacing_factor > this. Trial source = base::FieldTrialList via WebRtcFieldTrials (webrtc_overrides/field_trial.cc:13, environment.cc:14; peer.cc:705 uses it).
  Cap guard: `WebRTC-Bwe-LimitPacingFactorByUpperLinkCapacityEstimate/Enabled/` clamps the pacing rate to the estimated upper link capacity (goog_cc_network_control.cc:104-108, 667-672): this is what keeps a high factor from bursting past a shaped link.
- ZeroHertz screenshare: only for content type kScreen (frame_cadence_adapter.cc); we set kFluid and measured source->encode 0.1 ms, so no cadence hold. `WebRTC-VideoFrameTrackingIdAdvertised` only adds an RTP header extension; no latency effect. WebRTC-Pacer-DrainQueue is already default-on (pacing_controller.cc:63).
- Pacer queue-time limit 2000 ms (kMaxExpectedQueueLength) is irrelevant at these sizes.

Estimated pacer delay for a frame of S kB, delay ~ (S-63)/R (R in kB/ms; R = 1.65 at factor 1.1, 3.0 at 2.0, 3.75 at 2.5, 4.5 at 3.0) [I, model]:

| S | f=1.1 | f=2.0 | f=2.5 | f=3.0 |
|---|---:|---:|---:|---:|
| 50 kB | 0 | 0 | 0 | 0 |
| 80 kB | 10 ms | 6 ms | 5 ms | 4 ms |
| 120 kB | 35 ms | 19 ms | 15 ms | 13 ms |
| 200 kB | 83 ms | 46 ms | 37 ms | 30 ms |

P50 frames in the harness are small (avg budget 50 kB at 12 Mbps, static page with a marker) => P50 gain ~0-1 ms; the colour-flip / keyframe tail (P95/P99) gains 10-30 ms. Recommended trial string (opt-in first):
`--webrtc-field-trials=WebRTC-Video-Pacing/factor:2.5/WebRTC-Bwe-LimitPacingFactorByUpperLinkCapacityEstimate/Enabled/`
Average bitrate is untouched (encoder target unchanged); only peak rate rises to ~30 Mbps. On loopback no cap issue. On the d25j5-c12 shaped link the shaper queue absorbs it; watch loss (drop-tail) and GCC backoff in the netem matrix.
Timer evidence: `bitrate_prober.cc:155 Probe delay too high (next - now = 12-15 ms)` appears 9-13 times per run (baseline1080/fps60 endpoint logs): the pacer task wakes 12-15 ms late at startup, i.e. the 15.6 ms Windows tick [I].
The endpoint has no window, so Win11 may ignore timer-resolution requests and apply timer throttling. main.cc (diff) calls SetProcessInformation(ProcessPowerThrottling, IGNORE_TIMER_RESOLUTION|EXECUTION_SPEED => opt out) and ActivateHighResolutionTimer(true). Expected: -0..2 ms P50, the more important effect is pacer/probe timing accuracy on P95. Unverified.
Direct measurement is missing today (latency.md "Direct per-frame packet-send/receive correlation is needed"): outbound-rtp `totalPacketSendDelay` ([V] rtcstats_objects.h:357) is exactly pacer+send-queue dwell. Snippet for the integrator (peer.cc is conflicted, so not in the diff): every 5 s
`pc_->GetStats(rtc::scoped_refptr<webrtc::RTCStatsCollectorCallback>)`, find `RTCOutboundRtpStreamStats`, log `total_packet_send_delay / packets_sent` (delta over interval), `bytes_sent`, `frames_encoded`, `total_encode_time`, `target_bitrate`. Then mean pacer delay per packet is directly comparable between trials.

## 4. Ranked levers (P50 / P95 ms removed, loopback 1080p30; relative to the M2 baseline in latency.md)

| # | Lever | P50 | P95 | Confidence / cost |
|---|---|---:|---:|---|
| 1 | oneVPL (readback 5.5 + I420 1.8 + OpenH264 9.7, tail 30) | -15 | -25 | given; other agent |
| 2 | Sender pacing: factor 2.5 + upper-capacity guard (opt-in trial) | 0..-1 | -10..-30 | medium; 0 code, test via totalPacketSendDelay; P95 only if frames > 63 kB |
| 3 | Tick source: fake-vsync 120 Hz + precise timer (chromium.diff) at 30 fps | -4..-5 | -8..-10 | medium [I]; GPU-proc 25 lines; cc runs at 120 Hz |
| 3b | Same at 60 Hz | -1 | -2 | fixes 52.7 -> ~60 fps (high confidence), the real reason for the patch |
| 4 | Endpoint hires timer + throttling opt-out (remotekit.diff) | 0..-2 | -2..-5 | low-medium; 12 lines; part of the 16 ms "unpartitioned" in latency.md |
| 5 | Viz capture 12.7: non-dcheck build / GPU power state | -2..-5 | -2..-4 | low; not a config issue, trace first (T6) |
| 6 | External BeginFrame (patch 10) | 0..-1 capture->display; -4..-8 input->pixel | -8 | medium; ~200 lines, OOPIF/ack risk, later |
| 7 | NV12 Viz output | 0..-1 | 0 | only if VPP cost shows up; touches prts_capture.cc:241 and the mojom nv12 path |
| 8 | Viewer (jitter buffer 0, submit->display 18.6 is the headless rVFC estimate) | 0 | 0 | nothing actionable |

With oneVPL plus levers 2-4: expected about -20..-27 ms P50 and -45..-65 ms P95 capture->display; not additive with certainty (rank 2 depends on the new encoder's frame-size distribution, which oneVPL changes).

## 5. Test plan for the integrator

Common: `bash /c/src/tmp/m1/test.sh 60 [args]` (restarts chain, runs `node tools/latency/run.mjs --no-mock`). Each trial 60 s + 3 s warm-up, repeat each twice (jitter between runs was 5-10 ms P50 in M2). Compare: capture->display P50/P80/P95/P99, input->decoded flip, `D3D source fps` lines in cp1.log, the endpoint `PRTS latency5s` stages (capture_to_encoded, convert), decoded fps, endpoint CPU. Do the oneVPL A/B first, then each lever on top of the better encoder.
- T0 baseline: `test.sh 60` and `test.sh 60 --fps=60` (expect D3D source ~52.7 fps at 60).
- T1 timer only: rebuild endpoint (`be.sh`), `test.sh 60 [--no-hires-timer]` A/B; look at count of "Probe delay too high" in cp1.log and pacer stats.
- T2 pacing: `test.sh 60 --webrtc-field-trials=WebRTC-Video-Pacing/factor:2.5/WebRTC-Bwe-LimitPacingFactorByUpperLinkCapacityEstimate/Enabled/`; also factor:2.0 and factor:3.0; confirm the log line "PRTS webrtc field trials". Then netem: `node tools/latency/run.mjs --no-mock --port 8300 --secs 60 --netem d25j5-c12 --remote-cdp 9333 --endpoint-log <log>` (netem.md line 25) baseline vs factor 2.5, and `d50j10-c12-wifi` as the loss stress; fail criterion: higher loss/freezes or lower decoded fps than baseline.
- T3 vsync (needs `bash /c/src/tmp/m1/be.sh chrome` with chromium.diff): `test.sh 60 --fps=60 --cr-fake-vsync-rate=60` (expect >=58 fps), `--cr-fake-vsync-rate=120` at fps 30 and 60. Control without the diff but with the switch: `--cr-fake-vsync-rate=75` (expect ~60-62 uneven). Check chrome CPU (cc at 120 Hz) and that "Failed to find DXGI output" lines disappear (fake branch skips them).
- T4 combined: oneVPL + T1 + T2 best + T3 best; report vs T0.
- T6 trace for lever 5 (read-only): add `--cr-trace-startup=viz,gpu,gpu.capture --cr-trace-startup-duration=8 --cr-trace-startup-file=C:\src\tmp\m3\levers\trace.json`, open in ui.perfetto.dev: slices "FrameInUse", "Graphics.Pipeline.Draw", "SkiaOutputSurfaceImplOnGpu::CopyOutput", "WaitForVSync Sleep" (vsync_thread_win.cc TRACE_EVENT0 gpu).
- Add the totalPacketSendDelay logger (section 3) before T2 so the pacer effect is measured directly.

## Open risks
Nothing compiled. chromium.diff uses CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS, base::win::ScopedHandle (header checked present); remotekit.diff relies on PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION which base/win/win_util.cc:1231 already uses (SDK has it).
fake-vsync also drives BeginFrame `interval`, so rAF/animations run at the fake rate. FieldTrialList::CreateTrialsFromString must run before the first WebRtcEnvironment() (it does: before Peer::Initialize). Pacer-delay and tick-phase numbers are models until T1-T3 are run.
