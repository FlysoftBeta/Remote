# S8: Chrome 89 as the receiver (viewer against the mock endpoint)

Date: 2026-10-25, parallel-track orchestrator. Harness: `tools/latency/run.mjs` (control-plane + mock endpoint in headless Edge 156 +
viewer in the browser under test, everything on one machine over loopback, so the network part is ~1 ms RTT).
[V] = measured here, [I] = inference.

## 1. What could be obtained
| Build | Source | H.264 in WebRTC | Notes |
|---|---|---|---|
| Chromium **89.0.4389.0** (snapshot r843807, the M89 branch point is r843830) | `commondatastorage.googleapis.com/chromium-browser-snapshots/Win_x64/843807/chrome-win.zip`, unpacked to `C:\src\tmp\chrome89\chrome-win\` | **no** [V] (`RTCRtpReceiver.getCapabilities('video')` has no H.264: open-source builds lack `proprietary_codecs`/`rtc_use_h264`) | Real M89 Blink/libwebrtc (`e7d9f74b` lineage), so the receiver APIs and jitter buffer are M89's |
| Google Chrome 89 (branded, with H.264) | Google update server: `targetversionprefix="89."` (with and without `rollback_allowed`) → `noupdate` [V] | — | Google no longer serves M89; no other official Google source found |

So S8's "1080p H.264 on Chrome 89" part is **not closed**: the M89 receiver was tested with VP8 (the mock keeps H.264 first and other codecs as a
fallback). The latency-hint behavior is codec-independent (same `VideoReceiveStream`/`VCMTiming`/Blink path) [I]; H.264 1080p decode on 89
must be checked on a real Chrome 89 install (e.g. a client machine that still has it, or a Chrome 89 offline installer kept by IT).

Win11 24H2 quirk [V]: Chromium 89 crashes its network service (`EXCEPTION_ACCESS_VIOLATION` in process exit) unless started with
`--no-sandbox --enable-features=NetworkServiceInProcess`; old `--headless` also needs `--remote-debugging-port` to stay alive. `run.mjs --old-headless`
adds these flags.

## 2. Receiver API support on M89 [V]
| Feature | M89 | Edge/Chrome 156 | Viewer handling |
|---|---|---|---|
| `RTCRtpReceiver.playoutDelayHint` (s) | present, read-back works, **honored when > 0** | present | set 0 when `jitterBufferTarget` is absent |
| `RTCRtpReceiver.jitterBufferTarget` (ms) | absent | present, honored | preferred |
| `RTCRtpTransceiver.setCodecPreferences` | present (`ok`) | present | H.264 pm=1 / 42e01f first |
| negotiated DataChannels (`negotiated:true,id`) | works (input/ctl/probe all open) | works | — |
| `requestVideoFrameCallback` + `expectedDisplayTime` | present | present | used for the marker measurement |
| `inbound-rtp.jitterBufferDelay/EmittedCount` | **absent**; found on the legacy `track` stats object (`remoteSource:true`) | on `inbound-rtp` | viewer falls back to `track` |
| `inbound-rtp.totalDecodeTime`, `decoderImplementation` | present (`libvpx`) | present (impl hidden) | — |
| Pointer Events, `touch-action:none`, `visualViewport`, `beforeinput` | present | present | — |

## 3. Latency-hint effect (1080p30, loopback; mock sender = headless Edge, canvas capture + software encode) [V]
| Receiver | Hint | capture→display P50 / P80 / P99 ms | input→photon P50 / P99 ms | jitter buffer ms/frame |
|---|---|---|---|---|
| Chromium 89 (VP8) | not set | 94.0 / 110.2 / 127.3 | 122.6 / 170.7 | 35.0 |
| Chromium 89 (VP8) | `playoutDelayHint=0` | 105.4 / 106.3 / 123.6 (other run: 95.8 / 96.9 / 114.3) | 121.5 / 173.3 | 41.8 |
| Chromium 89 (VP8) | `playoutDelayHint=0.1` | 165.2 / 166.7 / 183.9 | 179.0 / 224.6 | 100.0 |
| Edge 156 (H.264) | not set | 102.8 / 112.3 / 123.0 | 126.6 / 172.1 | 8.8 |
| Edge 156 (H.264) | `jitterBufferTarget=0` | 97.5 / 107.3 / 123.1 | — | 7.2 |
| Edge 156 (H.264) | `jitterBufferTarget=100` | 168.4 / 178.4 / 195.4 | — | 75.2 |

Findings:
1. M89 honors `playoutDelayHint` as a **minimum** (0.1 s → jitter buffer exactly 100 ms, +60-70 ms end to end) [V]. Setting it to **0 does not
   lower** the buffer below M89's own estimate (35-42 ms here, versus 7-9 ms on 156 under the same conditions) [V]. Consistent with the M89
   source reading (e-video): `max(frame, base, sync minimum)`, and the "render immediately" branch only triggers with the RTP
   `playout-delay` extension at (0,0) [I]. **The endpoint must send the playout-delay RTP extension (0,0); the JS hint alone cannot reach low
   latency on 89.** The mock (a Chrome sender) does not write that extension, so this M89 path is still unmeasured; it is a test for the
   real libwebrtc endpoint (03 §3.2 `SetPlayoutDelay({0,0})`, E10).
2. Never set a non-zero hint on 89 (it adds directly to latency).
3. The ~95 ms baseline is dominated by the mock sender (canvas capture → software encode, ~21 ms encode at 1080p, plus headless compositor
   pacing) and is the same order for both receivers; it is not representative of the real endpoint. Use the harness for relative comparisons until the
   endpoint exists.

## 4. Reproduce
```
node tools/latency/run.mjs --secs 20 --port 8097 --delay 0 --viewer-browser C:/src/tmp/chrome89/chrome-win/chrome.exe --old-headless
node tools/latency/run.mjs --secs 20 --port 8099 --delay off            # Edge 156 viewer
```
`--delay off` leaves the hint unset; per-frame CSV and JSON land in `C:\src\tmp\ptrack\latency-*.{csv,json}`.

## 5. Open
- H.264 1080p decode on branded Chrome 89 (hardware vs software decoder, `decoderImplementation`) — needs a real Chrome 89 install.
- M89 with the sender playout-delay extension (0,0) — needs the libwebrtc endpoint (or a native test sender).

## 6. Harness calibration (added) [V]
- `/mock?loop=1` shows the mock's own captured canvas stream in a local `<video>` (no encoder, no network) and decodes the marker:
  headless Edge draw → captureStream → local display = **P50 80 ms, P99 121 ms**. Most of the ~95-105 ms end-to-end baseline is therefore
  canvas capture plus headless display pacing, not WebRTC.
- The viewer now also records `requestVideoFrameCallback` `receiveTime` (Edge 156, H.264, hint 0): draw → frame received **P50 38.5 / P80 51.8 /
  P99 72.3 ms** (includes ~21 ms software encode); received → displayed ≈ 62 ms in headless (decode 3.4 ms, jitter buffer 7.7 ms, the rest is render/display).
- Consequence: mock-based numbers are upper bounds; compare configurations, and re-measure with a headed viewer on the real client device.
