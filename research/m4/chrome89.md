# Chrome 89 viewer acceptance (M4)

2026-10-27, current out/dev sender; status: complete. Eleven 60 s harness runs plus one 60 s field run completed. All six shaped Chrome 89 capture rows pass; zero of six input rows pass, so overall acceptance FAIL. All evidence under `C:/src/tmp/m4/chrome89/`; no commits, no builds requested, out/rel untouched except setting background build process priority to Idle before each run.

Primary: `summary.latDecoded` / `summary.i2decoded` end at rVFC `presentationTime`; target P80<100 / P99<200 ms on d25j5. Old glass estimates use `expectedDisplayTime` (`lat`, `i2display`); old input->flip uses callback-time colour detection (`i2p`). Capture begins at embedded source page draw, as in M3, not capture_end.

Genuine signed Google Chrome **89.0.4389.90**, headed. Fixed profile and requested update/background flags retained. Plain headed attempt disconnected ~0.22 s after offer, before answer; CDP page list became empty while browser survived. The documented Win11 workaround `--no-sandbox --enable-features=NetworkServiceInProcess` kept it working, still headed. This is a host compatibility requirement, not a sender change. Failed logs: loop-marker-r1 / loop-marker-valid-r1; valid loopback rows: loop-marker-r2 and loop-marker-repeat.

Current viewer window is requested at 1928x1162 but constrained by desktop to **1684x939** CSS px; sender rounds to **1684x940**, SPS level **4.2**. This task validates the current viewport, not 1080p. Capture is the current shared-fence path and oneVPL D3D11 hardware encoder, not M3's older mutex path. V/G retain the 30 fps canvas content workload; capture/marker request 60 fps.

**Measurement caveat:** Chrome 89 reports `presentationTime == expectedDisplayTime` in all collected rVFC samples; its callback arrives ~20 ms later. Consequently its decoded and glass quantiles are identical. Reported decoded fps is ~60, while rVFC/pixel observations are ~28 fps (many marker IDs skipped). These exact API endpoints are retained, but numerical gains versus Edge/M3 must not be interpreted as a physical-display improvement or proof of the decoder's internal completion instant. Clock uses symmetric minimum-RTT mapping; shaped asymmetry can bias it.

## H.264 and playout evidence

[V runtime] `loop-marker-r2/snapshot1.json` / `snapshot2.json`: UA Chrome/89.0.4389.90, visible, video 1684x940, `video/H264`, payload 108, fmtp `level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f`. `framesDecoded` rises **887 -> 2025** over 19.8 s; decoderImplementation **ExternalDecoder**. This identifies Chromium's external decoder adapter, not a specific hardware engine; powerEfficientDecoder is absent, so hardware decode is not conclusively established. SPS logged from actual Annex-B NALs: `PRTS VPL SPS profile=66 constraints=64 level=42` (SDP nominal 3.1 differs; level asymmetry allowed).

[V runtime] Answer: `a=extmap:5 http://www.webrtc.org/experiments/rtp-hdrext/playout-delay`. Sender logs `playout_extension=1 playout_metadata=1 playout_min_ms=0 playout_max_ms=0`, and negotiated `playout_extension=1`. JS reads `playoutDelayHint=0`; jitterBufferTarget is absent. However the legacy `track` counter ratio is ~30 ms/frame on loopback, **not** near zero. This fails the literal counter check; exact M89 source below shows that this counter sums modeled current delay, rather than literal buffer residence. Negotiation alone does not establish packet-by-packet delivery of (0,0). Unset-JS comparison follows below, keeping endpoint flags unchanged.

## Compatibility and source checks

[V runtime] rVFC exposes presentationTime/expectedDisplayTime/receiveTime/processingDuration/rtpTimestamp; setCodecPreferences works, negotiated DataChannels/probe/input work, getStats exposes framesDecoded/decoderImplementation. Legacy remote-source `track` holds jitterBufferDelay/EmittedCount; inbound-rtp holds totalDecodeTime. Absent newer fields remain optional. Viewer and loaded helpers use Chrome 89-compatible syntax; no syntax downgrade was needed. Node-only `run.mjs` uses modern Node APIs, not page JavaScript.

[V source at M89 tag] `api-source/receiver.txt`: playoutDelayHint and getCapabilities/getStats in receiver IDL. `rvfc_impl.txt:203-209` maps presentation and display separately; `decoder.txt:429-430` names RTCVideoDecoderAdapter ExternalDecoder. [Chromium M89 receiver IDL](https://chromium.googlesource.com/chromium/src/+/89.0.4389.90/third_party/blink/renderer/modules/peerconnection/rtc_rtp_receiver.idl), [rVFC implementation](https://chromium.googlesource.com/chromium/src/+/89.0.4389.90/third_party/blink/renderer/modules/video_rvfc/video_frame_callback_requester_impl.cc). [V source] M89 DEPS pins WebRTC e7d9f74b256605556726c64dacd518a105ba872f; `api-source/timing.txt:173-179` returns immediate render for min=max=0. `stats.txt:772-775` places video jitter counters in track stats. [Pinned timing source](https://webrtc.googlesource.com/src/+/e7d9f74b256605556726c64dacd518a105ba872f/modules/video_coding/timing.cc). [V local pin] endpoint/vpl_encoder.cc:1158-1173 verifies emitted SPS. [I] These source paths support API compatibility; they do not establish why the observed jitter counter remains nonzero.

## Latency results

All quantiles P50/P80/P95/P99 ms, each run 60 s after existing 3 s warmup. FPS is mean of 1 Hz framesDecoded deltas over the report stats samples; rVFC fps is separate. Build column records clang-cl running at batch start; priority set Idle.

| Run | capture->decoded | input->decoded | decoded FPS | backend actually used | rel build running | capture/input verdict |
|---|---|---|---:|---|---|---|
| loop-marker-r2 | 16.8/20.0/32.3/33.8 (1703) | 60.4/64.8/79.1/79.2 (119) | 60.03 | PRTS oneVPL D3D11 AVC | yes (16) | PASS / PASS |
| loop-marker-repeat | 17.2/20.3/33.1/34.3 (1706) | 61.6/66.4/78.6/79.9 (119) | 60.01 | PRTS oneVPL D3D11 AVC | yes (16) | PASS / PASS |
| d25-S-r1 | 73.7/82.1/94.5/118.8 (1514) | 145.8/159.9/185.8/253.2 (104) | 55.94 | PRTS oneVPL D3D11 AVC | yes (15) | PASS / FAIL |
| d25-V-r1 | 52.4/62.9/70.5/75.9 (1690) | 115.1/126.2/136.6/144.1 (111) | 57.25 | PRTS oneVPL D3D11 AVC | yes (16) | PASS / FAIL |
| d25-G-r1 | 72.6/78.6/86.9/94.0 (1671) | 138.3/150.3/159.1/167.8 (110) | 57.37 | PRTS oneVPL D3D11 AVC | yes (16) | PASS / FAIL |
| d25-S-r2 | 60.6/72.7/80.9/101.3 (1453) | 133.5/145.5/162.9/242.3 (104) | 55.46 | PRTS oneVPL D3D11 AVC | yes (16) | PASS / FAIL |
| d25-V-r2 | 58.3/66.9/73.1/82.9 (1629) | 120.3/132.3/145.5/153.8 (110) | 57.43 | PRTS oneVPL D3D11 AVC | yes (16) | PASS / FAIL |
| d25-G-r2 | 70.6/81.1/90.4/98.2 (1672) | 137.3/150.0/161.2/166.0 (109) | 57.06 | PRTS oneVPL D3D11 AVC | yes (16) | PASS / FAIL |
| d25-S-hint-off | 58.3/65.0/78.1/96.4 (1461) | 137.1/145.9/156.2/238.9 (105) | 55.91 | PRTS oneVPL D3D11 AVC | yes (16) | PASS / FAIL |
| d25-S-Edge | 75.9/81.6/92.4/113.4 (2243) | 126.3/136.8/148.3/160.2 (104) | 56.03 | PRTS oneVPL D3D11 AVC | yes (16) | PASS / FAIL |
| loop-S-field-reference | 19.3/31.3/35.4/38.0 (1526) | 63.9/67.6/80.8/82.1 (104) | 56.20 | PRTS oneVPL D3D11 AVC | yes (16) | PASS / PASS |

| Run | capture->display glass | input->display glass | input->flip callback (M3) | rel build running |
|---|---|---|---|---|
| loop-marker-r2 | 16.8/20.0/32.3/33.8 (1703) | 60.4/64.8/79.1/79.2 (119) | 82.6/89.4/99.8/103.9 (119) | yes |
| loop-marker-repeat | 17.2/20.3/33.1/34.3 (1706) | 61.6/66.4/78.6/79.9 (119) | 82.9/85.8/100.0/105.9 (119) | yes |
| d25-S-r1 | 73.7/82.1/94.5/118.8 (1514) | 145.8/159.9/185.8/253.2 (104) | 166.2/180.6/204.3/272.8 (104) | yes |
| d25-V-r1 | 52.4/62.9/70.5/75.9 (1690) | 115.1/126.2/136.6/144.1 (111) | 140.8/148.0/161.3/169.0 (111) | yes |
| d25-G-r1 | 72.6/78.6/86.9/94.0 (1671) | 138.3/150.3/159.1/167.8 (110) | 165.8/173.8/183.0/190.6 (110) | yes |
| d25-S-r2 | 60.6/72.7/80.9/101.3 (1453) | 133.5/145.5/162.9/242.3 (104) | 157.4/167.5/186.5/266.2 (104) | yes |
| d25-V-r2 | 58.3/66.9/73.1/82.9 (1629) | 120.3/132.3/145.5/153.8 (110) | 142.0/157.0/166.9/176.9 (110) | yes |
| d25-G-r2 | 70.6/81.1/90.4/98.2 (1672) | 137.3/150.0/161.2/166.0 (109) | 161.5/173.1/181.0/184.9 (109) | yes |
| d25-S-hint-off | 58.3/65.0/78.1/96.4 (1461) | 137.1/145.9/156.2/238.9 (105) | 160.9/167.0/184.5/261.5 (105) | yes |
| d25-S-Edge | 93.4/100.5/111.5/129.9 (2243) | 142.8/157.1/169.7/178.2 (104) | 137.9/149.8/160.8/172.9 (104) | yes |
| loop-S-field-reference | 19.3/31.3/35.4/38.0 (1526) | 63.9/67.6/80.8/82.1 (104) | 83.8/92.8/100.4/101.9 (104) | yes |

## Receiver and transport diagnostics

Jitter/decode are means of the saved 1 Hz interval ratios, not subtraction of quantiles. Raw counters are in each report. NACK/PLI/loss are whole-connection totals, including warmup.

| Run | JB ms/frame | decode ms/frame | rVFC fps | framesDecoded at end | NACK / PLI / lost | freezes >200 ms | marker failed / frames | send Mbps | rel build running |
|---|---:|---:|---:|---:|---|---:|---|---:|---|
| loop-marker-r2 | 30.12 | 2.84 | 28.38 | 3796 | 0/0/0 | 0 | 0/1703 | - | yes |
| loop-marker-repeat | 28.97 | 2.47 | 28.43 | 3812 | 0/0/0 | 0 | 0/1706 | - | yes |
| d25-S-r1 | 38.41 | 4.17 | 28.52 | 3455 | 12/0/37 | 0 | 177/1711 | 8.35 | yes |
| d25-V-r1 | 40.61 | 3.82 | 29.80 | 3560 | 0/0/0 | 1 | 89/1788 | 2.52 | yes |
| d25-G-r1 | 42.24 | 4.65 | 29.70 | 3536 | 0/0/0 | 1 | 96/1782 | 5.89 | yes |
| d25-S-r2 | 37.06 | 4.38 | 27.48 | 3427 | 30/1/134 | 1 | 185/1649 | 8.26 | yes |
| d25-V-r2 | 45.59 | 4.26 | 29.32 | 3539 | 0/0/0 | 1 | 107/1759 | 4.38 | yes |
| d25-G-r2 | 41.77 | 4.36 | 29.93 | 3525 | 0/0/0 | 1 | 111/1796 | 5.58 | yes |
| d25-S-hint-off | 37.15 | 4.27 | 27.27 | 3478 | 16/0/77 | 0 | 160/1636 | 8.47 | yes |
| d25-S-Edge | 13.42 | 4.38 | 43.05 | 3464 | 6/0/9 | 0 | 221/2583 | 8.23 | yes |
| loop-S-field-reference | 32.33 | 3.08 | 28.23 | 3480 | 0/0/0 | 0 | 168/1694 | - | yes |

## Interpreting the M89 jitter counter

[V source at exact M89 WebRTC revision] `api-source/receive_statistics_proxy.cc.txt:647-650` sets `jitter_buffer_delay_seconds = current_delay_counter_.Sum(...) / 1000` and emitted count to `current_delay_counter_.NumSamples()`. Lines 667-683 accumulate the timing model's `current_delay_ms`; this is not a direct packet/frame residence-time measurement. At the same revision `VCMTiming::RenderTimeMsInternal` returns 0 for min=max=0 (timing.txt:173-179), bypassing the estimated render delay. The observed ~30-40 ms counter ratio can coexist with immediate render [I]; it cannot establish that the RTP limit was ignored. Negotiation succeeds; the literal near-zero-counter check does not. Proving the actual on-wire extension values and buffer residence separately remains open. [Pinned M89 receive statistics](https://webrtc.googlesource.com/src/+/e7d9f74b256605556726c64dacd518a105ba872f/video/receive_statistics_proxy.cc).

[V source] `api-source/video_frame_compositor.cc:174-175` (M89 media/blink) stores `NowTicks` as presentation time and the supplied expected display separately. `video_frame_metadata.idl` documents both fields; these are actual M89 API values, not fallback substitutions. `viewer-system-info.json` successfully calls SystemInfo.getInfo on the M89 browser target and reports Intel UHD 630 with video_decode enabled. This is hardware capability evidence; ExternalDecoder alone still does not identify the selected decode engine.
## Acceptance and comparison

All six Chrome 89 shaped S/V/G rows meet capture->decoded P80<100/P99<200, but **none** meets input->decoded P80<100. Both S repetitions additionally fail input P99<200. Acceptance for the complete pipeline therefore fails. Runs differing by less than ~20 ms are treated as noise; the S input tail failure repeats, whereas fine ordering of captures does not establish an effect. Capture quantiles condition on valid unique pixel markers; S has ~10-11% failed marker reads. Input colour flips and all-rVFC freeze counts are independent of valid marker samples.

M3 c12 capture->display P80 was 101.7-124.0 ms (S:108.6/124.0, V:107.2/101.7, G:104.2/112.6), callback input P80 137.9-164.7 ms, mean decoded fps ~57-59. Our Chrome 89 glass numbers are numerically lower, but its callback input P80 is not consistently better. M3 mostly used headless Edge and 1898x1020, versus headed Chrome 89 at 1684x940; current capture uses fence and the background rel build is running. The M4 capture-phase baseline also reported shaped capture->decoded P80 135-143 ms during the build. These are confounded comparisons, not isolated evidence of Chrome 89 gains. The same-session headed Edge row below is a closer reference, with viewport difference and single-run noise still relevant. See [M3 matrix](../m3/matrix2-measurements.md) and [capture phase](capture-phase.md).
## JS-hint comparison

`d25-S-hint-off` changes only the viewer URL to `delay=off`; endpoint experiment and negotiated settings retain playout extension enabled and metadata (0,0), FEC off, VPL60 and the same pacer. Answer still includes extmap 5. Mean legacy JB ratio is **37.15 ms/frame**, compared with **38.41 / 37.06** in S r1/r2 with JS hint zero. Capture decoded P80/P99=65.0/96.4, input=145.9/238.9 ms; capture/glass identical on M89. Compare zero-hint S r2=72.7/101.3 and145.5/242.3. These differences are below ~20 ms and are not an established benefit of unsetting the hint. The literal near-zero counter expectation fails with either hint setting, with the M89 accounting caveat above.
## Same-session Edge reference

Default viewer remained Edge **156.0.0.0**, headed, actual video **1676x932** (Chrome 89 1684x940); sender oneVPL/fence unchanged. d25 S capture->decoded P50/P80/P95/P99 **75.9/81.6/92.4/113.4**, input->decoded **126.3/136.8/148.3/160.2**. Glass capture **93.4/100.5/111.5/129.9**, glass input **142.8/157.1/169.7/178.2**, old callback input **137.9/149.8/160.8/172.9**. Capture primary passes, input fails P80. Edge's presentation->display is nonzero, unlike Chrome 89. Its capture primary is within ~20 ms of both Chrome 89 S rows; no repeat-backed capture advantage established. Chrome 89 S input tails exceed 240 ms in both rows versus this single Edge P99 160.2 ms, but this is one Edge reference with stochastic queue drops and build load, not a definitive browser causality result.
## Field-kit runtime check

Started `tools/field-test/start-server.ps1 -BinDir C:/src/chromium/src/out/dev -Port 8300 -Scenario S -Seconds 60 -OutDir C:/src/tmp/m4/chrome89/field-runs`. Selected printed **http://192.168.3.31:8300/?measure=60&scenario=S**, on this same host's LAN address. This validates the direct-LAN runtime/report path, not a second device/Wi-Fi link. Build was running (16 clang-cl processes, priority set Idle).

Fixed one runtime launcher bug: `(Get-Command node -CommandType Application).Source` returned an array when fnm and system Node were both on PATH; invocation failed with their paths concatenated. The launcher now selects the first Application match. No control-plane changes were needed. Initial plain standalone headed Chrome decoded ~60 fps but produced no rVFC samples, leaving Start disabled; screenshots timed out and desktop CopyFromScreen returned invalid handle. Relaunched **headed** with the same four harness background/occlusion switches (`--disable-background-timer-throttling --disable-renderer-backgrounding --disable-backgrounding-occluded-windows --disable-features=CalculateNativeWinOcclusion`), retaining the requested Chrome flags and Win11 compatibility switches. Start enabled and screenshot capture then worked. This host's occluded/noninteractive desktop is a limitation; it does not establish behavior on a normally visible physical client desktop.

The field page performed all timing, probes, snapshot and upload itself. CDP DOM/box queries only located/inspected Start; Input.dispatchMouseEvent tapped it at (1408.45,172.5). No Runtime.evaluate, harness report polling or injected measurement was used for the field run. `field-start-tap.json` records the tap, `field-launch.json` the full flags/URL, and `field-after.png` shows the Saved UI (screenshot taken after measurement).

Saved report: `C:/src/tmp/m4/chrome89/field-runs/20261027-034406-055-18824/field-2026-10-26T19-47-29-882Z-326e80b2.json` (94,018 bytes); control-plane stdout: `19:47:29.889 field report saved: field-2026-10-26T19-47-29-882Z-326e80b2.json`. `kind=field-latency`, UA Chrome89, scenario S, VPL native/fence in stderr, actual SPS level42, 1684x940 H.264 / ExternalDecoder, end framesDecoded4256. Zero marker failures (1729/1729), timeouts, loss, NACK, PLI, frame drops and >200 ms freezes. These are direct/unshaped results; do not compare them against shaped S as a network-equivalent run.

| Run (build running) | capture->decoded | input->decoded | capture->display glass | input->display glass | old input->flip callback |
|---|---|---|---|---|---|
| Direct S harness (yes, 16) | 19.3/31.3/35.4/38.0 | 63.9/67.6/80.8/82.1 | 19.3/31.3/35.4/38.0 | 63.9/67.6/80.8/82.1 | 83.8/92.8/100.4/101.9 |
| Field S (yes, 16) | 18.0/20.4/33.3/36.0 | 61.0/66.2/77.7/79.9 | 18.0/20.4/33.3/36.0 | 61.0/66.2/77.7/79.9 | 81.4/85.4/99.2/106.7 |

P80/P99 deltas field minus harness: capture **-10.9/-2.1 ms**, input **-1.5/-2.2 ms**; within the stated ~20 ms noise range. Runtime/save path confirmed; mean decoded FPS **60.07**, actual backend oneVPL D3D11 throughout. Detailed comparison and raw stats: `field-comparison.json`.

## Behavior, changes and remaining limits

After the Win11 workaround, no sustained connection failure or explicit H.264 decode error was found in saved browser logs. All 11 harness reports used oneVPL native hardware encoding with no OpenH264 fallback. S had NACK12/30, PLI0/1 and receiver loss37/134 in the two shaped rows; V/G had zero NACK/PLI. Isolated >200 ms rVFC gaps occur in several V/G rows and S r2 (table above), similar to M3's isolated startup gaps; no causal attribution was recorded. No ongoing resolution oscillation: all Chrome89 rows settle at1684x940, Edge1676x932. Failed initial plain-headed negotiations and the first field occlusion issue are retained, not counted as successful measurements.

Files changed: `tools/latency/run.mjs` (viewer executable alias, quoted extra flags only for viewer, launch metadata, full browser stderr artifact); `viewer/viewer.js` (preserve codec fmtp and selected jitter-counter source including legacy track); `tools/latency/PRESETS.md` (reproduction); `tools/field-test/start-server.ps1` (first Node executable match); this report. No syntax downgrade was required, no Chromium/C++ or control-plane edits, no builds or commits.

Validation: Chrome89 parsed all eight loaded page scripts (`chrome89-script-audit.json`); Browser.getVersion, Runtime.evaluate, Performance.enable/getMetrics/disable and SystemInfo.getInfo worked, as did DOM query/box, Input.dispatchMouseEvent and post-workaround Page.captureScreenshot. Node syntax checks passed for changed JS and field helper, PowerShell parser found no errors, `git diff --check` passed. `validation.json` verifies 11 complete 60 s harness rows, finite samples for all five timing metrics, H.264, negotiated extension, shaped relay/relay pairs, VPL throughout and recorded background build status; completeness PASS is distinct from input acceptance FAIL. First successful loopback pipeline was recollected from a UTF8-normalized/unwrapped PowerShell log because the initial task driver emitted UTF16; original report/log are retained, latency values unchanged. Subsequent driver writes direct UTF8.

Open: specific hardware decode engine (ExternalDecoder is the GPU-factory adapter; hardware path is inferred, no D3D11/DXVA-specific name or powerEfficientDecoder), actual packet-level extension values, actual buffer residence versus M89 modeled delay, physical scanout, same-frame VideoFrame timestamp validation on M89, a normally interactive headed desktop/two-machine field link, 1080p at60 on this old receiver (current viewport was tested), objective visual quality, release binaries and build-free repeated A/B. The background release build was never killed or modified; only process priority was set Idle.
