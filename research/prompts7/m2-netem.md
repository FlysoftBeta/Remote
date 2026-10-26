# Task m2-netem (timeout 45 min): network emulation for the latency harness (shaping TURN relay)
Task dir C:\src\tmp\m2\netem\ (notes, logs). Deliverable code in remotekit tools/netem/ (+ minimal hooks in tools/latency/, viewer/, control-plane/,
endpoint/ if needed). You are the only launcher of the chain now. No commits.

Today all latency numbers are loopback (no network). Build a controllable, reproducible network path for the WebRTC media AND the DataChannels:
1. tools/netem/: a small local TURN relay in Node (use the `node-turn` npm package if it works with libwebrtc/Chrome; otherwise a minimal TURN
   (RFC 5766 Allocate/CreatePermission/ChannelBind/Send/Data, long-term credentials) relay is acceptable) whose relay forwarding path applies, per
   direction: one-way delay + jitter (normal distribution, clipped >= 0, with no packet reordering unless --reorder: hold order like a real FIFO link),
   bandwidth cap via a token-bucket serializer with a drop-tail queue limited in BYTES (default = 50 ms worth at the cap; configurable; log drops and
   max queue delay), random loss and bursty loss (Gilbert-Elliott: p, r, loss in bad state), and occasional jitter spikes (e.g. every N s a 30-50 ms
   extra delay for a short burst). Profiles in a JSON file; select by name on the CLI.
2. Force relay: viewer RTCPeerConnection with iceServers=[the relay] and iceTransportPolicy 'relay' (behind a URL parameter, e.g. ?relay=turn:127.0.0.1:3478),
   and the endpoint configured with the same TURN server (libwebrtc PeerConnection config; find how endpoint/peer.cc builds its RTCConfiguration;
   add `--turn=` and force relay-only too) so that BOTH directions traverse the shaper. Verify with getStats (selected candidate pair types = relay)
   and the relay's per-direction byte counters.
3. tools/latency/run.mjs: `--netem <profile>` starts the relay with that profile, passes the viewer/endpoint parameters, and records the profile + the
   relay stats (drops, max queue delay, achieved rate) in the JSON report.
4. Self-check: profile "none" (relay, no shaping) vs. no relay, then one profile "d50j10-c12" (50 ms one-way +-10 ms, 12 Mbps cap): run 60 s each on the
   real chain and report capture->display and input->flip P50/P80/P95/P99. Do NOT run the full matrix (the next task does that).
Profiles to define: d25j5-c16/c12/c10, d50j10-c16/c12/c10, and Wi-Fi variants d25j5-c12-wifi, d50j10-c12-wifi (1% bursty loss + 30-50 ms jitter spikes).
5. Pipeline configuration recorded automatically in EVERY report JSON (and printed in the summary): remote Chromium render path (GPU adapter name,
   ANGLE backend / Skia Graphite or Ganesh, GPU process in use or SwiftShader; read from the remote browser via its --remote-debugging-port and CDP
   SystemInfo.getInfo, or from chrome_debug.log), capture/copy path (today D3D11 staging + I420), encoder (implementation name from the endpoint log or
   sender getStats encoderImplementation, threads, resolution, fps, target/max bitrate), viewer (browser + version, headless or headed, inbound-rtp
   decoderImplementation and powerEfficientDecoder from getStats), the display measurement point (rVFC expectedDisplayTime; in headless there is no
   real scanout/vsync, so state it), and the network (loopback or profile). Then run ONE 60 s loopback measurement with a HEADED Edge viewer
   (normal window on the interactive desktop, hardware decode; confirm decoderImplementation is a D3D11/hardware decoder) next to the headless one.
NOTES.md: design (queue sizing and why), how to run, verification evidence, self-check numbers, risks (Node timer resolution on Windows: use a
high-resolution scheduling approach and measure its error).
