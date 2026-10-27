# Task m3-vpl-draft (timeout 45 min): DRAFT the oneVPL GPU encoder path inside prts_endpoint (zero copy from the capture texture)
Task dir C:\src\tmp\m2\vpl\. DRAFT ONLY: another agent builds and runs the chain. Do NOT build in out\dev, do NOT launch chrome/endpoint, do NOT edit the
remotekit repo or C:\src\chromium\src in place. Produce remotekit.diff against remotekit HEAD (endpoint/ files + BUILD.gn; verify with
`git -C C:\Users\Flysoft\Desktop\remotekit apply --check`) plus any vendoring steps. You MAY compile single translation units standalone with MSVC
(as the previous vpl task did in C:\src\tmp\m1\vpl\check) to catch errors.

Inputs: the previous draft and review C:\src\tmp\m1\vpl\{vpl_encoder.cc,NOTES.md,REVIEW.md,BUILD.gn.snippet,libvpl-manifest.json} (settings in REVIEW.md
"M3 starting configuration"; take them as the starting point, but note: our frame rate is 30 fps today and may become 60, and resolution changes at
runtime with the viewer size), research/e2-vpl-zero-copy.md (zero-copy sequence), research/s7-onevpl-probe.md, design/03-video-pipeline.md, and the current
endpoint frame path endpoint/d3d_frame_source.cc (keyed mutex -> private copy -> staging -> I420) and peer.cc (encoder factory, OpenH264).
Deliver:
1. A D3D11 path: the endpoint opens the capture's shared NT handle texture (cache per DXGIHandleToken / handle as the design says), acquires the keyed mutex
   (key 0), does ONE GPU copy (or a VPP BGRA->NV12 with BT.709 limited range) into a VPL surface allocated on the same ID3D11Device that the VPL session
   uses (MFX_HANDLE_D3D11_DEVICE, external frame allocator or the VPL 2.x internal allocation with mfxFrameSurfaceInterface), releases the mutex, encodes.
   No CPU readback on this path. Carry the texture to the encoder as a webrtc::VideoFrameBuffer of kNative type (custom subclass) so the WebRTC
   encoder adapter receives it; ToI420() must still work (slow path) for fallback/simulcast safety.
2. The webrtc::VideoEncoder (from the previous draft) accepting both the native D3D11 buffer and I420; low-latency settings; runtime SetRates handling
   without forced IDR where the runtime allows; resolution change -> reinit + IDR; keyframe requests; H.264 constrained baseline, packetization mode 1,
   the true SPS level for the resolution/fps.
3. Selection: `--encoder=vpl|openh264` (default vpl when a VPL implementation loads, fallback openh264 with a log line), and the frame source producing
   native frames only when the VPL encoder is active.
NOTES.md: design, file list, API citations (oneVPL headers + libwebrtc at the pin), integration steps in order, the smoke test plan (what to measure), risks
(keyed mutex hold time, device/adapter mismatch between Chrome's GPU process and the endpoint, surface pool size vs AsyncDepth).
