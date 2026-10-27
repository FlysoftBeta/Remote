# m2-netem notes (work in progress; numbers filled in below as runs finish)

## Design
- `tools/netem/relay.mjs`: own minimal UDP TURN server (RFC 5766 subset: Allocate, Refresh, CreatePermission (implicit/accept-all), ChannelBind, Send/Data indications,
  ChannelData, long-term credentials with HMAC-SHA1 MESSAGE-INTEGRITY). node-turn was NOT used: the forwarding path must be hooked per direction, and a ~200 line server is simpler than patching it.
  Relay-to-relay traffic is not sent over a real socket: a Send/ChannelData from client X to peer address = Y's relayed address is handed straight to the link emulator and then
  delivered to Y's client as Data/ChannelData with the peer address = X's relayed address. Every packet (RTP, RTCP, SCTP/DataChannels, STUN checks) is shaped exactly once per direction.
  Direction: allocation with username `endpoint` = `down` (endpoint -> viewer, the media), every other username = `up` (input/DataChannel/RTCP feedback).
- `tools/netem/shaper.mjs`: per-direction FIFO `Link`: (1) Gilbert-Elliott loss (p good->bad, r bad->good, lossBad) and uniform `lossPct`; (2) drop-tail queue limited in BYTES
  (`queueBytes`, default `queueMs`=50 ms x rate = 75000 B at 12 Mbps), queued bytes = (serializer_free_time - now) x rate; (3) rate serializer: departure = max(arrival, free) + size/rate
  (equivalent to a token bucket with zero burst, packet size = UDP payload + 28 B IP/UDP header); (4) one-way delay = delayMs + N(0, jitterMs) clipped >= 0; (5) spikes: every `everyS` s for `durationMs`
  an extra uniform(minMs,maxMs) delay; (6) FIFO: departure is clamped to >= the previous departure (a real link does not reorder; jitter therefore shows up as occasional head-of-line holds,
  which raises the mean delay slightly above delayMs; `--reorder` disables the clamp).
- Queue sizing: 50 ms at the cap. A shallow queue (about one frame-burst of a 30 fps stream at 12 Mbps = 50 KB) like a modest router buffer; bigger queues would convert congestion into bufferbloat latency
  (up to 100s of ms) that the product's rate control would also see; with 50 ms the worst-case added queue delay is bounded and drops give libwebrtc's loss/delay-based BWE a clear signal. `--queue-ms`/`--queue-bytes` override.
- Timer: Windows default tick is 15.6 ms; Node `setTimeout` alone measured error p50 4.2 ms / p99 14 ms (see `t2`). The scheduler (min-heap) uses `setTimeout` until 16 ms before the due time and `setImmediate` spinning for the
  last 16 ms (`--spin-ms`): measured error p50 0.004 ms, p99 0.28 ms, max 0.9 ms (profile d50j10-c12, 20 s run). Cost: the relay process burns about one core while packets are queued. A `timeBeginPeriod(1)` helper (`--hires`) did NOT help.
- Why the relay listens on the LAN IP (`pickHost()`, 192.168.x): libwebrtc binds one port per local interface and cannot send from them to 127.0.0.1 (WSAEADDRNOTAVAIL 10049). Traffic to the machine's own address stays in the host stack.
- Force relay: viewer `?relay=turn:<ip>:<port>` -> RTCPeerConnection `iceServers=[{viewer/netem}]`, `iceTransportPolicy:'relay'`, and the same URL is sent in the signalling `hello` -> control-plane adds
  `iceServers` (user `endpoint`) + `iceTransportPolicy:'relay'` to `session.open` -> `endpoint/peer.cc` maps it to `RTCConfiguration::type = kRelay`. The endpoint additionally has a static `--turn=host:port[,user[,pass]]` switch (same effect).

## How to run
- Relay alone: `node tools/netem/relay.mjs --profile d50j10-c12 [--queue-ms N|--queue-bytes N] [--reorder] [--seed 1] [--host ip] [--port 3478] [--http 3479]`; stats `GET http://<ip>:3479/stats`, `POST /reset`. Unit check: `node tools/netem/selftest.mjs`.
- Harness: chain as before (`bash /c/src/tmp/m1/cp.sh 8300 --url=http://127.0.0.1:8300/tools/latency/remote-page.html --cr-enable-logging --cr-remote-debugging-port=9333`), then
  `node tools/latency/run.mjs --no-mock --port 8300 --secs 60 --remote-cdp 9333 --endpoint-log <chain log> --netem d50j10-c12 [--headed]`. Without `--netem` = plain loopback. Task-local driver: `C:\src\tmp\m2\netem\trial.mjs <name> 30 0` (env TRIAL_SECS, TRIAL_RUNARGS).
- Profiles: `tools/netem/profiles.json` (none, d25j5-c16/c12/c10, d50j10-c16/c12/c10, d25j5-c12-wifi, d50j10-c12-wifi = Gilbert-Elliott p=0.0025 r=0.25 lossBad=1 (~1% bursty loss) + every 5 s a 100 ms window with +30..50 ms).
- Every report JSON (`latency-*.json`) now has `pipeline` {remote (CDP SystemInfo.getInfo: GPU adapter, ANGLE impl parts, feature status incl. skia_graphite, GPU-process flags), capture, encoder (InitEncode log line: impl/res/fps/start-min-max kbps/cores), viewer (UA, headless/headed, decoderImplementation if exposed), display (measurement point + headless caveat), network (selected candidate pairs)} and, with --netem, `netem` (profile config, per-direction pkts/bytes/drops/max queue delay/achieved Mbps, timer error, per-allocation counters). The same is printed as `pipeline.*` / `netem:` lines.
- Endpoint rebuilt with `be.sh` (peer.cc: `iceTransportPolicy` from session.open, `--turn=`, richer InitEncode log).

## Verification evidence (relay forced on both sides)
- Selected pair in the report (viewer getStats): local=relay/udp (relayProtocol udp), remote=relay (`d50j10-c12b/latency-*.json` pipeline.pairs). First attempt with a 127.0.0.1 relay failed with WSAEADDRNOTAVAIL 10049 in the endpoint log, which led to the LAN-IP host.
- Relay counters (d50j10-c12b): allocation `endpoint` sent 1,324,253 B / 2857 pkts (down), `viewer` sent 117,008 B / 1759 pkts (up); `unshapedPkts 0`, `externalInBytes 0` (nothing bypassed the shaper); viewer pair bytesReceived 1,313,779 matches the down bytes. `unauthorized 20` = normal 401 challenge round trips. getStats RTT 0.102 s (2 x ~50 ms).
- Shaper unit run (selftest.mjs, 8 s each): 14.4 Mbps offered into the 12 Mbps cap -> achieved 12.075 Mbps, 1956 tail drops, max queue delay 49.15 ms, max queue 74,926 B of the 75,000 B limit; Gilbert-Elliott 0.58% loss (46/7990) with 0 reordered packets; (its d50j10 uncapped line was measured with the old 1 ms spin and shows the 15 ms tick error).
- Timer error in real runs: p50 0.004 ms, p95 0.07 ms, p99 0.23 ms, max 1.3-2.0 ms (scheduler error = actual - due; the OS UDP send time is not included).

## Self-check numbers (60 s real chain + 3 s warm-up, tab mode, OpenH264, Chromium 157 dev build sender, Edge 156 headless receiver; ms; P50/P80/P95/P99)
Common pipeline configuration (also stored in each JSON): remote Chrome 157.0.8103.15, GPU Intel UHD Graphics 630 (driver 30.0.100.9805), GPU process on, ANGLE D3D11 (gl=egl-angle,angle=d3d11), Skia Graphite enabled, not SwiftShader;
capture = Viz -> Mojo -> D3D11 copy -> staging Map -> libyuv I420 (CPU); encoder OpenH264 (screen mode, ContentHint fluid, MAINTAIN_RESOLUTION, playout-delay 0/0), ~1898x1068 @30 fps, start/min/max 4736/2000/12000 kbps, 16 cores;
viewer Edge 156 headless=new (no real vsync/scanout; display point = rVFC expectedDisplayTime estimate), window 1928x1162; page tools/latency/remote-page.html (marker page).

| run | network | capture->display | input->flip | notes |
|---|---|---|---|---|
| norelay | loopback host/host | 67.8 / 75.1 / 81.4 / 91.0 (n=1541) | 104.8 / 115.5 / 128.8 / 138.2 (120) | |
| norelay2 | loopback | 66.3 / 73.1 / 79.8 / 88.5 (1531) | 105.5 / 115.9 / 127.7 / 134.6 (120) | repeat |
| none | relay/relay, no shaping | 85.2 / 92.4 / 99.5 / 110.6 (1475) | 133.1 / 140.0 / 151.8 / 155.0 (119) | "slow state" run, see caveat 2 |
| none2 | relay/relay, no shaping | 69.6 / 79.3 / 87.4 / 95.3 (1505) | 106.0 / 118.6 / 133.1 / 138.7 (119) | repeat: relay itself adds about 2-3 ms |
| d50j10-c12 | relay, 50 ms +-10 ms, 12 Mbps | 117.7 / 130.9 / 143.0 / 154.1 (1307) | 227.0 / 242.2 / 255.4 / 264.1 (119) | |
| d50j10-c12b | same | 103.5 / 118.1 / 129.3 / 140.4 (1296) | 214.0 / 227.2 / 237.3 / 251.5 (119) | repeat; 1898x1020 |
| headed | loopback host/host, Edge HEADED | 85.7 / 93.8 / 100.0 / 108.0 (1417) | 136.1 / 145.3 / 155.5 / 158.5 (119) | encode 1930x1072 |
| headed2 | same | 87.0 / 95.8 / 101.9 / 108.8 (1358) | 135.4 / 147.9 / 157.8 / 169.2 (119) | repeat |
| wifi20 (20 s) | d50j10-c12-wifi | 110.9 / 124.6 / 140.2 / 154.1 (421) | 230.9 / 253.1 / 291.1 / 348.4 (39) | 4 loss events in 1132 pkts |

Reading: the 50 ms +-10 ms link adds about 36-50 ms to capture->display and about 108-120 ms to input->flip (up and down legs plus the encode path).
Caveat 1: the marker page produces only about 0.17 Mbps down / 0.02 Mbps up, so the 12 Mbps cap and its 75 KB queue were never reached in these runs (max queue delay 11 ms, 0 drops). Cap behaviour is verified only by the unit test; the matrix task needs content with real bitrate (video or scrolling page) for c10/c12 to matter.
Caveat 2: the chain is bimodal run to run: `none` (85 ms) and both headed runs (86-87 ms) sit about 18 ms above norelay/norelay2/none2 (66-70 ms), although none2 through the same relay is back at 69.6. Treat single-run differences below about 20 ms as noise and repeat runs. Cause of the mode switch not isolated (the headed runs also encode 1930x1072).
Hardware decode (headed): inbound-rtp decoderImplementation / powerEfficientDecoder stayed undefined in Edge 156 even with a granted fake capture permission (viewer `?hwinfo=1` does getUserMedia before creating the RTCPeerConnection, lab flags `--use-fake-ui-for-media-stream --use-fake-device-for-media-stream`). Alternative evidence: during a headed run the Windows GPU Engine counters show msedge pid 19056 `engtype_VideoDecode` about 6.2 % (chrome and prts_endpoint show 3D only) in `gpu-engines-headed.txt`, so hardware decode is in use; the decoder implementation name itself is NOT confirmed.

## Files
tools/netem/{relay.mjs,shaper.mjs,profiles.json,selftest.mjs}; tools/latency/{run.mjs (--netem, --remote-cdp, --endpoint-log, fake-media flags), pipeline.mjs}; viewer/viewer.js (?relay=, ?hwinfo=1, pipeline stats in report); control-plane/index.js (relay in viewer hello -> session.open);
endpoint/peer.cc (iceTransportPolicy, --turn, InitEncode log). Evidence: C:\src\tmp\m2\netem\<run>.stdout, <run>-harness.log, <run>-endpoint.log, <run>/latency-*.json|csv.

## Risks
- Relay is single-threaded Node and spins one core (spin-ms 16) while packets are queued; fine at 16 Mbps, not for much higher rates.
- Jitter is applied before the FIFO clamp, so the effective mean one-way delay is slightly above delayMs and visible jitter is smaller than sigma (head-of-line holds).
- Loss/spike random streams are seeded (--seed) but arrival times are not, so loss patterns are statistically, not bit-for-bit, reproducible. Loss also hits STUN keepalives/consent checks.
- UDP TURN only; permissions are not enforced; needs a real LAN IP (recomputed each run, changes with DHCP). Packet size accounting adds 28 B IP/UDP but not ChannelData/Ethernet framing.
- Edge decoder name not exposed; encoder/capture lines come from the chain log passed via --endpoint-log and are stale if the wrong file is given.
