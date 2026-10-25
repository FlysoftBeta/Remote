> **NOTE:** sections after "Initial encode contract" were never written (run killed). Gaps are filled by research/e2-vpl-zero-copy.md, research/g-h264-level.md (corrects the Level 3.1 -> 720p advice below) and design/03-video-pipeline.md.

# Low-latency WebRTC video pipeline for remotekit

Research date: 2026-10-25. Scope: Windows browser-endpoint, Intel UHD 630, H.264 WebRTC, Chrome 89+ receivers.
Primary recommendation: retain the prior Rust endpoint, explicitly add sender GCC/TWCC and bounded packet pacing, replace its interim MF encoder with a oneVPL-dispatched legacy Media SDK AVC session, and negotiate sender playout-delay `(0,0)`.

## Evidence and scope

- The project brief and prior implementation are read-only inputs.
- **Verified code** means the named source was read; **documented** means a primary specification/vendor source says it; **proposal** and **estimate** require experiments.
- No hardware measurements are performed for this report. Prior-art measurements retain their original scope.
- ChromiumDash's Windows Stable API returned **157.0.8103.15**, Chromium commit **1aa07b638799e59656559ae6c6649d4b197f555e**, libwebrtc **95554e4a0c5c343fdc989387d9d0ce18e2a2294b**. `chrome/VERSION` at that commit was read and agrees. This records the API response, rather than inferring a milestone from a release calendar. [Release API](https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Windows&num=1), [VERSION](https://chromium.googlesource.com/chromium/src/+/1aa07b638799e59656559ae6c6649d4b197f555e/chrome/VERSION).
- Historical receiver inspection: **89.0.4389.90**, whose `DEPS` pins libwebrtc **e7d9f74b256605556726c64dacd518a105ba872f**. Current code cannot establish Chrome 89 behavior; historical source below does. [M89 DEPS](https://chromium.googlesource.com/chromium/src/+/89.0.4389.90/DEPS).
- `C:/src/cr-partial` was absent at initial inspection; Gitiles was read over HTTPS, without a checkout or build.
- Local prior-art root below is `C:/Users/Flysoft/Desktop/chromium`; it was never modified. Its `PIN` is `caef9db47e2cf3fe8e9f07ddceeff6ddf6957385`, not the research baseline.
- Constraint clarification: Windows 10 has no 24H2 release; the brief's development machine is Windows 11 IoT LTSC 24H2. Proposed D3D11/DXGI interfaces are Windows 10-era APIs; an actual Windows 10 deployment still needs qualification.

## Prior implementation

**Verified code:** `prtsd/Cargo.toml` pins `webrtc = 0.21.1`, matching `rtc = 0.21.1`, `openh264 = 0.9.9`, and `windows = 0.62.2`.
This is the newer async wrapper over a sans-I/O protocol core, not the older webrtc-rs 0.14 design.
`prtsd/src/viewer.rs::Session::create` registers one H.264 codec with `42e01f`, packetization mode 1, level asymmetry, PLI/FIR and REMB feedback.
`register_default_interceptors` supplies NACK/report/TWCC-receiver machinery; merely calling it does **not** enable a sender congestion controller or a packet pacer.
The installed `rtc-0.21.1/src/peer_connection/configuration/interceptor_registry.rs` explicitly makes `configure_congestion_control` opt-in; it installs GCC-compatible estimation, a pacer, and selected feedback plumbing.
Thus the prior application handles REMB caps, but should not be described as already having full sender GCC/TWCC pacing.

`video.rs::Video::new` has one encoder thread per instance, wall-clock frame deadlines, no catch-up queue, and a `tokio::sync::watch` latest-encoded-frame slot.
`viewer.rs` uses `TrackLocalStaticSample::write_sample` and a 100 ms submission timeout; its empty-sample clock advance preserves actual capture gaps before packetizing the next access unit.
This fixes false RTP transit jitter after capture skips; retain that principle when moving to an absolute 90 kHz RTP timestamp mapping.
An encoded-ID gap requests another keyframe, but the shown code proceeds to send the new access unit; a missing referenced P-frame may therefore cause a transient decode failure until that IDR arrives.
Dropping raw captures is safe; dropping arbitrary encoded reference frames is not.
`Telemetry::handle_read` handles PLI/FIR by setting an atomic keyframe request, and REMB by updating an encoder cap.
`daemon.rs` combines configured/network/congestion ceilings with `min`, so a configured minimum cannot override a lower available rate.
No playout-delay header extension is supplied: both sample writes pass an empty extension list.
`shaping.rs` delays/drops classified SRTP/SRTCP datagrams for experiments; this is a network impairment tool, not a bitrate packet pacer.

**Verified prior docs:** `docs/prts/mvp-mf-latency.md` identifies the shipping hardware backend as Intel H.264 Media Foundation MFT, with GPU NV12 input and CPU fallback; direct oneVPL is deferred.
It requires low latency, CBR, zero B-pictures, baseline, a two-second GOP, and explicit IDRs; it reads controls back and permits one synchronous encode request at a time.
Its HRD request is `max(ceil(bitrate_bps / 80), 32768)` **bytes**: nominally 100 ms, not one frame.
At very low bitrate, literal small HRD settings passed readback but broke decoding; a 32 KiB floor fixed that regression.
This warns against treating accepted encoder parameters as evidence of acceptable output.

| Prior measurement, 2026-10-21, UHD 630 | Mean | P95 | P99 | Scope |
| --- | ---: | ---: | ---: | --- |
| CPU BGRA conversion + Intel MFT | 10.656 ms | 11.862 ms | 38.424 ms | 180 changing 1280x720 inputs |
| D3D11 BGRA conversion + Intel MFT | 9.281 ms | 10.737 ms | 11.769 ms | Same workload, 30 fps timestamps |

These calls were submitted without real-time pacing. Timing includes conversion, dispatch/polling and bitstream copy; excludes source upload, transport, decode and display.
There is no verified 1080p60 oneVPL result or glass-to-glass percentile in those measurements.
`prtsd-notes.md` explicitly calls the shaping measurements receiver jitter/decode/publication measurements, not capture-to-display latency.
`d3d.rs` already creates an Intel device with BGRA/video support, enables multithread protection, checks adapter LUID, imports shared textures and converts BGRA through D3D11 video processing.
This ownership/device code is more reusable than the MF encoder control interface.

## Receiver contract and delay controls

### Codec negotiation and hardware decode

The compatibility payload is `video/H264`, RTP clock 90000, RFC 6184 non-interleaved packetization (`packetization-mode=1`), 8-bit 4:2:0 progressive, no B-pictures.
Keep a constrained-baseline payload available even if a separately offered High-profile payload is preferred on known hardware.
The conservative common fmtp is:

```sdp
a=fmtp:102 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f
a=rtcp-fb:102 nack
a=rtcp-fb:102 nack pli
a=rtcp-fb:102 ccm fir
a=rtcp-fb:102 transport-cc
```

**Critical limit:** `42e01f` is constrained baseline **Level 3.1**; it does not authorize 1080p30/60.
1920x1080 needs 120x68=8160 macroblocks/frame: 244800 MB/s at 30 fps fits Level 4.0, while 489600 MB/s at 60 fps needs Level 4.2.
For a receiver advertising enough level, use an honest constrained-baseline Level 4.0/4.2 SPS (e.g. `42e028`/`42e02a`) and negotiate the permitted sending level.
`level-asymmetry-allowed=1` allows directional levels; it does not waive the peer's receive constraints.
If the answer only permits Level 3.1, reduce to 720p30 or another conforming format; do not rely on tolerant decoders accepting an invalid stream.
RFC 6184 `max-fs`/`max-mbps` can describe additional receiver capacity if explicitly advertised. [RFC 6184 negotiation](https://www.rfc-editor.org/rfc/rfc6184.html#section-8.2.2), [H.264 level definitions](https://www.itu.int/rec/T-REC-H.264).

High (`640cxx` constrained High where advertised, or an actually offered High profile) is a separate capability, not interchangeable with `42e0xx`.
It can improve coding efficiency through CABAC/8x8 transforms without adding B-frame reorder delay, but support must be checked in the SDP and on the physical receiver.
Do not invent a High fmtp and rewrite a baseline payload's SPS underneath it.
Windows uses Chromium's GPU/platform decoder path when available; Android uses suitable MediaCodec hardware decoders; macOS uses VideoToolbox where supported.
Driver blocklists, codec/profile/level support, Android device quirks and software-only Chromium distributions mean H.264 negotiation alone proves neither hardware decoding nor 1080p60 throughput.
Record `chrome://gpu`, `chrome://webrtc-internals`, inbound codec/profile and available decoder/power-efficiency diagnostics during qualification.
Hardware AVC baseline/High is common across these platforms; universal availability is not a valid design assumption. [Chromium media architecture](https://chromium.googlesource.com/chromium/src/+/main/media/README.md).

### Versioned receiver controls

| Control | Version evidence | Meaning / fallback |
| --- | --- | --- |
| `RTCRtpReceiver.playoutDelayHint` | M78 release IDL still has trial-gated `jitterBufferDelayHint`; M79 `79.0.3945.79` has ungated `playoutDelayHint`; M89 also has it | Nonstandard nullable **seconds**; set 0 after feature detection |
| `RTCRtpReceiver.jitterBufferTarget` | Shipped **Chrome 124**, 2024-04-16 | Nullable **milliseconds**; use 0 where supported; on 89 use the legacy hint plus RTP extension |
| Sender RTP playout-delay | Parsed and applied in M89 libwebrtc source | Best-effort min/max bounds in 10 ms units; `(0,0)` requests immediate presentation |
| `requestVideoFrameCallback` | Chrome 83+ API | Feature-detect metadata separately; API presence does not guarantee every WebRTC timing field |

Release-source evidence: [M78 IDL](https://chromium.googlesource.com/chromium/src/+/78.0.3904.70/third_party/blink/renderer/modules/peerconnection/rtc_rtp_receiver.idl), [M79 IDL](https://chromium.googlesource.com/chromium/src/+/79.0.3945.79/third_party/blink/renderer/modules/peerconnection/rtc_rtp_receiver.idl), [M89 IDL](https://chromium.googlesource.com/chromium/src/+/89.0.4389.90/third_party/blink/renderer/modules/peerconnection/rtc_rtp_receiver.idl), [Chrome 124 release notes](https://developer.chrome.com/release-notes/124).
The old ChromeStatus entry tracks the M75-M76 trial and has stale/incomplete shipping metadata; source comparison is the stronger evidence for the renamed API.
The October 2019 intent describes the API as a requested minimum delay, not a way to disable all buffering. [Intent to ship](https://groups.google.com/a/chromium.org/g/blink-dev/c/4W4orKqA3Rs/m/H1GYmVriDwAJ).

```js
pc.addEventListener('track', ({receiver}) => {
  if ('jitterBufferTarget' in receiver) receiver.jitterBufferTarget = 0; // ms
  else if ('playoutDelayHint' in receiver) receiver.playoutDelayHint = 0; // s
});
```

Current Blink `RTCRtpReceiver::setPlayoutDelayHint` and `setJitterBufferTarget` both call `SetJitterBufferMinimumDelay`; the latter converts milliseconds to seconds.
Zero removes an application-imposed minimum; it does not cap adaptive delay. Do not set competing nonzero legacy and modern hints.

### How the sender actually requests immediate rendering

Negotiate `a=extmap:<id> http://www.webrtc.org/experiments/rtp-hdrext/playout-delay` on the video m-line, with compatible directions.
Use the negotiated ID from the answer, never a hardcoded ID or merely an unaccepted offer line.
The extension has a **three-byte payload** holding two 12-bit unsigned values; for `(0,0)` the payload is `00 00 00`.
The RFC 8285 one-byte extension element's length nibble is 2 (payload length minus one).
Register the URI in the stack's media engine, then attach its payload before SRTP protection/packet pacing.
For a first implementation send it on every video packet; optimize repetition only after acknowledgement behavior is verified, and resend after IDR/restart.
SDP `fmtp`, `b=AS`, receiver JS, and a DataChannel message cannot substitute for the RTP element. [Extension format and sender repetition](https://webrtc.googlesource.com/src/+/refs/heads/main/docs/native-code/rtp-hdrext/playout-delay/README.md).

**Verified M89 path:** `RtpVideoStreamReceiver` extracts playout bounds; `VideoReceiveStream::OnCompleteFrame` updates them; `VCMTiming::RenderTimeMsInternal` returns 0 when min=0/max=0.
The same function also permits min=0/max<=500 with the enabled-by-default low-latency renderer experiment, but `(0,0)` does not depend on that branch.
`VideoReceiveStream::UpdatePlayoutDelays` takes the maximum of frame/base/sync minimums, so a nonzero JS minimum can defeat the desired M89 immediate branch.
`WebMediaPlayerMSCompositor::EnqueueFrame` documents that frames without reference-time bypass its ordinary smoothing algorithm unless the dedicated composition-delay algorithm is requested.
This is an immediate-rendering **path**, not zero network/decode/display latency. Frame assembly, decoder dependencies, missing packets, GPU queues and display vsync remain.
Use `(0,20)` or `(0,40)` only as measured alternatives if `(0,0)` causes poor smoothness under Wi-Fi jitter.
Current libwebrtc has evolved minimum/maximum precedence; test the oldest supported binary independently.
[M89 timing](https://webrtc.googlesource.com/src/+/e7d9f74b256605556726c64dacd518a105ba872f/modules/video_coding/timing.cc), [M89 receive stream](https://webrtc.googlesource.com/src/+/e7d9f74b256605556726c64dacd518a105ba872f/video/video_receive_stream.cc), [M89 compositor](https://chromium.googlesource.com/chromium/src/+/89.0.4389.90/third_party/blink/renderer/modules/mediastream/webmediaplayer_ms_compositor.cc).

## Sender stack comparison

The comparison concerns an endpoint consuming **already captured GPU frames**, not camera capture or browser publishing.
Every option still needs signaling, adapter selection, encoder rate adaptation, frame admission and measured queue limits.
No primary source establishes a reliable universal per-stack latency number; buffering configuration dominates language overhead.

| Stack | Congestion / pacing | Repair / keyframe feedback | Extension support | Windows cost / maturity |
| --- | --- | --- | --- | --- |
| Native libwebrtc C++ | Integrated GoogCC, TWCC, probing and production pacer | NACK/RTX; RED/ULPFEC/FlexFEC machinery subject to negotiation and codec policy; PLI/FIR enter encoder keyframe flow | Native playout delay and absolute capture time | Largest GN/depot_tools toolchain and API maintenance burden; closest match to Chrome's heavily exercised transport |
| webrtc-rs 0.21.1 + rtc | Explicit `configure_congestion_control(..., Gcc, Twcc)`; sender pacing is opt-in | Default NACK responder/generator; RTX when codec/SSRC configured; FlexFEC-03 modules exist, not enabled by prior app; PLI/FIR app-handled | Typed `PlayoutDelay`; `Custom` supports abs-capture-time payload | Cargo/MSVC and existing endpoint integration; new 0.21 core needs production qualification |
| libdatachannel C++ | `PacingHandler` exists; application must supply controller/target; no verified integrated GCC equivalent | `RtcpNackResponder` includes negotiated RTX; app keyframe callbacks; no verified turnkey adaptive video FEC | Packet/media handlers permit custom negotiated RTP fields | CMake/vcpkg, smaller than libwebrtc; mature transport, substantial media policy remains ours |
| Pion Go | `interceptor/pkg/gcc` + cc/TWCC configuration; explicitly choose a real pacer | NACK/RTX and feedback interceptors; FEC depends on selected interceptors and browser negotiation; app consumes PLI/FIR | RTP `Header.SetExtension`; playout-delay and capture-time helpers/custom payloads | Go easy; D3D11/oneVPL through cgo/C ABI adds ownership work; broad server deployment history |
| GStreamer `webrtcbin` | Attach `rtpgccbwe` with TWCC; wire estimate to encoder; `webrtcsink` automates more | RTP manager supports RTX/FEC; transceiver `do-nack`/FEC policy and upstream keyframe events | RTP header-extension objects or payloader/pad hooks; verify plugins for chosen URIs | Good Windows distributions/D3D11 elements; many plugins/queues and old-GPU QSV runtime compatibility to package |
| str0m Rust | GCC/TWCC with pacing and probing, `enable_bwe(Some(...))`; BWE defaults off | NACK/RTX and keyframe request events; do not assume browser-compatible video FEC without a spike | Source parses/writes both playout-delay and abs-capture-time; check public writer for pinned release | Cargo plus selected DTLS crypto provider, Windows crypto option; sans-I/O makes deadlines explicit, less transport battle history than libwebrtc |

**Native integration:** supply `VideoEncoderFactory::Create` returning a oneVPL-backed `VideoEncoder`.
Implement `InitEncode`, `Encode`, `RegisterEncodeCompleteCallback`, `SetRates`, `Release`, and honest `GetEncoderInfo` (hardware/native-surface support).
Represent textures with a ref-counted native `VideoFrameBuffer`; do not invoke `ToI420` in the normal path.
Return Annex-B H.264 `EncodedImage` access units with correct dimensions, RTP/capture timestamps and key/delta types through `EncodedImageCallback`.
Forward keyframe requests to `mfxEncodeCtrl`, and apply `SetRates` without building a backlog.
Current `EncodedImage::SetPlayoutDelay(VideoPlayoutDelay(...))` is the direct metadata route; `RTPSenderVideo` serializes `PlayoutDelayLimits` when negotiated.
An encoder facade that hands through an externally encoded access unit is possible; a general public PeerConnection `sendEncodedH264` API is not available.
An encoded-frame transformer also does not automatically replace the capture/encoder scheduling pipeline.
Lower-level RTP injection bypasses valuable pacing/feedback/adaptation integration unless those modules are deliberately retained.
[Native build guide](https://webrtc.github.io/webrtc-org/native-code/development/), [encoder contract](https://webrtc.googlesource.com/src/+/95554e4a0c5c343fdc989387d9d0ce18e2a2294b/api/video_codecs/video_encoder.h), [encoded image metadata](https://webrtc.googlesource.com/src/+/95554e4a0c5c343fdc989387d9d0ce18e2a2294b/api/video/encoded_image.h).

**Rust integration, verified installed source:** `webrtc-0.21.1/src/media_stream/track_local/static_sample.rs::write_sample` accepts `&[rtp::extension::HeaderExtension]` and forwards them to each RTP packet.
Replace the prior app's `&[]` with `HeaderExtension::PlayoutDelay(PlayoutDelayExtension { min_delay: 0, max_delay: 0 })`, after registering its URI.
Use `HeaderExtension::Custom { uri, extension: Box<dyn Marshal> }` for the 8/16-byte absolute-capture-time form; no typed capture-time variant appears in this installed enum.
`rtc-0.21.1/.../interceptor_registry.rs::configure_congestion_control` installs the congestion controller, TWCC numbering/feedback and `PacerBuilder`.
`handler/interceptor.rs` publishes `Attribute::TargetBitrateChanged` into outbound stats `target_bitrate`; consume that estimate on a short control tick or add a narrow callback, rather than continuing to listen only for REMB.
The installed pacer defaults to **4096 packets**, initial **1 Mbps**, and drops arriving packets on overflow; both are unsuitable as an unexamined remote-desktop policy.
4096x1200-byte packets is roughly 3.9 seconds at 10 Mbps. Bound *age/bytes/frame admission*, not just packet count.
If overflow happens after encoding, resynchronize the reference chain; shrinking the packet queue and silently accepting truncation is insufficient.
The custom telemetry interceptor at `Slot::from(0)` is now wire-side of the explicit sender controller: verify retransmit counts and timestamp ordering after composing the registry.
Local source root: `C:/Users/Flysoft/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/`; [upstream architecture](https://github.com/webrtc-rs/webrtc), [rtc API](https://docs.rs/rtc/0.21.1/rtc/peer_connection/configuration/interceptor_registry/).

**libdatachannel detail:** its existing `H264RtpPacketizer` already handles NAL splitting/fragmentation, so a new packetizer is unnecessary unless special policy requires one.
If implementing it, support Annex-B/AVCC normalization, FU-A, SPS/PPS with IDR, one timestamp/access unit, final marker bit, sequence rollover and MTU headroom.
Its `PacingHandler(bitsPerSecond, sendInterval, maxQueueSize)` defaults `maxQueueSize=0`; explicitly bound it and handle overflow at a frame boundary.
This stack is attractive for controlled fixed-rate experiments, but implementing an independent GCC correctly is more work than reusing an existing sender controller.
[H.264 packetizer](https://github.com/paullouisageneau/libdatachannel/blob/master/include/rtc/h264rtppacketizer.hpp), [pacer](https://github.com/paullouisageneau/libdatachannel/blob/master/src/pacinghandler.cpp), [RTX responder](https://github.com/paullouisageneau/libdatachannel/blob/master/include/rtc/rtcpnackresponder.hpp).

Pion's bandwidth-estimation example explicitly configures cc/GCC and TWCC; choose `SendSideBWEPacer(NewLeakyBucketPacer(...))`, not an assumed default pacer.
The inspected leaky-bucket implementation uses a 5 ms tick and a 1.5 factor; measure timer wakeups/queue age and supply a bounded pacer if needed.
GStreamer `rtpgccbwe` has its own pacing thread and `notify::estimated-bitrate`; `webrtcbin`'s `request-aux-sender` integration is available since 1.22.
Use one-buffer leaky raw-frame queues, correct live timestamps, and avoid clock-synchronized sinks introducing unnecessary waits.
GStreamer `latency` on the receive side is not a substitute for a sender playout-delay extension.
[Pion example](https://github.com/pion/webrtc/blob/master/examples/bandwidth-estimation-from-disk/main.go), [GCC options](https://pkg.go.dev/github.com/pion/interceptor/pkg/gcc), [GStreamer BWE](https://gstreamer.freedesktop.org/documentation/rsrtp/rtpgccbwe.html), [webrtcbin](https://gstreamer.freedesktop.org/documentation/webrtc/).

str0m's `Event::EgressBitrateEstimate` directly fits an external encoder budget, with no internal network threads; the application must service every timeout and drain output correctly.
Its inspected `src/rtp/ext.rs` implements the two playout-delay bounds and absolute capture timestamp; some fields are private, so API exposure must be checked before selecting a release.
It is the strongest alternative if the new webrtc-rs controller/pacer needs substantial surgery; native libwebrtc is the transport-behavior reference if both Rust approaches fail the Wi-Fi qualification.
[str0m BWE design](https://github.com/algesten/str0m/blob/main/docs/BWE.md), [extension implementation](https://github.com/algesten/str0m/blob/main/src/rtp/ext.rs), [configuration](https://github.com/algesten/str0m/blob/main/src/config.rs).

## Intel encoder and concrete starting parameters

### Runtime selection on UHD 630

**Documented:** Coffee Lake UHD 630 is Gen9.5 and belongs to Media SDK's `KBLx` family (which explicitly includes CFL).
Use the **oneVPL dispatcher loading the legacy Intel Media SDK hardware runtime**, typically `libmfxhw64.dll` on Windows.
The newer oneVPL GPU runtime (`libmfx64-gen.dll` on Windows, `libmfx-gen.so` on Linux) targets Gen12/Xe and newer; installing it does not add Gen9.5 support.
Using modern dispatcher headers does not grant the legacy implementation API 2.x-only memory/import features.
Enumerate implementations, require hardware/AVC/D3D11, retain the returned implementation index and API version, then create the session and bind the endpoint's Intel D3D11 device.
Do not request a minimum runtime API 2.x if it filters out the required 1.x implementation.
Log dispatcher version, resolved runtime DLL path/version, graphics driver, adapter LUID and actual queried encoder parameters on every startup.
[Intel runtime mapping](https://www.intel.com/content/www/us/en/docs/onevpl/upgrade-from-msdk/2023-1/onevpl-hardware-support-details.html), [Media SDK family matrix](https://github.com/Intel-Media-SDK/MediaSDK/blob/master/README.rst), [dispatcher/session model](https://oneapi-spec.uxlfoundation.org/specifications/oneapi/v1.2-rev-1/elements/onevpl/source/programming_guide/vpl_prg_session).

AVC fixed-function **VDEnc/LowPower exists on Gen9**, including the Coffee Lake family; it is not reserved for Gen12.
`LowPower=ON` selects a different encoder path and changes supported controls/quality tradeoffs.
Windows driver/runtime capabilities still decide whether CBR, chosen profile, input format and intra refresh work together.
Query the complete parameter tuple with LowPower ON; if unsupported or unacceptable, retry OFF using the normal hardware AVC encoder, then retain the software fallback.
Do not transfer Linux HuC firmware setup instructions to Windows; Intel's Linux matrix is hardware/path evidence, not Windows qualification.
[Intel encode/BRC matrix](https://github.com/intel/media-delivery/blob/master/doc/quality.rst), [Media SDK AVC implementation](https://github.com/Intel-Media-SDK/MediaSDK/blob/master/_studio/mfx_lib/encode_hw/h264/src/mfx_h264_encode_hw.cpp).

### Initial encode contract

All numbers below are **proposed test inputs**, not promises that the installed Windows runtime accepts or obeys them.
Run `Query`, `QueryIOSurf`, `Init`, `GetVideoParam`; record corrections/warnings and inspect actual SPS/frames/bitrate.
`AsyncDepth=1` limits submitted work; it does not mean only one allocated texture, nor prove the encoder has no internal delay.
Use `MFX_IOPATTERN_IN_VIDEO_MEMORY`, NV12, progressive, storage 1920x1088 with crop 1920x1080, frame rate N/1, 8-bit YUV420.

| Parameter | 1080p60 starting point | 1080p30 starting point | Reason / qualification |
| --- | --- | --- | --- |
| `CodecId` / `CodecProfile` | AVC / constrained baseline | Same | Separate High-profile experiment only after negotiated support |
| `CodecLevel` | 4.2 | 4.0 | Verify actual SPS and receiver answer |
| `TargetUsage` | 7, compare 4 | 4, compare 7 | Latency/quality tradeoff; TU1 is not initial choice |
| `AsyncDepth` | 1 | 1 | One admitted encode operation |
| `GopRefDist` / `NumRefFrame` | 1 / 1 | 1 / 1 | IPPP; no B-frame reorder; test 2 references only if quality needs it |
| `LowPower` | ON if complete tuple passes; OFF fallback | Same | Compare VDEnc versus VME under concurrent Chromium GPU load |
| `RateControlMethod` | CBR | CBR | Test capped VBR + `LowDelayBRC` as second branch |
| `TargetKbps` / `MaxKbps` | 10000-16000, initially 12000 | Same range | These are elementary-stream rates; reduce for a 10-16 Mbps total path |
| `BufferSizeInKB` | 1-frame sweep: 21/25/34 | 1-frame sweep: 42/50/67 | At 10/12/16 Mbps respectively; integer ceiling, decimal KB |
| `InitialDelayInKB` | 21/25/34 for first sweep | 42/50/67 for first sweep | At most buffer size; compare half-full after basic conformance |
| Qualification fallback VBV | 63/75/100 (3 frames), then 100 ms | 84/100/134 (2 frames), then 100 ms | Raise only when decode/IDR/rate-quality evidence requires it |
| `GopPicSize` / `IdrInterval` | 65535 / 0 | 65535 / 0 | Long finite GOP approximates on-demand operation; force startup/PLI IDR |
| `AdaptiveI` / `AdaptiveB` | OFF / OFF | OFF / OFF | Avoid surprise keyframe bursts and GOP changes |
| `NumSlice` | 1; compare 4 | 1; compare 4 | More slices cost bits; complete-frame API gives no automatic slice streaming |
| `IntRefType` | `MFX_REFRESH_NO` | Same | Establish IDR-based recovery first |
| Refresh experiment | Vertical, cycle 60, delta 0 | Vertical, cycle 30, delta 0 | One-second rolling refresh; query LowPower support |
| LTR / adaptive LTR | OFF | OFF | No client reference-picture acknowledgement protocol initially |
| QP limits | Leave 0/default initially; compare min I/P=18, max I/P=42 | Same | Query support; hard quality cap can conflict with CBR and tiny VBV |
| Lookahead / ExtBRC | Disabled / OFF | Same | No multi-frame pre-analysis or external re-encode loop initially |
| SPS/PPS / timestamp | Send with each IDR; real capture time | Same | Startup/recovery and correct jitter estimation |

One-frame size calculation is `ceil(TargetKbps / (8*fps))` decimal KB; 12 Mbps/60 = 25 KB, 12 Mbps/30 = 50 KB.
`BRCParamMultiplier=1`; API documentation defines KB=1000 bytes and Kbps=1000 bits/s.
A one-frame VBV strongly constrains IDR size/quality; it is an aggressive experimental point, not a universally safe default.
Intel's older tuning guide recommends enough buffer for several large frames, so the table explicitly includes a less aggressive qualification fallback.
Neither `BufferSizeInKB` nor `InitialDelayInKB` is a command to sleep that many milliseconds in the endpoint; they shape rate control/HRD and may affect output or receiver policy.
[API definitions](https://github.com/intel/libvpl/blob/main/api/vpl/mfxstructures.h), [Media SDK tuning guide](https://www.intel.com/content/dam/develop/external/us/en/documents/intel-media-developers-guide.pdf).

**Rate control alternatives:** plain CBR avoids lookahead but may create filler/bursts; capped VBR permits simpler static scenes to use fewer bits and requires explicit `MaxKbps`.
`LA`, `LA_HRD` and `LA_ICQ` deliberately analyze future frames and are inappropriate for this latency floor.
ICQ/CQP have no reliable transport-rate ceiling; QVBR is quality-driven VBR whose availability and frame-size behavior must be queried, not assumed on UHD 630.
VCM is designed for temporally correlated IPPP content and can be an additional experiment; its non-HRD behavior needs wire-rate validation.
Keep adaptation independent of the chosen BRC mode: the encoder must follow the usable congestion budget.

`mfxExtCodingOption2::MaxFrameSize` is **bytes**, documented for VBR-based modes and ignored in others; do not claim it enforces a strict CBR packet cap.
For a 12 Mbps capped-VBR experiment, try 60 fps P=50000/I=100000 bytes, 30 fps P=100000/I=200000, with a compatible enlarged VBV and actual wire-burst measurements.
These two/four-frame limits trade quality for recovery burst size; Intel recommends still larger I-frame allowances, so measure scene cuts and text fidelity.
`MaxFrameSizeI/P` and `LowDelayBRC` in CodingOption3 are conditional features; rejected or silently rewritten values cannot be acceptance evidence.
Use a separate adequately sized `mfxBitstream` allocation; the requested VBV is not the output-array capacity.
[Frame-size and refresh controls](https://intel.github.io/libvpl/latest/API_ref/VPL_structs_encode.html).

**GOP/recovery:** `GopPicSize=0` is documented as unspecified even though explanatory pseudocode describes unlimited GOP; a runtime may substitute a default.
65535 is an explicit long GOP (about 18.2 minutes at 60 fps), not literal infinity. Inspect periodic keyframes and accept a rare maintenance IDR, or verify a runtime-specific unlimited mode.
On PLI/FIR/new subscriber/resize/reference-chain loss, submit `mfxEncodeCtrl.FrameType = I | IDR | REF`, retaining the request until an actual IDR is observed in output.
Coalesce repeated feedback; do not force an IDR for every lost packet or every PLI in a storm.
Rolling refresh (`IntRefType`, `IntRefCycleSize`, optionally `IntRefCycleDist`) spreads cost, but is not an immediate random-access point for a new decoder.
Keep an actual IDR path even when refresh is enabled; verify Chrome's handling of recovery-point SEI and gradual recovery under loss.
LTR needs coordinated reference selection/feedback to be useful after loss; enabling it alone does not make Chrome acknowledge a usable long-term reference.
[Intel GOP guidance](https://community.intel.com/t5/Media-Intel-Video-Processing/Bitstream-sequence/td-p/1063979), [streaming features](https://www.intel.com/content/www/us/en/developer/articles/technical/video-conferencing-features-of-intel-media-software-development-kit.html).

**Static frames:** skip raw submissions when there is no compositor damage, preserving time in RTP and RTCP. Wake immediately on input/damage/PLI/new viewer.
Maintain protocol keepalives and a sparse refresh policy; do not use repeated IDRs as an idle heartbeat.
Encoder skip-frame controls are optional and differ by BRC/runtime; endpoint admission is simpler and avoids consuming GPU work for known duplicates.

## GPU surface ownership and conversion

## Pacing, congestion, and capture scheduling

## Measurement harness

## Recommendation

## Open questions / spikes to verify

