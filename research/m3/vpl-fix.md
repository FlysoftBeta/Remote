# m3-vpl-fix
Completed 2026-10-26. Endpoint-only source changes; no git commits, Chromium edits, full builds or suites. Incremental be.sh builds only. Existing timer/sender telemetry changes were preserved. All scripts/evidence below are task-local.

## Changes / files
- endpoint/vpl_encoder.{cc,h}: only TargetKbps/MaxKbps change at Reset. Fixed maximum-sized BufferSizeInKB/InitialDelayInKB/MaxFrameSize/FPS/SPS; direct Reset with OFF option and plain fallback, status and GetVideoParam verification. No rate-triggered rebuild. >=20% hysteresis, at most one request per2s after two seconds of frames. If runtime inserts an unrequested reset IDR, restore/hold negotiated maximum VBR after cooldown. Five-second reset attempts/successes/IDRs/rate/ignored counts.
- VPL starts at negotiated maximum under VBR LowDelayBRC; it adopts GCC target via seamless reset/hysteresis. Actual codec allocation uses the same start/min/max as OpenH264. Untrusted QP remains untrusted; a single supported-ceiling resolution-bitrate entry avoids WebRTC's default cap and 30kbps minimum/interpolation.
- endpoint/peer.cc: read min/max from the same RtpEncodingParameters fields into VplEncoderState; software encoder settings unchanged.
- endpoint/d3d_native_frame.{cc,h}: bounded leased private BGRA input + NV12 output + event per slot. Keyed mutex covers only CopyResource and ReleaseSync. VideoProcessorBlt/End(event)/nonblocking Flush run after release. No completion polling or interstage SyncOperation. MFX bitstream is the sole blocking sync; event completion is observed afterward without waiting.
- endpoint/d3d_frame_source.cc: corrected source-release comment.

## Build and functional evidence
- build-1.log, build-2.log, build-final.log: endpoint incremental build finished successfully. Explicit Chromium formatting; git diff --check passes with cr-at-eol (peer/source existing CRLF). Six source files changed. No test launcher/builder ran concurrently.
- Final build includes startup guard reset on every Start, unused FPS adaptation removal, ceiling bitrate table key, and max-VBR restoration branch. Formal timing binary differs only in those equivalent ordinary-path/exceptional-path cleanups and formatting; formal 60s runs were not repeated afterward.
- smoke-final-results.json and smoke-openh264-final-results.json: actual viewer viewport and decoded sizes1920x1080 ->1280x720 ->1920x1080, explicit ctl keyframe decoded counter5->6 for both, frames continue. Reviewed final VPL720p and OpenH2641080p screenshots: complete fixture/browser chrome, legible text/marker, no obvious corruption.
- smoke-plain-variant-chain.log: task-local temporary probe after90 frames invokes plain Reset to9000/12000kbps; status0 and readback0, VBV101KB/initial50KB/framecap50000B unchanged. Last IDR17:00:08.848, plain Reset17:00:10.677, no subsequent IDR through17:00:20 shutdown. Temporary probe removed before final build; rate-variants.mjs restores source in finally. First variant smoke without the explicit probe did not exercise Reset and was superseded.
- Production OFF-option reset verified in formal scrolling/game: status0/readback0, one/two successful resets respectively, zero steady IDRs/rebuilds. See archived logs and 5s stats.
- validation.json: eight60s runs, >1500 marker samples and120 input samples each; all marker failures/input timeouts/freezes zero, zero steady IDRs/rebuilds. No native slow readbacks or software fallbacks in VPL runs. Scenario evidence JSON confirms game update counter609->617; fixture URLs and unchanged scenarios.js define S/G.
- cleanup.log: test browser/endpoint processes stopped. Temporary plain-probe code absent from final sources.

## API verification
[V] means read at the pin/its SDK; [I] means inference.
- [V] vendored api/vpl/mfxvideo.h:346-397 Reset status semantics; GetVideoParam requires caller-owned extension buffers. mfxstructures.h:3400-3461 lists TargetKbps/MaxKbps as seamless-reset changes and documents OFF-option support variability. Existing Parameters copy rebinds extensions.
- [V] Chromium third_party/webrtc/video/video_stream_encoder.cc:1114-1157: untrusted-QP default table replaces app bitrate limits. rtc_base/experiments/encoder_info_settings.cc:126-197: interpolated minimum becomes30kbps; a single entry selects the supplied min/max without interpolation for every supported size. api/video_codecs/video_encoder.h ResolutionBitrateLimits constructor and EncoderInfo fields.
- [V] Windows SDK10.0.26100.0 um/d3d11.h:7934 GetData,8074 CopyResource,8557 Flush,14331 CreateQuery; shared/dxgi.h:795 ReleaseSync. Existing CreateTexture2D/VideoProcessor view/Blt calls now use private resources. Existing base/time/time.h TimeTicks arithmetic and latency API retained.
- [I] Same device/context ordering plus keyed ReleaseSync preserves the source-copy ordering and private texture lifetime. Correct streaming/resize on this UHD630 verifies this host; it does not prove every driver/adapter.

## Interpretation and remaining regressions
- Original VPL marker P95 was133-137ms with~120 rebuilds/~115 IDRs per chain. Now69.7-72.6ms, zero rebuilds, zero steady IDRs; mutex P50/P95~0.04/0.06-0.07ms instead of native VPP9-16ms under the mutex. Max sampled holds58-84us across VPL runs. VPP submit~0.21-0.22ms P50; post-bitstream completion observation~7.3-7.6ms P50 includes queue/encode and is only an upper bound.
- Marker VPL display is still worse: P9572.64/69.68 vs66.50/66.67ms; receiver capture->received is faster37.62/38.16 vs39.52/39.53ms P50, but receive->display is slower26.2/22.0 vs18.5/19.6ms. Presentation->display accounts for most of that gap (24.2/20.2 vs17.1/18.2ms). Encode alone is slower~7.23 vs4.35-4.48ms mean, while avoiding CPU staging/conversion makes capture->encoded faster~8.4 vs10.2ms P50. [I] Remaining marker display gap chiefly lies in receiver/compositor scheduling/phase, not a recurring reset stall. Quantiles are descriptive and not exactly additive.
- Scrolling improves every reported latency quantile: VPL capture/input P9572.52/96.4 vs105.80/130.2ms. Game capture P95/P9969.81/77.45 vs93.96/106.04ms improves, but VPL capture P50/P80 remains worse60.99/66.16 vs58.35/65.80ms, and game input P95 is worse94.9 vs93.0ms. Game receiver capture->received P5038.84 vs35.26ms locates that small median disadvantage before receiver display (receive->display22.0 vs22.6ms). Across the same run, mean encode is faster7.79 vs13.25ms; full capture/marker-render/pacer phase correlation was not collected, so the exact cause of the residual game median/input-tail difference is unverified.
- Marker input tails are mixed: run1 P9585.0 vs87.9ms improves, P9995.5 vs92.6ms worsens; run2 P95/P9991.4/105.4 vs85.4/94.5ms worsens despite near-equal median68.0 vs68.1ms. [I] Thirty-fps frame timing and120 unsynchronized input samples can affect these small tails; attribution requires more paired runs. No claim that VPL wins every metric.
- Equal codec limits do not imply equal emitted bitrate/quality: marker~0.43 vs0.26Mbps, scrolling9.45 vs10.17Mbps, game9.12 vs10.53Mbps. This is the actual encoder behavior at equal app settings, not matched-quality testing. No objective image-quality audit.

## Open risks / unverified
- Forced-IDR-on-reset recovery/hold-max branch, reset failure/readback mismatch fallback, pause/resume, driver removal/device loss, multi-adapter, sustained resize/pool pressure and memory stress were not fault-injected.
- Hysteresis intentionally delays network adaptation up to2s; initializing at max VBR may overshoot a constrained network before GCC settles. Only loopback was measured. OpenH264 source behavior/settings unchanged.
- Completion event gives an observation upper bound, not independent GPU timestamp VPP duration. Stage table uses last rolling5s, not merged whole-run distributions. Mutex maxima are periodic samples; rolling quantiles are also provided.
- Headless compositor display estimates are not physical scanout. Actual decoded size1890x938; final smoke covers full1080p and720p. Decoder implementation/power-efficiency fields are not exposed by this receiver. Release/60fps/other GPUs/Chrome89 remain unverified.

## Artifacts
- MEASUREMENTS.md: complete requested tables. PIPELINES.md: readable pipeline block per run. BREAKDOWN.md: receiver-stage table. measurements.json: structured combined results/counters/pipelines. validation.json: checks.
- Each run directory: original latency JSON/CSV. Sibling *-chain.log and *-run.log are archived originals. Raw reports retain the existing tool's old native-path wording; combined pipeline blocks correct it, without changing tools/latency.
- NOTES-progress.md preserves incremental notes. Scripts and temporary draft source are under this task directory only.

# Measurements

Eight 60s loopback/headless Edge runs, sequential and interleaved by encoder. UHD 630, legacy MSDK API 1.35, component/dcheck. Viewer outer 1920x1080, decoded 1890x938; 30fps. rVFC compositor estimates, not physical scanout. Scenario S/G are unchanged tools/latency/scenarios.js.

P50/P80/P95/P99 milliseconds. Sender Mbps uses interior 5s outbound-rtp snapshots (roughly 50-55s); RTP payload bytes including retransmits, excludes headers.

| Run | Capture->display | Input->flip | Samples capture/input | FPS | Send Mbps |
|---|---|---|---|---|---|
| vpl-marker-1 | 63.64/69.10/72.64/76.30 | 66.60/74.50/85.00/95.50 | 1783/120 | 29.99 | 0.43 |
| openh264-marker-1 | 58.36/63.30/66.50/73.48 | 70.80/79.30/87.90/92.60 | 1782/120 | 30.03 | 0.26 |
| vpl-marker-2 | 60.29/64.97/69.68/74.29 | 68.00/79.40/91.40/105.40 | 1780/120 | 29.96 | 0.43 |
| openh264-marker-2 | 59.17/63.81/66.67/69.97 | 68.10/78.70/85.40/94.50 | 1776/120 | 29.99 | 0.26 |
| vpl-S | 63.12/67.84/72.52/98.90 | 71.90/84.90/96.40/105.70 | 1725/120 | 30.00 | 9.45 |
| openh264-S | 70.90/77.74/105.80/129.02 | 83.20/97.00/130.20/146.20 | 1589/120 | 28.99 | 10.17 |
| vpl-G | 60.99/66.16/69.81/77.45 | 68.20/82.20/94.90/103.30 | 1785/120 | 30.01 | 9.12 |
| openh264-G | 58.35/65.80/93.96/106.04 | 70.00/82.30/93.00/108.10 | 1671/120 | 29.00 | 10.53 |

Latest rolling 5s stage percentiles P50/P95 ms (n), not merged whole-run percentiles. Completion observation is an upper bound from VPP submit until event checked after bitstream sync; includes queue/encode, not isolated GPU VPP time. Shared mutex covers copy submission only.

| Run | Mutex hold | VPP submit | VPP completion observed | Staging/map | CPU convert | Encode callback | Capture->encoded |
|---|---|---|---|---|---|---|---|
| vpl-marker-1 | 0.04/0.06 (149) | 0.22/0.30 (149) | 7.64/8.01 (150) | - | - | 7.24/7.61 (150) | 8.47/8.88 (150) |
| openh264-marker-1 | - | - | - | 4.06/4.71 (149) | 0.77/1.01 (149) | 4.12/6.06 (150) | 10.11/12.31 (150) |
| vpl-marker-2 | 0.04/0.07 (149) | 0.22/0.28 (149) | 7.56/7.95 (149) | - | - | 7.19/7.60 (149) | 8.41/9.01 (149) |
| openh264-marker-2 | - | - | - | 4.13/4.75 (149) | 0.77/1.01 (149) | 4.15/5.86 (149) | 10.21/12.20 (149) |
| vpl-S | 0.04/0.06 (150) | 0.21/0.29 (150) | 7.50/11.51 (149) | - | - | 7.18/11.24 (149) | 8.33/12.29 (149) |
| openh264-S | - | - | - | 4.15/5.41 (149) | 0.86/1.93 (149) | 10.65/12.33 (146) | 16.94/19.59 (146) |
| vpl-G | 0.04/0.06 (149) | 0.21/0.28 (149) | 7.30/11.22 (150) | - | - | 6.98/10.84 (150) | 8.10/12.19 (150) |
| openh264-G | - | - | - | 4.33/7.49 (150) | 0.82/1.42 (150) | 13.56/15.53 (147) | 20.19/22.82 (147) |

| Run | Codec start/min/max kbps | Last effective encoder readback |
|---|---|---|
| vpl-marker-1 | 2000/2000/12000 | GetVideoParam status=0 kbps=12000/12000 fps=30 VBV_KB=101 initial_KB=50 cap_bytes=50000 LDB=16 LP=16 rate_control=2 AsyncDepth=1 level=40 profile=578 |
| openh264-marker-1 | 2000/2000/12000 | OpenH264 camera; 4 thread limit |
| vpl-marker-2 | 2000/2000/12000 | GetVideoParam status=0 kbps=12000/12000 fps=30 VBV_KB=101 initial_KB=50 cap_bytes=50000 LDB=16 LP=16 rate_control=2 AsyncDepth=1 level=40 profile=578 |
| openh264-marker-2 | 2000/2000/12000 | OpenH264 camera; 4 thread limit |
| vpl-S | 2000/2000/12000 | Reset-readback status=0 kbps=9600/12000 fps=30 VBV_KB=101 initial_KB=50 cap_bytes=50000 LDB=16 LP=16 rate_control=2 AsyncDepth=1 level=40 profile=578 |
| openh264-S | 2000/2000/12000 | OpenH264 camera; 4 thread limit |
| vpl-G | 2000/2000/12000 | Reset-readback status=0 kbps=10488/12000 fps=30 VBV_KB=101 initial_KB=50 cap_bytes=50000 LDB=16 LP=16 rate_control=2 AsyncDepth=1 level=40 profile=578 |
| openh264-G | 2000/2000/12000 | OpenH264 camera; 4 thread limit |

VPL initializes Target/Max at the negotiated ceiling under VBR LowDelayBRC, then follows GCC through hysteretic seamless resets. Initial codec allocation still uses the same VideoCodec / RtpEncodingParameters values as software.

Counters: whole chain includes warmup/initial resize. Steady IDRs are sender snapshot deltas over the send-rate interval. Mutex maxima sample every120 frames; full rolling distributions above.

| Run | Successful resets/attempts | Whole chain IDRs | Steady IDRs | Rate rebuilds | Max sampled mutex us | Slow native readbacks |
|---|---|---|---|---|---|---|
| vpl-marker-1 | 0/0 | 2 | 0 | 0 | 84 | 0 |
| openh264-marker-1 | 0/0 | 1 | 0 | 0 | - | 0 |
| vpl-marker-2 | 0/0 | 1 | 0 | 0 | 63 | 0 |
| openh264-marker-2 | 0/0 | 2 | 0 | 0 | - | 0 |
| vpl-S | 1/1 | 1 | 0 | 0 | 58 | 0 |
| openh264-S | 0/0 | 2 | 0 | 0 | - | 0 |
| vpl-G | 2/2 | 2 | 0 | 0 | 175 | 0 |
| openh264-G | 0/0 | 1 | 0 | 0 | - | 0 |

Complete pipeline blocks and validation data: measurements.json; raw latency JSON/CSV and chain logs are unchanged. VPL pipeline corrects the existing report tool's old path wording; no tool source edits.

# Residual latency breakdown

All values ms P50/P95. Receiver metrics cover full measurement; endpoint stages are final rolling5s. Percentiles do not add exactly.

| Run | Capture->received | Received->display | Receive->presentation | Presentation->display | Decoder processing |
|---|---|---|---|---|---|
| vpl-marker-1 | 37.62/40.54 | 26.20/34.10 | 1.60/2.90 | 24.20/32.20 | 1.40/2.30 |
| openh264-marker-1 | 39.52/42.96 | 18.50/26.30 | 1.40/1.90 | 17.10/25.00 | 1.10/1.70 |
| vpl-marker-2 | 38.16/41.16 | 22.00/31.70 | 1.60/2.70 | 20.20/30.00 | 1.40/2.20 |
| openh264-marker-2 | 39.53/42.53 | 19.60/26.70 | 1.30/2.00 | 18.20/25.20 | 1.10/1.60 |
| vpl-S | 41.52/47.20 | 20.90/28.50 | 3.10/6.50 | 17.10/25.10 | 2.80/4.10 |
| openh264-S | 50.18/88.90 | 20.10/29.10 | 3.10/6.80 | 16.50/25.20 | 2.80/4.10 |
| vpl-G | 38.84/42.45 | 22.00/30.10 | 4.80/7.90 | 16.90/24.90 | 4.50/6.10 |
| openh264-G | 35.26/73.10 | 22.60/31.00 | 4.70/9.30 | 17.50/25.60 | 4.40/6.10 |
