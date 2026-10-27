# Task endpoint-webrtc (timeout 45 min): //prts_endpoint skeleton: libwebrtc, stdio signalling, DataChannels, H.264 (OpenH264)
Output dir: C:\src\tmp\m1\endpoint\  (becomes remotekit endpoint/, junctioned to C:\src\chromium\src\prts_endpoint)

Goal (M1, "works first"): an in-tree executable that speaks design/06 section 1.4 (stdio, 4-byte LE length + JSON) with control-plane
(read control-plane/endpoint.js, framing.js, index.js and tools/mock-endpoint/mock.js to match exactly what the CP and viewer expect).
On `session.open` it creates a PeerConnection (endpoint = offerer), one sendonly H.264 video track, 3 negotiated DataChannels
(id 0 `input` binary, 1 `ctl` JSON, 2 `probe` unordered maxRetransmits=0), trickles ICE, answers the `ctl` hello and `probe` pings (06 3.1, 4.1).
Video: a pluggable `FrameSource` interface; implement a synthetic test-pattern source (I420 1920x1080, moving bar, and the 06 4.2 marker
with frameId/time) so the endpoint can be tested before the browser link exists. Another task adds the D3D11 source.
Input: parse 06 2.2 binary messages into the structs of C:\src\tmp\m1\contract\prts.mojom (PointerInput/WheelInput/KeyInput; you may use
plain C++ mirror structs) delivered to an `InputSink` interface (stub logs them). ctl `ime.*`/`focus`/`keyframe` -> same sink interface.

Requirements (verify each against the pin's sources, cite in NOTES):
1. BUILD.gn: `executable("prts_endpoint")` with deps on the libwebrtc targets Chromium itself uses (see
   third_party/blink/renderer/platform/peerconnection or modules/peerconnection BUILD deps, //third_party/webrtc_overrides,
   remoting/protocol/BUILD.gn), //base, //third_party/libyuv. Decide threads + socket factory: does libwebrtc's own PhysicalSocketServer /
   BasicPacketSocketFactory work under build_with_chromium (S9a), or is the remoting approach (chromium_socket_factory, net::UDPSocket)
   required? Pick what compiles and runs with least code; cite evidence (check webrtc_overrides replacing rtc_base pieces, e.g. task queue,
   time, metrics, field trials).
2. Encoder: builtin/libwebrtc H.264 (OpenH264; our args set rtc_use_h264=true, proprietary_codecs=true), constrained baseline,
   packetization-mode=1; offer fmtp `profile-level-id=42e01f;packetization-mode=1;level-asymmetry-allowed=1`. If the builtin factory is not
   linkable under build_with_chromium, say why and create H264Encoder directly.
3. Low latency: the playout-delay RTP header extension (min 0, max 0) MUST be sent (Chrome 89 needs it). Find how the pin's libwebrtc
   negotiates `http://www.webrtc.org/experiments/rtp-hdrext/playout-delay` (transceiver SetHeaderExtensionsToNegotiate / default list)
   and how frames carry playout delay (see remoting/protocol/webrtc_video_encoder_wrapper.cc SetPlayoutDelay). content_type SCREENSHARE,
   DegradationPreference MAINTAIN_RESOLUTION, start ~8 Mbps, max 12 Mbps, min 2 Mbps.
4. Field trials / logging init for a standalone binary; flags: --instance=, --chrome= (path, unused here), --fps= (default 30).
Deliverables: BUILD.gn, main.cc, signaling.{h,cc} (stdio framing + JSON via base::JSONReader/Writer, reader thread), peer.{h,cc},
frame_source.h, synthetic_source.{h,cc}, input_parser.{h,cc}, NOTES.md (integration steps: junction, patch 14 adding //prts_endpoint to
gn_all in //BUILD.gn, build command `autoninja -C out\dev prts_endpoint`). Max ~1500 lines. Do not build (no gn/ninja in src).
