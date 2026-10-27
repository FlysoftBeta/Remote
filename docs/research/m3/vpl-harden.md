# m3-vpl-harden

Started: 2026-10-26. Scope: review findings 1-5 and cheap finding-6 fixes; incremental build; encoder smoke, two GPU restarts, 60-second latency run.

## Changes
- Completed endpoint-only hardening in vpl_encoder.{cc,h}, d3d_native_frame.{cc,h}, d3d_frame_source.cc. No commits or upstream Chromium changes.

## Evidence
- Final incremental build, both encoder smoke tests, two final GPU restarts and 60s loopback passed; details below.

## API citations
- Local pinned headers and SDK declarations inspected; citations below.

## Risks / unverified
- Hybrid-GPU host unavailable; no intentional VPL busy/sync/readback error injection. GPU-process restart is covered; whole-adapter removal is not.
- Existing runtime rejection/unrequested-IDR hold-max policy remains.
- Viewer display/gap timings use headless rVFC compositor estimates.

## Implementation progress
- Findings 1/12: rebuild native pool on Bind/Reset, retry unhealthy VPP after 2s, discard abandoned shared buffers without poisoning pool; CPU path also discards non-timeout mutex failures.
- Finding 2: keep pending target, retry from Encode after cooldown, bypass cooldown for >=30% decreases, initialize BRC resources at negotiated max, retain startup target; runtime adjustment explained below.
- Finding 3: shared device clear is now atomic compare-and-clear under state mutex.
- Finding 4: drop transient input/busy/empty-output errors and request IDR after possible reference loss; fall back after 8 consecutive errors or explicit device/session loss. In-flight sync/bitstream remains owned until completion.
- Findings 5/6: unsuitable capture adapters use own Intel device; I420 mismatch cannot restart; stale native texture uses ToI420. Only a current native device can trigger adoption.
- Cheap findings 7/9/11: cached per-buffer staging, once-only slow-path log, explicit readback failure -> capture CPU bridge, maxBitrate==0 guard, timing eviction by encode age; clear timing on dropped frames.

## API citations (read locally)
- [V] third_party/webrtc/api/video/video_frame_buffer.h:81-87 explicitly permits ToI420 nullptr on readback failure; H264 encoder h264_encoder_impl.cc:453-459 checks it. Native CropAndScale and VPL Prepare also check null.
- [V] third_party/webrtc/api/video_codecs/video_encoder.h:78-81 OnFrameDropped(timestamp, spatial, end).
- [V] endpoint/third_party/libvpl/api/vpl/mfxvideo.h:183-199 SyncOperation wait and IN_EXECUTION; mfxdefs.h:288-304 device-loss, failed, hang, busy constants.

## Runtime evidence (initial build)
- Incremental `bash /c/src/tmp/m1/be.sh`: "The build has finished successfully."
- VPL smoke: resize 1920x1080 -> 1280x720 -> 1920x1080, increasing delivered frames; ctl keyframe raised receiver keyFramesDecoded 5 -> 6 (115 -> 151 video frames).
- GPU kill #1: PID 19140 at 09:47:44.778Z, max rVFC interval 531.8ms, +274 frames in recovery window. Capture Reset 17:47:44.916; native VPP frames=120 at 17:47:49.250, native=30/i420=0 afterwards.
- GPU kill #2: PID 2888 at 09:47:56.298Z, max rVFC interval 504.5ms, +271 frames. Capture Reset 17:47:56.427; native VPP frames=120 at 17:48:00.756.
- Both GPU restarts: no `fallback encoder=openh264`, encoder stats remain PRTS oneVPL D3D11 AVC. Results: smoke-hardened-results.json; full log smoke-hardened-chain.log; screenshots smoke-hardened-*.png.
- [V] Windows SDK 10.0.28000.0 um/d3d11.h:14437 GetCreationFlags; shared/dxgi.h:792-798 AcquireSync/ReleaseSync; :1335 IDXGIAdapter::GetDesc. All are already used elsewhere in this path.

## Runtime-driven adjustment
- Initial 60s marker run: capture/display P50/P95 60.8/71.4ms, input/flip 65.2/82.7ms, ~30fps. However, initializing at startBitrate=2000 caused UHD630 legacy runtime to reject subsequent increases (Reset-no-IDR=-15, plain=-14; readback stayed 2000). This optional startup change is withdrawn to preserve the m3/vpl-fix behavior and high-motion rate headroom. Final code initializes at the negotiated maximum, retains startup/GCC pending target and applies it after cooldown. See final run below for authoritative results.
- Final cleanup: preserve d3d_frame_source.cc original CRLF; remaining files retain LF. `git -c core.whitespace=cr-at-eol diff --check` passes.
- Completion-query telemetry now runs only for same-device native submissions; stale-native I420 uploads cannot query an old device through the current context.

## Final build / smoke evidence
- build-final.log: "The build has finished successfully." Incremental endpoint only.
- VPL final smoke: 1920x1080 -> 1280x720 -> 1920x1080; keyframe count increased, video continued. smoke-final-results.json and smoke-final-chain.log.
- Final GPU kill #1 PID 8128 at 09:57:18.924Z: +271 frames, max rVFC gap 536.3ms.
- Final GPU kill #2 PID 7328 at 09:57:30.490Z: +272 frames, max rVFC gap 520.7ms. Native VPP delivery resumed after each; no fallback.
- Final OpenH264 smoke: resize both directions, keyFramesDecoded 5 -> 6, frames 119 -> 156 after ctl keyframe. smoke-final-openh264-results.json / chain.log.
- Final rate smoke: Reset-no-IDR status=0 at target=2000 then 11288, VBV_KB=101/initial_KB=50 and cap_bytes=50000 unchanged, hold_max=0; no repeated rejected-reset loop.

## Final 60-second latency and baseline comparison
- Command: node tools/latency/run.mjs --no-mock --port 8300 --secs 60 --endpoint-log C:/src/tmp/m3/vpl-harden/vpl-marker-final-chain.log --remote-cdp 9333. Task-local measure.mjs launched cp.sh with --encoder=vpl and marker remote-page URL.
- Decoded 1785/1785 marker frames, zero marker failures, receiver framesDropped=0; 120/120 input attempts, no timeouts. Capture/display P50/P80/P95/P99 = 63.60/69.17/72.67/75.94ms. Input/flip = 68.5/78.5/87.3/94.7ms.
- Compared with research/m3/vpl-fix.md marker-1 capture/display 63.64/69.10/72.64/76.30ms: essentially unchanged (P95 +0.03ms). Marker-2 was faster at 60.29/64.97/69.68/74.29ms. Input P95 87.3ms is between prior 85.0 and 91.4ms; no material latency regression observed in this single final run.
- Summary last-snapshot fps=28.99; rolling viewer 1s samples are ~30, endpoint source ~29.95-30. Final measurement sample count corresponds to ~29.75 marker samples/s; no receiver frame drops. Do not interpret the last 1s snapshot as the whole-run FPS.
- Rate recovery: initial max=12000, successful immediate decrease to 2000, deferred successful increase to 11471 after 2s. Reset/readback status=0, VBV=101KB, initial=50KB, cap=50000 unchanged; hold_max=0, total 2 resets and one initial IDR across run. No reset rejection or OpenH264 fallback.
- Artifacts: validation-final.json (checks/metrics/baseline), vpl-marker-final-run.log, vpl-marker-final-chain.log; vpl-marker-final/latency-2026-10-26T10-00-28-004Z.{json,csv}.
- Final GPU native delivery evidence: capture reset 17:57:19.074 -> native VPP frames=120 17:57:23.393; reset 17:57:30.612 -> native VPP frames=120 17:57:34.934. All final GPU intervals below 1.5s.

## Final scope and remaining risks
- Five source files changed, all under endpoint/. Task evidence/scripts/screenshots only under C:/src/tmp/m3/vpl-harden/. No commits, no full builds, no test suites, no C:/src.old access.
- [I] Stale native device conversion, non-Intel bridge, compare-and-clear overlap and pending-sync buffer lifetime checked statically, without dedicated fault injection. Eight consecutive transient errors trigger fallback; explicit MFX device/session loss falls back immediately.
- [I] Shared readback health token survives pool retirement safely; a failed readback disables only its originating pool and periodic retry creates a fresh pool. ToI420 nullptr is a documented WebRTC contract, and call sites checked here already guard it.
