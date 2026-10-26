# m2-netem-matrix completed

Completed 2026-10-26T07:42:56.622Z. 27 x 60s runs: 24 acceptance (four d25j5 profiles x S/V/G x two repetitions), two d50j10 stress S rows, one headed d25j5-c12 S repeat. Every combined capture and input target failed; individual failing thresholds are identified in the table. No builds, endpoint/Chromium edits, or commits.

Realistic endpoint egress was 2.1-4.6 Mbps, rather than the earlier ~0.17Mbps marker-only load. OpenH264 is the installed binary default (InitEncode impl=OpenH264 on every run); staged VPL source is not in this measured executable. Whole-run average egress stayed below all caps because software encoding was limiting, especially G (~10fps, ~80ms encoder P50). Short frame bursts still occupied the queue and Wi-Fi G reached the 50ms limit with tail drops. The data establishes failure of the latency targets for these workloads, not a passing sustained-10/12/16Mbps saturation benchmark [I].

Changed only tools/latency/{remote-page.html,scenarios.js,measure.js,run.mjs}. S=continuous 360 CSS px/s scroll through a long DOM text/image article; V=local 1920x1080 moving-pattern canvas fallback at requested30fps because ffmpeg is unavailable; G=1920x1080 game canvas with scrolling detailed terrain and160 moving sprites at requested30fps. Original default marker fixture remains available. All keep the marker and a small compression-tolerant input colour patch. Sender throughput and all-frame display gap counters are now exported in run reports.

Reproduce: `node C:/src/tmp/m3/netem-matrix/trial.mjs d25j5-c12 S 3 0` (final argument1=headed), or set the chain URL to `http://127.0.0.1:8300/tools/latency/remote-page.html?scenario=S` and use the existing `node tools/latency/run.mjs --no-mock --port 8300 --secs 60 --netem d25j5-c12 --remote-cdp 9333 --endpoint-log <chain log> --viewer-window-size 1928,1162`. Exact executed argv is saved in each command.json; driver source has full valid commands. All processes run sequentially and scripts clean the test chain after each run.

Common pipeline: Chromium157.0.8103.15 on Intel UHD630, driver30.0.100.9805, GPU process present, ANGLE D3D11, Graphite enabled. Viz -> Mojo -> D3D11 staging readback -> libyuv I420 CPU -> OpenH264 screensharing, kFluid, MAINTAIN_RESOLUTION,30fps requested,2Mbps min/12Mbps max, zero/zero sender playout delay and receiver target0. Edge156 headless=new, outer1928x1162; remote source1898x933 CSS/DPR1 within1898x1020 captured browser frame. V/G backing canvas1920x1080 is scaled to viewport. Forced TURN relay/relay on both sides, UDP, seeded loss/jitter, FIFO serializer,50ms queue (100000/75000/62500B at16/12/10Mbps); acceptance25+-5ms one-way (~50ms nominal RTT), stress50+-10ms one-way. Wi-Fi GE p=.0025,r=.25,lossBad=1 plus extra30-50ms for100ms every5s.

Headed repeat: actual encoded 1930x1072, Edge UA Chrome/156.0.0.0 Edg/156.0.0.0, headless=false; capture P80/P99=136.7/158.3ms, input=197.4/219.2ms. Headed changes window/compositor scheduling; it still does not measure monitor scanout.

Validation: JS syntax checks and git diff --check for task files pass; task-local verify-gaps.mjs passes the strict >200ms threshold, pre-marker frame counting and warmup reset. All raw CSV/JSON, per-run fixture counters, remote PNGs, endpoint/Chrome/harness logs, launch argv, completed.jsonl and series.jsonl are retained. No full test suite run.

Limitations: first seven r1 rows took an evidence screenshot ~10s into measurement; later rows and all r2 screenshots were taken after measurement or omitted. Early screenshot-related freezes/tails are not purely network evidence. First S initial metadata preceded settled rAF activity; later screenshot and marker samples show scrolling. Display is rVFC expectedDisplayTime, not physical photons. Decoder implementation is not exposed. Sender egress is measured from actual endpoint TURN allocation counters, not codec-only GetStats output; includes retransmission/control and excludes TURN/IP/UDP framing overhead. Its window uses nearest1Hz snapshots (~60s); queue/drop/timer counters include warmup. Capture quantiles use only CRC-valid unique markers; input-to-decoded flip uses one viewer clock and does not depend on marker decoding. Network/source/pacer dwell cannot be partitioned exactly from these aggregate stages.

## Per-run results

All runs: 60 s measured +3 s warmup; milliseconds P50 / P80 / P95 / P99 (n). C=capture/draw marker to rVFC expected display; I=DataChannel key to decoded colour flip. Target PASS requires P80<100 AND P99<200 for that metric.

| Run | C quantiles | I quantiles | Send Mbps | Drops q/l down;up | Max queue down/up ms | Freezes >200ms | C / I |
|---|---|---|---:|---|---|---:|---|
| d25j5-c16-S-r1 | 151.2 / 176.8 / 198.0 / 216.4 (1262) | 216.8 / 249.7 / 270.6 / 298.8 (110) | 2.1 | 0/0;0/0 | 32.3/0.5 | 2 | FAIL(P80,P99) / FAIL(P80,P99) |
| d25j5-c16-V-r1 | 125.9 / 139.7 / 154.3 / 168.7 (1499) | 177.6 / 201.0 / 223.4 / 249.7 (120) | 4.6 | 0/0;0/0 | 15.2/0.6 | 0 | FAIL(P80) / FAIL(P80,P99) |
| d25j5-c16-G-r1 | 228.7 / 241.8 / 259.5 / 274.9 (606) | 301.9 / 347.0 / 379.9 / 445.3 (120) | 3.6 | 0/0;0/0 | 17.2/0.6 | 1 | FAIL(P80,P99) / FAIL(P80,P99) |
| d25j5-c12-S-r1 | 143.6 / 158.7 / 172.6 / 181.4 (1432) | 200.9 / 214.2 / 230.0 / 247.7 (114) | 2.4 | 0/0;0/0 | 42.3/0.8 | 1 | FAIL(P80) / FAIL(P80,P99) |
| d25j5-c12-V-r1 | 162.6 / 177.1 / 188.4 / 197.9 (1291) | 217.1 / 235.1 / 250.3 / 269.0 (120) | 3.8 | 0/0;0/0 | 24.0/0.8 | 0 | FAIL(P80) / FAIL(P80,P99) |
| d25j5-c12-G-r1 | 243.7 / 260.9 / 275.7 / 287.2 (640) | 313.2 / 348.2 / 377.4 / 392.4 (120) | 3.8 | 0/0;0/0 | 26.7/0.8 | 0 | FAIL(P80,P99) / FAIL(P80,P99) |
| d25j5-c10-S-r1 | 145.6 / 158.6 / 170.5 / 184.1 (1433) | 196.3 / 214.6 / 230.2 / 248.4 (114) | 2.4 | 0/0;0/0 | 19.2/0.9 | 2 | FAIL(P80) / FAIL(P80,P99) |
| d25j5-c10-V-r1 | 138.1 / 166.5 / 193.7 / 210.8 (1384) | 194.1 / 215.8 / 245.4 / 261.3 (120) | 4.2 | 0/0;0/0 | 25.0/0.9 | 0 | FAIL(P80,P99) / FAIL(P80,P99) |
| d25j5-c10-G-r1 | 225.1 / 240.2 / 259.8 / 270.0 (655) | 298.8 / 332.1 / 361.7 / 386.6 (120) | 3.9 | 0/0;0/0 | 43.0/0.9 | 0 | FAIL(P80,P99) / FAIL(P80,P99) |
| d25j5-c12-wifi-S-r1 | 144.6 / 160.7 / 180.3 / 228.6 (1330) | 205.9 / 226.4 / 259.9 / 405.4 (113) | 2.6 | 0/148;0/7 | 40.5/0.7 | 2 | FAIL(P80,P99) / FAIL(P80,P99) |
| d25j5-c12-wifi-V-r1 | 169.2 / 185.0 / 199.8 / 213.5 (1222) | 221.9 / 242.5 / 261.8 / 287.1 (120) | 3.9 | 0/167;0/4 | 22.8/0.6 | 1 | FAIL(P80,P99) / FAIL(P80,P99) |
| d25j5-c12-wifi-G-r1 | 223.8 / 243.1 / 267.1 / 303.7 (646) | 294.5 / 335.8 / 366.7 / 509.9 (118) | 4.0 | 9/137;0/7 | 49.2/0.7 | 3 | FAIL(P80,P99) / FAIL(P80,P99) |
| d25j5-c16-S-r2 | 140.2 / 156.8 / 174.7 / 192.4 (1373) | 208.8 / 225.2 / 247.2 / 252.3 (114) | 2.4 | 0/0;0/0 | 16.7/0.6 | 1 | FAIL(P80) / FAIL(P80,P99) |
| d25j5-c16-V-r2 | 165.2 / 179.5 / 193.2 / 204.5 (1294) | 214.2 / 231.0 / 242.9 / 265.4 (120) | 3.8 | 0/0;0/0 | 16.1/0.6 | 0 | FAIL(P80,P99) / FAIL(P80,P99) |
| d25j5-c16-G-r2 | 228.0 / 240.9 / 255.7 / 266.3 (609) | 303.9 / 339.5 / 367.9 / 397.5 (120) | 3.6 | 0/0;0/0 | 17.4/0.6 | 2 | FAIL(P80,P99) / FAIL(P80,P99) |
| d25j5-c12-S-r2 | 150.3 / 161.6 / 173.4 / 188.8 (1443) | 199.4 / 213.5 / 227.9 / 232.8 (114) | 2.4 | 0/0;0/0 | 18.5/0.8 | 1 | FAIL(P80) / FAIL(P80,P99) |
| d25j5-c12-V-r2 | 129.2 / 140.7 / 161.8 / 183.1 (1435) | 183.2 / 200.0 / 224.9 / 242.6 (120) | 4.5 | 0/0;0/0 | 20.9/0.8 | 0 | FAIL(P80) / FAIL(P80,P99) |
| d25j5-c12-G-r2 | 238.2 / 252.8 / 275.9 / 324.6 (558) | 315.2 / 359.2 / 453.1 / 490.2 (119) | 3.3 | 0/0;0/0 | 22.7/0.8 | 9 | FAIL(P80,P99) / FAIL(P80,P99) |
| d25j5-c10-S-r2 | 151.1 / 166.1 / 179.7 / 192.3 (1380) | 207.8 / 221.2 / 246.9 / 252.2 (114) | 2.4 | 0/0;0/0 | 32.9/0.9 | 1 | FAIL(P80) / FAIL(P80,P99) |
| d25j5-c10-V-r2 | 175.3 / 189.6 / 205.3 / 216.5 (1288) | 223.2 / 231.7 / 250.7 / 262.8 (120) | 3.8 | 0/0;0/0 | 25.5/0.9 | 0 | FAIL(P80,P99) / FAIL(P80,P99) |
| d25j5-c10-G-r2 | 237.2 / 253.0 / 274.5 / 327.7 (589) | 306.9 / 341.8 / 370.0 / 388.1 (120) | 3.5 | 0/0;0/0 | 28.2/0.9 | 13 | FAIL(P80,P99) / FAIL(P80,P99) |
| d25j5-c12-wifi-S-r2 | 148.0 / 162.3 / 176.4 / 197.6 (1377) | 201.4 / 217.2 / 235.8 / 249.0 (112) | 2.5 | 0/107;0/7 | 15.8/0.7 | 2 | FAIL(P80) / FAIL(P80,P99) |
| d25j5-c12-wifi-V-r2 | 192.8 / 207.2 / 222.4 / 240.1 (1222) | 237.8 / 254.1 / 283.0 / 308.4 (120) | 3.9 | 0/186;0/0 | 25.7/0.7 | 0 | FAIL(P80,P99) / FAIL(P80,P99) |
| d25j5-c12-wifi-G-r2 | 233.3 / 251.7 / 269.5 / 294.0 (666) | 303.1 / 333.5 / 358.9 / 436.1 (120) | 4.3 | 6/209;0/1 | 49.1/0.8 | 4 | FAIL(P80,P99) / FAIL(P80,P99) |
| d50j10-c12-S-r1 | 155.9 / 173.6 / 189.9 / 205.8 (1335) | 245.4 / 258.8 / 280.4 / 303.8 (113) | 2.4 | 0/0;0/0 | 18.2/0.8 | 2 | FAIL(P80,P99) / FAIL(P80,P99) |
| d50j10-c12-wifi-S-r1 | 157.8 / 173.6 / 193.7 / 243.4 (1233) | 252.6 / 269.2 / 323.8 / 372.8 (112) | 2.5 | 0/119;0/1 | 15.4/0.8 | 4 | FAIL(P80,P99) / FAIL(P80,P99) |
| d25j5-c12-S-headed | 123.0 / 136.7 / 147.7 / 158.3 (1428) | 183.5 / 197.4 / 212.9 / 219.2 (114) | 2.1 | 0/0;0/0 | 35.6/0.9 | 1 | FAIL(P80) / FAIL(P80,P99) |

## Where the time goes

Endpoint P50/P95 below are medians of interior five-second window quantiles (first two and last windows excluded). Viewer receive->display is whole-run rVFC P50/P95; jitter/decode/fps are means of one-second getStats intervals. These cover different frame sets; quantiles overlap and MUST NOT be summed or subtracted as exact per-frame attribution. Network nominal delay is 25 ms one-way for acceptance, 50 ms for stress, plus jitter/FIFO holds/serialization/spikes. Input crosses the link twice.

| Run | Viz P50/P95 | Readback P50/P95 | I420 P50/P95 | Encode P50/P95 | Capture end->encoded P50/P95 | Viewer rx->display P50/P95 | Mean jitter/decode ms | Decoded fps | Relay timer P99/max ms |
|---|---|---|---|---|---|---|---|---:|---|
| d25j5-c16-S-r1 | 14.1/18.8 | 5.2/12.2 | 3.2/5.0 | 33.9/48.4 | 56.2/81.1 | 20.8/32.7 | 9.7/3.4 | 26.2 | 8.1/47.9 |
| d25j5-c16-V-r1 | 13.2/18.0 | 5.5/9.5 | 1.6/2.9 | 23.8/31.0 | 33.6/45.0 | 23.2/37.9 | 14.4/3.1 | 29.2 | 0.5/15.2 |
| d25j5-c16-G-r1 | 12.0/18.1 | 4.5/9.0 | 1.3/2.6 | 79.9/95.3 | 92.4/117.5 | 17.1/26.1 | 58.4/0.5 | 10.1 | 0.3/12.0 |
| d25j5-c12-S-r1 | 14.1/17.9 | 4.7/10.9 | 2.0/3.2 | 24.9/34.0 | 35.7/49.5 | 26.9/38.9 | 10.4/3.5 | 29.5 | 0.3/4.5 |
| d25j5-c12-V-r1 | 13.4/19.2 | 5.5/10.6 | 1.6/2.8 | 43.4/56.8 | 66.1/88.9 | 20.2/28.7 | 18.3/1.2 | 22.4 | 0.4/10.8 |
| d25j5-c12-G-r1 | 12.1/18.0 | 4.7/9.7 | 1.3/2.6 | 85.9/99.7 | 102.7/127.2 | 33.1/40.7 | 51.1/0.5 | 10.7 | 0.3/9.8 |
| d25j5-c10-S-r1 | 13.9/17.0 | 4.7/8.1 | 2.0/3.5 | 25.5/34.3 | 36.3/50.9 | 25.0/35.6 | 11.7/2.8 | 29.3 | 0.3/8.1 |
| d25j5-c10-V-r1 | 13.4/19.1 | 5.6/10.7 | 1.6/2.9 | 24.6/31.5 | 34.9/47.3 | 26.2/37.4 | 18.9/2.7 | 27.3 | 0.4/16.1 |
| d25j5-c10-G-r1 | 12.6/18.1 | 4.5/9.8 | 1.3/2.7 | 86.7/99.9 | 107.1/134.3 | 17.4/26.3 | 41.8/0.5 | 10.9 | 0.2/11.2 |
| d25j5-c12-wifi-S-r1 | 15.1/18.9 | 5.2/12.7 | 2.0/3.2 | 24.6/33.5 | 36.0/49.7 | 26.9/40.5 | 12.7/3.8 | 29.2 | 0.3/7.0 |
| d25j5-c12-wifi-V-r1 | 13.2/19.2 | 5.7/11.3 | 1.7/2.8 | 43.0/58.4 | 67.2/90.3 | 26.4/38.1 | 20.8/1.7 | 22.6 | 0.3/15.8 |
| d25j5-c12-wifi-G-r1 | 12.2/18.2 | 4.4/9.7 | 1.3/2.7 | 85.3/99.8 | 105.3/134.6 | 21.4/31.5 | 40.0/0.6 | 11.0 | 0.4/13.1 |
| d25j5-c16-S-r2 | 15.9/19.4 | 5.5/13.1 | 2.2/3.5 | 25.4/34.4 | 37.2/56.7 | 27.0/39.9 | 9.5/4.8 | 29.2 | 0.4/6.6 |
| d25j5-c16-V-r2 | 13.5/19.1 | 5.8/11.1 | 1.6/2.8 | 43.4/57.3 | 67.4/89.7 | 25.4/36.3 | 15.4/1.5 | 22.4 | 0.3/12.4 |
| d25j5-c16-G-r2 | 12.3/18.0 | 4.6/9.6 | 1.3/2.7 | 81.6/94.9 | 92.6/112.0 | 17.9/26.2 | 58.7/0.5 | 10.1 | 0.4/11.6 |
| d25j5-c12-S-r2 | 13.9/17.5 | 4.7/8.1 | 2.0/3.4 | 25.1/34.3 | 35.7/49.7 | 31.6/42.0 | 10.9/3.1 | 29.5 | 0.3/7.2 |
| d25j5-c12-V-r2 | 12.9/18.1 | 5.4/9.8 | 1.6/2.9 | 24.3/31.6 | 34.3/45.9 | 23.5/35.9 | 16.7/2.6 | 28.8 | 0.3/13.6 |
| d25j5-c12-G-r2 | 12.2/17.8 | 4.4/9.2 | 1.3/2.6 | 82.8/97.5 | 91.7/108.4 | 21.3/29.9 | 68.7/0.5 | 9.3 | 0.3/9.1 |
| d25j5-c10-S-r2 | 15.7/19.0 | 5.2/12.4 | 2.1/3.1 | 24.8/33.5 | 35.7/51.9 | 36.0/49.0 | 11.3/4.4 | 29.6 | 0.3/8.5 |
| d25j5-c10-V-r2 | 13.3/19.1 | 5.5/11.1 | 1.6/2.8 | 44.4/57.8 | 66.5/90.3 | 29.6/36.7 | 20.5/1.3 | 22.3 | 0.3/12.4 |
| d25j5-c10-G-r2 | 12.3/17.8 | 4.4/9.5 | 1.3/2.6 | 83.6/96.5 | 93.1/113.2 | 24.6/33.6 | 62.3/0.5 | 9.8 | 0.3/8.3 |
| d25j5-c12-wifi-S-r2 | 13.9/17.8 | 4.7/9.2 | 1.9/3.1 | 25.0/33.9 | 35.4/50.2 | 28.4/40.6 | 12.5/3.5 | 29.2 | 0.3/6.5 |
| d25j5-c12-wifi-V-r2 | 13.5/19.6 | 5.2/10.8 | 1.6/3.0 | 43.5/57.0 | 66.6/90.2 | 28.6/40.2 | 20.8/1.7 | 22.4 | 0.4/12.3 |
| d25j5-c12-wifi-G-r2 | 12.1/18.1 | 4.5/9.7 | 1.3/2.6 | 87.9/100.5 | 112.3/140.3 | 27.7/35.9 | 38.7/0.6 | 11.3 | 0.3/13.7 |
| d50j10-c12-S-r1 | 15.5/18.2 | 5.1/12.4 | 2.0/3.1 | 24.4/33.6 | 35.6/50.8 | 31.8/44.3 | 15.5/4.1 | 29.3 | 0.4/7.0 |
| d50j10-c12-wifi-S-r1 | 14.9/18.1 | 4.9/12.0 | 2.0/3.1 | 24.3/33.1 | 35.0/49.8 | 32.0/45.8 | 19.6/4.0 | 28.9 | 0.4/5.9 |
| d25j5-c12-S-headed | 13.7/16.9 | 5.0/8.4 | 2.2/3.5 | 26.1/34.7 | 37.0/52.2 | 22.9/34.7 | 9.7/3.3 | 29.6 | 0.4/7.6 |

## Profile summaries

- d25j5-c16: nominal 25 ms one-way; observed max down queue 15.2-32.3 ms; encode-window P50 23.8-81.6 ms; viewer rx->display P50 17.1-27.0 ms. Sender 2.1-4.6 Mbps.
- d25j5-c12: nominal 25 ms one-way; observed max down queue 18.5-42.3 ms; encode-window P50 24.3-85.9 ms; viewer rx->display P50 20.2-33.1 ms. Sender 2.4-4.5 Mbps.
- d25j5-c10: nominal 25 ms one-way; observed max down queue 19.2-43.0 ms; encode-window P50 24.6-86.7 ms; viewer rx->display P50 17.4-36.0 ms. Sender 2.4-4.2 Mbps.
- d25j5-c12-wifi: nominal 25 ms one-way; observed max down queue 15.8-49.2 ms; encode-window P50 24.6-87.9 ms; viewer rx->display P50 21.4-28.6 ms. Sender 2.5-4.3 Mbps.
- d50j10-c12: nominal 50 ms one-way; observed max down queue 18.2-18.2 ms; encode-window P50 24.4-24.4 ms; viewer rx->display P50 31.8-31.8 ms. Sender 2.4-2.4 Mbps.
- d50j10-c12-wifi: nominal 50 ms one-way; observed max down queue 15.4-15.4 ms; encode-window P50 24.3-24.3 ms; viewer rx->display P50 32.0-32.0 ms. Sender 2.5-2.5 Mbps.

## Pipeline from measured report

```json
{
  "relay": "turn:192.168.110.42:3478?transport=udp",
  "pairs": [
    {
      "local": "relay",
      "localProtocol": "udp",
      "relayProtocol": "udp",
      "localAddress": "192.168.110.42",
      "remote": "relay",
      "remoteAddress": "192.168.110.42",
      "currentRoundTripTime": 0.053,
      "bytesSent": 124322,
      "bytesReceived": 15811621
    }
  ],
  "inbound": {
    "framesPerSecond": 28,
    "frameWidth": 1898,
    "frameHeight": 1020,
    "framesDecoded": 1668,
    "framesDropped": 0,
    "packetsLost": 0,
    "nackCount": 0,
    "pliCount": 0,
    "firCount": 0,
    "retransmittedPacketsReceived": 25,
    "jitter": 0.012,
    "jitterBufferTargetDelay": 82.14322399999999
  },
  "headlessUa": true,
  "display": {
    "measurementPoint": "requestVideoFrameCallback expectedDisplayTime (compositor estimate)",
    "realScanout": false,
    "note": "headless=new: no real vsync/scanout, expectedDisplayTime is a compositor-clock estimate"
  },
  "remote": {
    "browser": "Chrome/157.0.8103.15",
    "gpuDevices": [
      {
        "vendor": "0x8086",
        "device": "Intel(R) UHD Graphics 630",
        "driver": "Intel 30.0.100.9805"
      },
      {
        "vendor": "0x1414",
        "device": "Microsoft Basic Render Driver",
        "driver": " 10.0.26100.9711"
      }
    ],
    "gpuFeatureStatus": {
      "2d_canvas": "enabled",
      "direct_rendering_display_compositor": "disabled_off_ok",
      "gpu_compositing": "enabled",
      "multiple_raster_threads": "enabled_on",
      "opengl": "enabled_on",
      "rasterization": "enabled",
      "skia_graphite": "enabled_on",
      "trees_in_viz": "enabled_on",
      "video_decode": "enabled",
      "video_encode": "enabled",
      "webgl": "enabled",
      "webgpu": "enabled",
      "webnn": "disabled_off"
    },
    "glRenderer": "ANGLE (Intel, Intel(R) UHD Graphics 630 (0x00009BC5) Direct3D11 vs_5_0 ps_5_0, D3D11-30.0.100.9805)",
    "glImplementationParts": "(gl=egl-angle,angle=d3d11)",
    "gpuProcessPresent": true,
    "gpuProcessFlags": [
      "--disable-features=PaintHolding"
    ],
    "softwareRenderer": false
  },
  "capture": {
    "path": "Viz capture -> Mojo -> D3D11 shared-texture copy -> D3D11 staging Map -> libyuv I420 (CPU) -> OpenH264 (software)",
    "lastD3dLine": "D3D source fps=30.8337 convert_ms=10.0261"
  },
  "encoder": {
    "initLine": "PRTS encoder InitEncode 1898x1020 max_fps=30 start_kbps=6883 min_kbps=2000 max_kbps=12000 cores=16 impl=OpenH264",
    "impl": "OpenH264",
    "fps": 30,
    "cores": 16,
    "startKbps": 6883,
    "minKbps": 2000,
    "maxKbps": 12000,
    "resolution": "1898x1020",
    "codec": "video/H264",
    "contentHint": "jitterBufferTarget=0 -> 0",
    "mode": "OpenH264 kScreensharing, track ContentHint kFluid, MAINTAIN_RESOLUTION, playout-delay 0/0",
    "relayOnly": "control-plane iceTransportPolicy=relay"
  },
  "viewer": {
    "ua": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) HeadlessChrome/156.0.0.0 Safari/537.36 Edg/156.0.0.0",
    "browser": "HeadlessChrome/156.0.0.0 Edg/156.0.0.0",
    "headless": true,
    "exe": "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
    "windowSize": "1928,1162"
  },
  "network": {
    "kind": "netem relay (forced relay-only both sides)",
    "profile": "d25j5-c16",
    "selectedPairs": [
      {
        "local": "relay",
        "localProtocol": "udp",
        "relayProtocol": "udp",
        "localAddress": "192.168.110.42",
        "remote": "relay",
        "remoteAddress": "192.168.110.42",
        "currentRoundTripTime": 0.053,
        "bytesSent": 124322,
        "bytesReceived": 15811621
      }
    ]
  }
}
```

## Measurement validation

Capture quantiles include only CRC-valid, unique marker frames. Input flip sampling is independent of marker decode and uses one viewer clock. Freezes count all rVFC display gaps, including marker failures. Sender Mbps uses endpoint TURN-allocation byte counters over the nearest 1 Hz snapshots to the measured window; it is endpoint payload egress excluding TURN/IP/UDP framing, including retransmissions/control, rather than codec-only output or receiver throughput. Relay queue/drop/timer counters cover connection setup and warmup as well as measurement. No physical scanout is measured.

| Run | Callback frames / valid marker / failures / duplicates | Input attempts / timeouts | Min probe RTT ms | Sender window s | Source final updates / inputs |
|---|---|---|---:|---:|---|
| d25j5-c16-S-r1 | 1486/1384/102/122 | 111/0 | 54.4 | 60.9 | 1/0 |
| d25j5-c16-V-r1 | 1676/1676/0/177 | 120/0 | 50.7 | 60.0 | 176/12 |
| d25j5-c16-G-r1 | 607/607/0/1 | 120/0 | 45.9 | 59.9 | 168/11 |
| d25j5-c12-S-r1 | 1650/1588/62/156 | 114/0 | 47.6 | 59.8 | 131/4 |
| d25j5-c12-V-r1 | 1323/1323/0/32 | 120/0 | 45.3 | 60.0 | 178/12 |
| d25j5-c12-G-r1 | 640/640/0/0 | 120/0 | 41.6 | 60.0 | 174/12 |
| d25j5-c10-S-r1 | 1696/1635/61/202 | 114/0 | 51.3 | 59.9 | 136/4 |
| d25j5-c10-V-r1 | 1546/1546/0/162 | 120/0 | 45.0 | 60.0 | 1665/124 |
| d25j5-c10-G-r1 | 655/655/0/0 | 120/0 | 41.0 | 59.9 | 1667/124 |
| d25j5-c12-wifi-S-r1 | 1546/1481/65/151 | 113/0 | 48.2 | 59.9 | 3086/116 |
| d25j5-c12-wifi-V-r1 | 1275/1275/0/53 | 120/0 | 45.6 | 60.0 | 1671/125 |
| d25j5-c12-wifi-G-r1 | 653/653/0/7 | 118/0 | 45.5 | 59.9 | 1668/122 |
| d25j5-c16-S-r2 | 1579/1517/62/144 | 114/0 | 46.2 | 59.9 | 3092/117 |
| d25j5-c16-V-r2 | 1325/1325/0/31 | 120/0 | 41.9 | 60.0 | 1664/125 |
| d25j5-c16-G-r2 | 609/609/0/0 | 120/0 | 45.8 | 59.9 | 1664/124 |
| d25j5-c12-S-r2 | 1693/1634/59/191 | 114/0 | 38.1 | 59.9 | 3092/117 |
| d25j5-c12-V-r2 | 1650/1650/0/215 | 120/0 | 49.8 | 60.0 | 1666/125 |
| d25j5-c12-G-r2 | 558/558/0/0 | 119/0 | 43.6 | 59.8 | 1663/121 |
| d25j5-c10-S-r2 | 1633/1568/65/188 | 114/0 | 53.4 | 59.8 | 3075/116 |
| d25j5-c10-V-r2 | 1321/1321/0/33 | 120/0 | 50.5 | 59.8 | 1666/124 |
| d25j5-c10-G-r2 | 589/589/0/0 | 120/0 | 39.4 | 59.9 | 1668/123 |
| d25j5-c12-wifi-S-r2 | 1607/1543/64/166 | 112/0 | 52.2 | 59.8 | 3087/114 |
| d25j5-c12-wifi-V-r2 | 1275/1275/0/53 | 120/0 | 43.6 | 59.9 | 1666/126 |
| d25j5-c12-wifi-G-r2 | 671/671/0/5 | 120/0 | 42.2 | 59.9 | 1668/124 |
| d50j10-c12-S-r1 | 1588/1522/66/187 | 114/0 | 100.0 | 59.7 | 3073/116 |
| d50j10-c12-wifi-S-r1 | 1474/1413/61/180 | 113/0 | 84.5 | 59.7 | 3078/115 |
| d25j5-c12-S-headed | 1703/1642/61/214 | 114/0 | 59.3 | 59.8 | 3079/116 |

## Work log / API citations

## Interim evidence / interpretation (15:19 SGT)
- Existing installed executable (last-write 12:34:24, SHA256 3C3C9AE07C0BB3651A7D477B8B61EF705FF816DFE36CDB706EDD78DD420CE3B1) logs impl=OpenH264 from peer.cc:63. It predates the currently dirty/staged VPL source; no builds performed. Endpoint default in this binary is therefore OpenH264. Repository HEAD 7e88818e9f07f3056192a1ae11ae423a7f7e2a67, Chromium HEAD c4699fcfdb72a2cba977394f4a89dc942db8cf71.
- First realistic samples: S ~2.1-2.4 Mbps, V ~3.8-4.6 Mbps, G ~3.6 Mbps; all target failures so far. G software encode stage ~80 ms P50, decoded ~10 fps, viewer jitter ~50 ms. The encoder is a bottleneck before any sustained 10-16 Mbps cap saturation [I]. Bursts still occupy 15-42 ms serializer queue.
- Initial r1 screenshots were captured ~10 s into measurement for the first profiles (up to d25j5-c10 S); these can perturb freezes/tails. Driver changed to take evidence screenshots AFTER measurement for subsequent rows; r2 has no screenshot during measurement. Do not claim early screenshot-contaminated freezes are purely network-caused. First S initial fixture metadata was read before rAF activity settled; final screenshot clearly shows active scrolling, and marker/input samples confirm ongoing content. Later driver saves both initial and final fixture metadata.
- Source page is 1898x933 CSS px within a 1898x1020 captured tab/frame at DPR1. V/G canvas backing is full 1920x1080, scaled to source viewport; the source is a synthetic 1080p canvas fallback, not a media-file decode benchmark.
- API/source inspection [V]: tools/netem/relay.mjs routes endpoint username to down link and increments allocation sentBytes from actual client payload before shaping. tools/netem/shaper.mjs adds 28 bytes IP/UDP for cap/queue accounting and uses 50ms byte-limited FIFO queue. endpoint/latency_stats.h logs bounded five-second P50/P95/n; viewer/viewer.js reports real DataChannel key to decoded colour flip, rVFC stage samples and inbound getStats. No C++ APIs added/called by this task.
- [V] Pin DOM/canvas bindings: third_party/blink/renderer/core/html/canvas/html_canvas_element.idl:36-37,53 (canvas backing dimensions/toDataURL), modules/canvas/htmlcanvas/html_canvas_element_module.idl:19 (getContext), modules/canvas/canvas2d/canvas_2d_recorder_context.idl:45,58,62-78 (gradient, rectangles, paths, drawImage), canvas_path.idl:10-11,20 (path/ellipse), base_rendering_context_2d.idl:16 (fillText); core/dom/element.idl:163,166,170 (scrollTop/scrollHeight/clientHeight). Calls exercised by real Chromium 157 fixture runs. No upstream hooks or C++ changes.
- Targeted verification: node --check passed for scenarios.js, measure.js and run.mjs; task-local verify-gaps.mjs passes strict >200 ms threshold (200 excluded,201 counted), pre-marker frame gaps and warmup reset behavior. git diff --check for tools/latency and tools/netem passes.
- First Wi-Fi S result: relay down loss148/up loss7, no queue drops, max down queue40.48ms; capture P99 228.6ms and input P99 405.4ms. Compare d25j5-c12 S plain input P99 247.7ms. Different runs/frame sets; this is evidence of tail sensitivity, not an exact paired causal decomposition [I].
- Clean repeats confirm the first-pass failures without screenshot capture during measurement. Run-to-run spread prevents a monotonic cap ranking: e.g. d25j5-c10 V capture P50 138.1 vs175.3ms, while endpoint send4.2 vs3.8Mbps. Report both rows; do not infer that the cap alone caused this change [I].
- Validation at21 completed runs passed: each60s, OpenH264, selected relay/relay, unshapedPkts0, externalInBytes0, nonempty capture/input samples, sender window58-63s with finite>1Mbps, scenario name matches. Input timeouts0 through these rows. Matrix-wide verification will be rerun after all27 rows.
- Runtime log confirms OpenH264 implementation, encoded resolution/fps request/rate bounds and CPU staging/I420 stages. The existing pipeline report's screen/fluid/MAINTAIN_RESOLUTION description comes from configured source/defaults; these track/encoder policy values are not separately introspected from the running sender [I]. Decoder implementation and actual monitor scanout remain unknown.
- Final 15:42 SGT: all27 runs completed and matrix coverage validation passed (24 acceptance,2 stress,1 headed); all27 have60s duration, OpenH264, relay/relay selected pairs, unshapedPkts0, externalInBytes0, nonempty latency data and finite sender windows/rates. Input timeouts0; all-frame display gaps>200ms total52, marker decode failures728 across all runs. validation.json retains the exact assertions. These totals include the early screenshot-perturbed r1 rows.
- Headed command line saved in headed-viewer-process.json: no headless switch; UA Chrome156/Edg156, pipeline.viewer.headless=false, encoded1930x1072. Headless actual encode1898x1020; dimensions/scheduling differ, so do not attribute the headed improvement solely to display mode [I]. Headed capture P80/P99=136.7/158.3ms; input197.4/219.2ms.
- Endpoint binary hash unchanged at final check. JS checks, targeted freeze verification, and git diff --check passed. Series exited0; no listener8300 remains. No build, commit, endpoint/Chromium edit, or full suite was performed.
