# Task vpl-encoder (timeout 45 min): critical review of the encoder-tuning run + draft oneVPL webrtc::VideoEncoder for prts_endpoint
Output dir: C:\src\tmp\m1\vpl\ (draft code + REVIEW.md). READ-ONLY elsewhere: do not edit remotekit or C:\src\chromium\src, do not build there
(another task is building). You may compile small standalone checks under C:\src\tmp\m1\vpl\ with the VS toolchain if useful.

1. Critically review research/t1-encoder-tuning.md and tools/encoder-tuning/ (commit 07575f6; an autonomous run, unreviewed) against
   research/s7-onevpl-probe.md and design/03 sections 3.3-3.5: Is "P99 (encode + link queueing) ~22-25 ms at 12 Mbps" with VBR 10 Mbps,
   LowDelayBRC ON, VBV ~42 KB, frame cap ~20.8 KB at 1080p60 measured or modeled? On what content? Are frame caps honored in the
   bitstream (actual frame sizes), and what happens to quality/QP on scrolling text and IDR frames? Is the link-queueing model sane
   (12 Mbps = 1.5 MB/s -> a 20.8 KB frame = 13.9 ms serialization)? List claims as [V]/[I]/[wrong] with evidence (files/lines), and
   give the parameter set you recommend for M3 as a starting point, plus the OpenH264 fallback settings (threads/slices) with justification.
2. Draft //prts_endpoint oneVPL encoder files (vpl_encoder.{h,cc}, BUILD.gn snippet, NOTES): a webrtc::VideoEncoder (namespace webrtc at
   this pin; read third_party/webrtc/api/video_codecs/video_encoder.h) using libvpl dispatcher (vendor it how? decide: third_party in remotekit
   endpoint/third_party/libvpl like tools/s7-vpl-probe/build.bat does, built as a GN static_library) with D3D11 video-memory input. Input
   for M1/M3 bridging: accept I420 buffers (upload to an NV12 D3D11 texture) AND a native buffer type carrying an NV12 ID3D11Texture2D (for
   the zero-copy path later); SetRates -> Reset TargetKbps; key frame requests -> IDR; EncodedImage with H.264 CodecSpecificInfo
   (packetization mode 1). Mirror endpoint/peer.cc's current encoder factory usage so swapping is one line (read endpoint/peer.cc).
   Must compile against the pin as far as you can verify by reading headers; cite file:line.
Final message: 5-8 lines (review verdict + draft status).
