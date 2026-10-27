# m3-vpl-integrate
Completed integration and four 60-second runs on 2026-10-26. Scope: endpoint and tools/latency only; no commits or Chromium edits.
## Work
- Read reviewed draft and integration order; resumed the already applied/merged implementation and object compile fixes. Unrelated design/prompts/ left alone.
- Retained current latency instrumentation, TURN/netem and resize/window behavior. No upstream Chromium hooks were added.
## Files changed
- Continued fixes: endpoint/d3d_native_frame.cc, endpoint/vpl_encoder.cc, endpoint/third_party/libvpl/{BUILD.gn,README.chromium}; vendored66 upstream payload files plus libvpl-manifest.json.
- tools/latency/pipeline.mjs: full encoder name, actual backend inference, runtime/settings/readback and branch-aware path metadata.
- Inherited applied draft already supplies endpoint/{vpl_encoder,d3d_native_frame}.{cc,h} and integration in BUILD.gn/main.cc/peer.{cc,h}/d3d_frame_source.{cc,h}; did not reapply or overwrite merged files.
## Evidence and API citations
[V] Draft API citations refer to pinned headers; calls introduced/fixed here will be verified directly against pin.
## Measurements
Complete: two60s loopback runs per encoder, four JSON/CSV pairs plus archived chain logs under vpl-1/, vpl-2/, openh264-1/, openh264-2/. Full tables/configuration below and in MEASUREMENTS.md.
## Risks
Keyed mutex hold time; exact capture/encoder device and adapter LUID matching; legacy runtime rate reset behavior.

## Compile fix (object-only)
- endpoint/third_party/libvpl/BUILD.gn: added to dispatcher cflags_cc -Wno-deprecated-declarations, -Wno-unused-function, -Wno-unused-const-variable, -Wno-sign-compare, -Wno-microsoft-cast, -Wno-extra-semi, -Wno-missing-braces, -Wno-reorder-ctor, -Wno-implicit-fallthrough, -Wno-pointer-sign, -Wno-writable-strings (dispatcher only; vendored sources untouched).
- endpoint/d3d_native_frame.cc, vpl_encoder.cc: include third_party/webrtc_overrides/rtc_base/logging.h instead of third_party/webrtc/rtc_base/logging.h (Chromium overrides RTC_LOG; double definition errors).
- Result: libvpl:dispatcher and all 10 prts_endpoint .obj files build cleanly (no exe/chrome linked, nothing launched).

## Continued integration (2026-10-26)
- Resumed after object-only compile fixes. Will link via be.sh and run smoke plus four 60-second measurements; no Chromium edits or commits.
- Existing NOTES and reviewed draft read; working tree contains three unstaged compile fixes and untracked vendored payload.

### Link and instrumentation
- be.sh incremental endpoint link passed (build-link.log). All 66 vendored payload files rechecked against reviewed SHA256 manifest (vendor-sha256.log).
- Retained source branch-specific timing (native_vpp vs import_copy_submit/staging_map/argb_i420), LowDelayOutput encode timing, TURN and resize behavior in applied draft.
- Initial smoke selected hardware VPL and native frames, but used a wrong fixture URL; its empty marker report is invalid and excluded.
- Changed VPL/native INFO diagnostics to base LOG(INFO), because RTC INFO is filtered by the embedder. Added IDR dimensions/SPS/PPS evidence. Report parser now preserves full implementation name and records actual backend/runtime/GetVideoParam settings and native path.

### Smoke evidence
- smoke-vpl-results.json: decoded dimensions 1920x1080 ->1280x720 ->1920x1080 and frame counter progressed through explicit ctl keyframe request. smoke-vpl-chain.log has SPS/PPS+IDR at each size; keyframe sent ~15:48:14.9, independent IDR at15:48:14.926 before next rate rebuild.
- Viewed smoke-vpl-1920x1080-2.png: complete browser UI, marker, text, colored fixture without obvious corruption. Remaining screenshots saved in task dir.
- Runtime [V]: mfxhw64 API1.35, DriverStore libmfxhw64.dll; GetVideoParam status0, rate_control2=VBR, LP16=ON, AsyncDepth1, level40, profile578=constrained baseline (API constants in vendored mfxstructures.h).
- Native VPP sampled mutex_hold_us=15993, acquire_us=44; conservative source completion can dominate latency. Rate-control reinit creates temporary I420 bridge frames; quantify in formal runs.
- First task-local CDP smoke missed Page.enable before injection; rerun succeeded. No production workaround introduced.

### Measurement progress
- Completed VPL 60s runs1/2 with existing test.sh/run.mjs; decoded size is1890x938 (headless Edge default1920x1080 outer window), not nominal harness1920x1080. Both encoders use identical settings; full1080p covered by smoke.
- Sender stats are absent (report.sender=null), so corrected pipeline backend inference to use init/active/fallback logs when sender backend is unavailable. Refreshed ONLY pipeline.encoder/capture in VPL reports from their archived chain logs; raw metrics/CSV unchanged and original JSON preserved as .json.original. Runtime/settings now present in both.
- VPL capture P50/P80/P95/P99: run1 66.29/75.38/136.98/157.68ms; run2 66.19/75.93/133.11/160.44ms. Input->decoded flip:77.5/92.8/160/178.8 and76.7/104.5/153.4/178.9ms. MEASUREMENTS.md generated incrementally.
- Viewed OpenH2641280x720 screenshot: complete content, clean text/marker, no obvious corruption. OpenH264 selection bypassed VPL probing and reinitialized both resolutions successfully.

### Final build and API checks
- build-final.log: incremental be.sh link passed after diagnostic-only adapter name logging and Chromium formatting (pinned buildtools/win-format/clang-format.exe, gn format; only endpoint files formatted). No release/full build or suites.
- [V] base/strings/utf_string_conversions.h:27 WideToUTF8(std::wstring_view); Windows SDK shared/dxgi.h:207-215 DXGI_ADAPTER_DESC Description/VendorId/DeviceId/AdapterLuid read before adding adapter identity log. All other introduced APIs are the pinned reviewed-draft calls (m2/vpl/NOTES.md API table).
- [V] out/dev/obj/prts_endpoint/third_party/libvpl/dispatcher.ninja cflags_cc has libc++ include and /EHsc; endpoint target has no /EHsc. Exception relaxation and vendor warning suppressions remain dispatcher-only.
- Added exact reviewed libvpl-manifest.json to vendor directory for reproducible payload verification, updated README.chromium; proprietary driver DLLs/prebuilt libs absent.
- Formal four runs complete. Both VPL runs show120 rate rebuilds across the whole chain/warmup, native/I420 delivered1667/235 and1706/229, zero native ToI420 slow reads. Maximum sampled source-key hold16.581/16.405ms (14 samples each; not full maximum).
- Measured binary differs from final only by adapter-name diagnostic/include and whitespace; final binary being smoke revalidated. No encoder settings or behavior changed after measurements.
# Measurements

Loopback; headless Edge; dev/component/dcheck endpoint; marker fixture in tab mode. rVFC compositor display estimate, not physical scanout. E2E values ms P50/P80/P95/P99 over the full 60-second measurement.

| Run | Decoded size | Encoder | Capture->display | Input->decoded flip | Samples capture/input | FPS |
|---|---|---|---|---|---|---|
| vpl-1 | 1890x938 | PRTS oneVPL D3D11 AVC | 66.29/75.38/136.98/157.68 | 77.50/92.80/160.00/178.80 | 1622/119 | 26.99 |
| vpl-2 | 1890x938 | PRTS oneVPL D3D11 AVC | 66.19/75.93/133.11/160.44 | 76.70/104.50/153.40/178.90 | 1640/119 | 30.03 |
| openh264-1 | 1890x938 | OpenH264 | 64.23/69.74/73.32/79.50 | 68.40/80.80/86.40/98.60 | 1778/120 | 29.97 |
| openh264-2 | 1890x938 | OpenH264 | 58.44/63.44/66.84/71.01 | 67.40/83.70/92.70/100.00 | 1785/120 | 29.88 |

Stage values ms P50/P95 (n), latest rolling five-second endpoint window recorded in report; not merged whole-run percentiles. CPU bridge samples on VPL are transition/reinit frames. native_vpp includes import-side VP conversion and conservative GPU completion under source mutex.

| Run | Copy submit | Staging/map | CPU convert | Native VPP | Encode callback |
|---|---|---|---|---|---|
| vpl-1 | 0.20/0.33 (16) | 4.18/4.61 (16) | 1.05/1.19 (16) | 9.71/16.58 (135) | 5.43/9.17 (142) |
| vpl-2 | 0.20/0.90 (20) | 3.99/4.60 (20) | 1.05/1.33 (20) | 8.65/16.30 (130) | 8.09/9.21 (136) |
| openh264-1 | 0.16/0.22 (150) | 4.12/4.72 (150) | 0.78/1.18 (150) | - | 4.38/6.38 (150) |
| openh264-2 | 0.17/0.21 (149) | 4.11/4.63 (149) | 0.78/1.12 (149) | - | 4.43/6.11 (149) |

Initial codec allocation is start/min/max kbps. Readback is the final effective VPL configuration; GCC changes rates during a run. These backends inherit different WebRTC bitrate limits.

| Run | Initial start/min/max kbps | Effective encoder configuration |
|---|---|---|
| vpl-1 | 1885/30/3608 | 1601/1601 kbps; fps=30; VBR=2; LP=16; AsyncDepth=1; level=40; profile=578 |
| vpl-2 | 1885/30/3608 | 1617/1617 kbps; fps=30; VBR=2; LP=16; AsyncDepth=1; level=40; profile=578 |
| openh264-1 | 2000/2000/12000 | OpenH264 camera; max 4 encoder threads |
| openh264-2 | 2000/2000/12000 | OpenH264 camera; max 4 encoder threads |

Whole archived chain counters include startup/warmup (not just the 60-second window). Mutex maxima are sampled every 120 native frames, not a bound or full distribution.

| Run | Rate rebuilds | IDRs | Max sampled mutex hold us | Hold samples | Native/I420 delivered | Slow ToI420 reads |
|---|---|---|---|---|---|---|
| vpl-1 | 120 | 116 | 16581 | 14 | 1667/235 | 0 |
| vpl-2 | 120 | 113 | 16405 | 14 | 1706/229 | 0 |
| openh264-1 | 0 | 0 | - | 0 | 0/1902 | 0 |
| openh264-2 | 0 | 0 | - | 0 | 0/1904 | 0 |


### Final validation and risks
- Final default-selection smoke (smoke-default-final-results.json/log/chain.log) runs without --encoder, selects VPL and logs Intel(R) UHD Graphics630 vendor32902 device39877 LUID0:59126 and mfxhw64/libmfxhw64.dll API1.35. Viewer viewport and decoded dimensions actually change1920x1080 ->1280x720 ->1920x1080; decoded keyframes14->16 after explicit ctl keyframe. SPS/PPS/IDR confirmed for both dimensions.
- Final OpenH264 smoke (smoke-openh264-final-*) has actual viewer viewport resizing, successful matching decoded dimensions and explicit keyframe decoded counter5->6. Native frame counters are0, with the I420 source path active in explicit software mode; no VPL probe. Screenshots from both backends saved, representative images visually reviewed without corruption.
- validation.json: all four reports are60s with>1500 latency samples and119/120 input samples, all marker decode failures0 and input timeouts0. Sender telemetry is null on this real endpoint, so report pipeline is explicitly inferred from active/init/fallback logs. Raw measurement/CSV data preserved.
- [I] Native VPP's conservative EVENT polling/completion is a major cost (P50~9ms/P95~16ms); rate changes cause~120 rebuilds/chain and temporary CPU bridge frames, explaining likely tail inflation. No claim of a speedup: this marker workload has worse VPL tails. Different WebRTC initial bitrate limits are documented in configuration table, so this is the actual default-path comparison, not fixed-equal-bitrate encoder microbenchmarking.
- Adapter matching: runtime extended DeviceLUID matches the chosen endpoint Intel device, and native source/encoder exact D3D11 device identity is enforced. Browser still reports requested capture LUID0 on this host; successful import plus exact shared device is observed, not proof of multi-GPU correctness. Multi-adapter/default adapter changes, device loss, runtime removal/failure injection and memory/teardown stress remain unverified.
- Keyed mutex: maximum sampled holds16.581/16.405ms; sampling every120 native frames cannot establish worst-case/distribution. The conservative20ms completion deadline and1ms sleeps can add latency or disable native path after failures.
- Release builds,60fps, Chrome89 receiver/conformance, colorimetry audit, long/high-motion bitrate quality and driver-internal copies remain unverified. No full builds/test suites were run; no commits or Chromium checkout edits were made.
- Final compile-only follow-up reorders base/logging.h after standard includes; no behavior change. Incremental build log build-final-include-order.log.

