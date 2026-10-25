# Task e: low-latency WebRTC video pipeline
Write: C:\Users\Flysoft\Desktop\remotekit\research\e-video.md

Mostly web research plus the prior art (C:\Users\Flysoft\Desktop\chromium\prtsd and docs/prts: read how it encodes, paces
and sends, and any measured latency). Cover, concretely:

1. Receiver constraints: H.264 in Chrome 89+ (constrained baseline 42e01f, packetization-mode=1, high profile; hardware
   decode availability on Windows/Android/macOS; the safest SDP fmtp). Receiver-side low-latency knobs available in
   Chrome 89: RTCRtpReceiver.playoutDelayHint (non-standard; since which version?), jitterBufferTarget (which version shipped;
   if later than 89, the fallback), the playout-delay RTP header extension
   (http://www.webrtc.org/experiments/rtp-hdrext/playout-delay; min=max=0 => libwebrtc low-latency renderer path),
   how the SENDER sets it, and how that interacts with the jitter buffer. Give exact versions where known.
2. Sender stack options for the browser-endpoint on Windows: (a) native libwebrtc C++ (build effort, custom
   VideoEncoderFactory wrapping a HW encoder, or encoded-frame injection), (b) webrtc-rs (Rust; prior art uses what?),
   (c) libdatachannel + own H.264 RTP packetizer, (d) Pion (Go), (e) GStreamer webrtcbin, (f) str0m (Rust, sans-IO).
   Compare: congestion control (GCC/TWCC), pacing, NACK/RTX, FEC, PLI/FIR handling, playout-delay ext, abs-capture-time,
   build complexity on Windows, latency overhead, maturity. Recommend one and justify.
3. Encoder: Intel oneVPL on UHD 630 (Gen9.5 Coffee Lake). Which runtime (legacy Media SDK runtime via the oneVPL dispatcher vs
   the oneVPL GPU runtime which needs Gen12+; verify). H.264 low-latency settings: TargetUsage, AsyncDepth=1, GopRefDist=1
   (no B-frames), LowPower/VDEnc availability for AVC on Gen9, RateControl (CBR vs VBR vs LA/ICQ/QVBR), BufferSizeInKB /
   InitialDelayInKB (~1 frame VBV), MaxFrameSize, IntRefType/IntRefCycleSize (intra refresh vs IDR), NumSlice, infinite GOP +
   on-demand IDR (on PLI), NumRefFrame, LTR, QP bounds, skip frames when static. Give a concrete parameter table for
   1080p@60 and @30 at 10-16 Mbps.
4. Zero-copy: shared D3D11 texture (from Chromium via NT handle + keyed mutex) into oneVPL: device sharing
   (MFX_HANDLE_D3D11_DEVICE, external frame allocator, mfxFrameSurface1 MemId wrapping ID3D11Texture2D),
   BGRA->NV12 (VPP on GPU vs encoder taking RGB4 directly on Gen9: verify), cross-adapter caveats, expected per-frame cost.
5. Pacing and congestion control for low latency: frame pacing vs burst send, pacer settings, GCC vs fixed bitrate on a LAN
   with a Wi-Fi peer, reaction to loss (NACK vs intra refresh vs PLI), frame-drop policy, capture-on-demand driven by the
   encoder budget (endpoint drives Chromium BeginFrames).
6. Measuring glass-to-glass and per-stage latency: frame-ID overlay (encode a counter/bitcode into pixels, read back in the
   client via canvas + requestVideoFrameCallback), getStats (jitterBufferDelay, totalDecodeTime, totalProcessingDelay,
   framesDropped), abs-capture-time ext + rVFC captureTime/receiveTime, clock sync, camera-based glass-to-glass.
   Propose a concrete experiment harness and scenario list (scrolling, video playback, real-time game).
7. Recommendation: the stack, the initial parameter set, and the top latency risks.
