# Video pipeline, pacing and measurement

Target: 1080p, 10-16 Mbps, RTT 50 ms + jitter, one peer on Wi-Fi; capture->display P80 < 100 ms, P99 < 200 ms (acceptance ends at the
decoded frame, see [measurement](#measurement)); receiver floor Chrome 89 + H.264. Decisions: [decisions](decisions.md) D4-D6, D12, D14, D15.

## Overview
```text
[GPU process / viz]                         [browser]            [re (C++, in-tree)]
FrameSinkVideoCapturerImpl (per window root)
  damage -> oracle -> CopyOutputRequest (D3D11 pool, same DrawAndSwap)
  signal shared fence, deliver {NT handle, token, value, LUID} --Mojo--> FrameImport (open-once cache)
                                                                          GPU Wait(fence) -> VPP BGRA->NV12
                                                                          encoder (oneVPL / NVENC / OpenH264)
                                                                          libwebrtc: RTP, pacer, GCC/TWCC, NACK/RTX -> UDP -> viewer
```
One source per browser window (popups stay in the root, D4b); with several windows only the shown one is encoded (switch = refresh + IDR).

## Capture (browser / viz side)
- Configuration: [browser](browser.md#details-worth-keeping). A static page produces no frames: new viewers and keyframes call
  `RequestRefreshFrame()` (revives the old frame without a new GPU copy). Patch 04 (`CompleteCapture`) is required.
- Resize: frames whose `coded_size/visible_rect/content_rect` do not match the root are dropped; the encoder is reconfigured with an IDR.
- Per-frame metadata: `frame_seq`, buffer id (`DXGIHandleToken`), sizes/rects, `capture_begin/end_time`, `reference_time`.
- **Shared fence hand-off (default, patch 19)**: viz submits the copy, signals a shared D3D11 fence and delivers `{handle, token, value, LUID}`
  immediately; `re` opens the token once, queues `ID3D11DeviceContext4::Wait` on its device, runs VPP and encode; a consumed receipt gates
  `Done()` and pool reuse. Viz capture segment P50 14.8 -> 1.3 ms. Fallback `--capture-sync=mutex` (keyed mutex key 0, `AcquireSync` accepting
  only `S_OK`). Evidence: [shared-fence](research/m4/shared-fence.md), [fence-impl](research/m4/fence-impl.md).
- GPU restart: patch 18 reconnects capturers and asks `re` to drop caches and send an IDR ([gpu-recover](research/m3/gpu-recover.md)).

## Endpoint options
| Option | Maturity (P99-critical) | Verdict |
|---|---|---|
| **C++ in the Chromium tree, libwebrtc at the pin** | highest: same lineage as the Chrome receiver (GCC/TWCC, probing, pacer, NACK/RTX, ULPFEC/FlexFEC, playout-delay, abs-capture-time); shares `.mojom` and `GpuMemoryBufferHandle`; reuses checkout and toolchain | **adopted** |
| C++, standalone libwebrtc checkout | same | rejected: second gclient (~20 GB), libc++ ABI, no Mojo, drift |
| Rust + webrtc-rs | sender BWE/pacer unverified; needs a C++ bridge for oneVPL | rejected |
| Rust + str0m | sans-IO, BWE/pacer, little production use | partially evaluated, not pursued ([str0m-partial](research/m6/str0m-partial.md)); a full Rust `re` was also tried and rolled back (D17) |
| Endpoint inside the browser process | direct SharedImage | rejected: largest patch, encoder/network in the sandboxed process |

Upstream structural reference (not reused code): `remoting/` (native host, Chromium socket factories, MF H.264, `VideoPlayoutDelay::Minimal()`).

## libwebrtc integration (`re`)
- `PeerConnectionFactory` with a custom `VideoEncoderFactory` (H.264 constrained baseline, `packetization-mode=1`), dummy audio device.
- Custom `VideoTrackSource` pushing `webrtc::VideoFrame` with a native buffer (the converted NV12 surface plus metadata);
  `content_type = SCREENSHARE`, `SetPlayoutDelay({0,0})`; no resolution/fps degradation (`MAINTAIN_RESOLUTION`); bitrate from GCC via `SetRates`.
- Offer `profile-level-id=42e01f;packetization-mode=1;level-asymmetry-allowed=1`; the answer's level is nominal, the SPS carries the real
  level (1080p30 -> 4.0, 1080p60 -> 4.2, [g-h264-level](research/g-h264-level.md)).
- Queue discipline: at most one pending raw frame (a busy encoder overwrites it; encoded reference frames are never dropped); `Done()` does
  not wait for encoding, so network congestion never back-pressures the Viz pool. `VideoStreamEncoder`'s own thread hop and frame dropper
  were measured in the transport work.

## GPU hand-off steps
1. First sight of a buffer: `OpenSharedResource1`, cached; the cache is cleared when the capturer pool is rebuilt.
2. Wait on the fence (or keyed mutex), then `VideoProcessorBlt` BGRA->NV12 (BT.709 limited, matching the SPS VUI) into a reusable NV12
   texture pool (`D3D11_BIND_RENDER_TARGET`); 1080->1088 alignment through source/dest rects; VP and views cached.
3. Release and `Done()`; no CPU busy wait. `SyncOperation` blocks on the encode thread, never under the `ID3D11Multithread` lock.
4. Variant: capturer-direct NV12 (skips VPP, holds pool buffers longer); RGB4 direct encoder input rejected (D15).

## Encoder (tuned defaults)
Backends: [re](re.md#encoders). oneVPL on UHD 630 / Gen9.5 uses the legacy Media SDK runtime (API 1.x), so the dispatcher filters on
hardware, AVC, `MFX_ACCEL_MODE_VIA_D3D11` and never requires API 2.x; the D3D11 device needs video support, multithread protection and the
browser GPU's LUID. Defaults (D14): VBR + LowDelayBRC, LowPower (VDEnc) on, TU7, AsyncDepth 1, `GopRefDist=1`, `NumRefFrame=1`, no periodic
IDR (`GopPicSize=65535`), constrained baseline, level from resolution/fps, 1 slice, frame cap 1.0x average at the maximum rate, VBV 1 frame
requested (the driver rounds to ~34 KB; the minimum is about 2 frames, 101 KB at 30 fps / 51 KB at 60), readback tolerance so driver
rounding no longer triggers fallback. IDR on PLI/FIR/new viewer/resize/window switch (storms coalesced); rate change by
`MFXVideoENCODE_Reset` without IDR (rebuild + IDR if the driver ignores it). Capability decisions use the `GetVideoParam` readback, not `Init >= 0`.
S7 numbers (driver 30.0.100.9805, API 1.35): 42 parameter sets initialised; LowPower 1080p60 CBR 12 Mbps encode P50 5.9 / P99 11.7 ms
(VME ~2x). Evidence: [s7-onevpl-probe](research/s7-onevpl-probe.md), [t1-encoder-tuning](research/t1-encoder-tuning.md), [vpl-harden](research/m3/vpl-harden.md),
[e2-vpl-zero-copy](research/e2-vpl-zero-copy.md).

Scenario overrides (switches, not defaults): video-heavy content on a link >= 14 Mbps may use `--max-kbps=10000-12000` (queue-loss cost,
measure first); lossy Wi-Fi may try `--vpl-intra-refresh=30`; `--fps=30` only for very low bandwidth.

## Receiver latency control
Chrome 89 treats `playoutDelayHint` as a **floor** and setting 0 does not reduce its own jitter estimate (loopback 35-42 ms vs 7-9 ms on 156),
so the endpoint must send RTP playout-delay (0,0); JS sets 0 or nothing (a non-zero hint cancels the immediate render). With min=max=0 the
remaining wait is frame completeness, decoder readiness, NACK/RTX and reference waits; jitter buffer delay then tracks packet spread
(pacer factor, shaper serialisation), not network jitter ([jitter](research/m3/jitter.md), [s8-chrome89](research/s8-chrome89.md)).

## Pacing
Browser root uses the ordinary clock (DComp root: `ExternalBeginFrameSourceWin`/`VSyncThreadWin`), child sinks inherit it; patch 17 supplies a
precise 60 Hz fake vsync because the cloaked window has no DXGI output. Capturer min period caps fps; `re` encodes only the latest frame
(cost: average half-vsync phase wait plus an occasional queued frame). External/demand BeginFrame was rejected (D15).

The precise fake-vsync clock is now enabled by default. The supported endpoint ceiling defaults to 60 fps (the current VPL contract);
`--fps` is an optional administrator ceiling. The viewer advertises its measured animation-clock rate, receives `maxFps`, and requests
the intersection via `video.rate`. Older viewers and headless test receivers use a 60 Hz capability fallback. Hidden tabs do not contribute
throttled samples. Refresh estimates require two stable windows. Capturer and WebRTC limits change together; hardware frame-rate changes
reinitialize the encoder with fresh SPS/PPS and an IDR, independently of GCC's ordinary bitrate resets.

GCC/TWCC continues to own bitrate. Once per second the viewer considers decode/encode cost, receiver drops, interval packet loss and
jitter-buffer delay, plus raw-source versus encoded throughput (to detect sender drops under CPU/GCC limits). Three consecutive bad
samples reduce the frame-rate budget; decreases have a five-second cooldown. Ten healthy
active samples allow an upward probe, with a ten-second cooldown. Static or low-fps content alone never reduces the budget, and missing
metrics do not count as overload. Reasons and accepted/display targets appear in the stats overlay and measurement report. Thresholds
are conservative heuristics, not a guarantee of available bandwidth or decoder throughput.
The default bitrate floor is 300 kbps so GCC can operate below the previous 2 Mbps floor. Resolution is preserved, so constrained links
can lose visual quality. An explicit `--min-kbps` still takes precedence. VPL startup readback allows the measured two-frame VBV minimum
in frame bytes, including low frame rates, instead of using only a fixed 64 KB allowance.
When a successful VPL reset explicitly warns that it adjusted the target bitrate, bounded target readback is accepted while all other
parameter checks remain strict. A discovered runtime bitrate floor is remembered for that encoder configuration, avoiding a reset storm
when GCC requests smaller rates. Actual encoded throughput remains the feedback signal; a requested rate is not proof the driver applied it.

## Measurement
- **Acceptance (M4)**: input->decoded and capture->decoded, ending at the viewer's decoded frame (rVFC `presentationTime`: receive + jitter
  buffer + decode), excluding presentation; P80 < 100 ms, P99 < 200 ms under d25j5. `expectedDisplayTime` is reported as a "glass estimate".
- One clock: end-to-end metrics are computed in the viewer `performance.now()` domain; segment stamps (browser monotonic, endpoint QPC, viewer)
  only locate delay, cross-host mapping uses the `probe` channel (min-RTT sample).
- Metrics: input-to-photon (viewer input t0 -> test page flips an 8x8 black/white marker code -> first rVFC frame with the new value);
  capture-to-decoded (page frame number in the marker, mapped by the clock offset); segments (capture begin/end, endpoint receive, VPP
  submit, encoded, first/last RTP, `getStats` jitter buffer/decode/drops, rVFC); quality (bitrate, QP, frame sizes, IDRs, loss/NACK, fps,
  freezes > 200 ms).
- Method: >= 2000 frames or >= 2 min per run, report P50/P80/P95/P99/max, a fresh instance per run (BWE state), differences below ~20 ms need
  repetition. Tools: [tools](tools.md).

### Latency budget and measured numbers
| Segment | Typical (design estimate) | P99 budget |
|---|---:|---:|
| Produce -> capture delivered (vsync phase, composite, blit, GPU) | 10 ms (now ~1.3 ms viz capture segment, [stage-table](research/m3/stage-table.md)) | 25 ms |
| Hand-off + BGRA->NV12 | 1 ms | 4 ms |
| H.264 encode (1080p, UHD 630 VDEnc) | 6 ms (S7: P50 5.9) | 15 ms |
| Pacer + serialisation (8-12 Mbps) | 10 ms | 40 ms |
| Network one-way + Wi-Fi jitter | 25 ms | 60 ms |
| Receive assembly / jitter buffer (playout-delay 0) | 3 ms | 20 ms |
| Decode (+ display, excluded) | 15 ms | 30 ms |

Measured (60 s runs, ms P50/P80/P99, capture->decoded unless noted; [experiments](research/m4/experiments.md), [chrome89](research/m4/chrome89.md),
[s5-pacing](research/m4/s5-pacing.md)): loopback marker 17-35 / 20-37 / 34-40; genuine Chrome 89 under d25j5 scenario S 61-74 / 73-82 / 101-119
(V and G similar); input->decoded under d25j5 S 134-146 P50 / 146-160 P80 (fails the 100 ms target because it carries the uplink and the
input frame wait; loopback input->decoded 37-60 P50). Current-binary spike baseline: S d25j5-c12 capture 85.4/90.1/98.6 and 69.5/74.0/81.4.

## Experiment matrix (E1-E14) and outcomes
Scenarios: S-scroll, S-video (V), S-game (G). Baseline B0: 1080p30, ARGB + VPP, CBR 12 Mbps, VBV 1 frame, TU7, LowPower, IDR on demand,
playout-delay (0,0), GCC on, RTT 50 +-10 ms, Wi-Fi receiver.
| E | Variable | Outcome |
|---|---|---|
| E1 | fps 30/60 | 60 |
| E2 | RC CBR / VBR+LDB+MaxFrameSize | VBR+LDB |
| E3 | VBV 1/3 frames/100 ms | 1 frame requested |
| E4 | TU / LowPower | TU7, LP on |
| E5 | bitrate 8/10/12/14 | 8 Mbps ceiling |
| E6 | IDR-only vs intra refresh | no latency gain, opt-in |
| E7 | hand-off zero-copy vs conservative | fence zero-copy |
| E8 | ARGB+VPP vs NV12 direct | ARGB+VPP kept |
| E9 | pacing P0 / P1 | P0 (P1 rejected) |
| E10 | playout-delay (0,0)/(0,20)/none | (0,0) |
| E11 | network RTT/jitter/loss | profiles in [tools](tools.md#netem) |
| E12 | adaptation | fixed 1080p |
| E13 | FEC | off |
| E14 | profile CB / High | CB (High gated) |

## Risks (remaining)
Render/encode GPU contention on the iGPU (fixed-function VDEnc mitigates); legacy MSDK runtime limits on `LowDelayBRC/MaxFrameSize/IntRefType`
combinations (read back, A/B both RC groups); playout-delay 0 stutter on jittery Wi-Fi (E10); NVENC unverified on hardware (D20).
