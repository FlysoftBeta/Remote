# m3-trace research table (ms, P50/P80/P95)
All on one host, TimeTicks/QPC base; [M]=measured, [I]=inferred. Run ids: loop marker=t2, d25j5 marker=t3, loop S=t4, d25j5 S=t5, headed loop marker=t7, headed d25j5 marker=t8

| stage | loop marker | d25j5 marker | loop S | d25j5 S | headed loop marker | headed d25j5 marker | mark |
|---|---|---|---|---|---|---|---|
| 1 viewer input send -> endpoint DataChannel rx | 0.71/0.79/0.91 | 26.25/30.15/33.83 | 0.69/0.81/1.07 | 26.06/31.58/34.99 | 0.71/0.82/1.37 | 25.26/29.05/33.02 | M |
| 2 endpoint rx -> parse -> Mojo Key post | 0.03/0.03/0.04 | 0.03/0.03/0.05 | 0.03/0.04/0.06 | 0.03/0.03/0.04 | 0.03/0.04/0.05 | 0.03/0.03/0.05 | M (F2 only; F3 <0.1) |
| 3 Mojo Key -> renderer keydown handler (inject) | 1.06/1.24/1.5 | 1.19/1.51/1.9 | 1.38/1.68/2.65 | 1.18/1.46/4.2 | 1.45/2.22/2.38 | 1.3/1.52/2.13 | M |
| 4 keydown -> next rAF tick (wait for BeginFrame) | 11.7/12.5/13.3 | 6.8/12.2/15.3 | 12.7/13.3/13.8 | 8.3/12.7/13.6 | 14.9/15.4/15.8 | 10.1/13.6/15.3 | M |
| 5 rAF JS draw | 0.2/0.3/0.4 | 0.3/0.3/0.4 | 0.1/0.2/0.2 | 0.1/0.2/0.2 | 0.3/0.4/0.7 | 0.3/0.4/0.5 | M |
| 6 rAF start -> Viz DrawAndSwap (commit, raster, submit) | 2.25/2.55/3.67 (n/a rest) | see loop marker (t6 trace) | see loop marker (t6 trace) | see loop marker (t6 trace) | see loop marker (t6 trace) | see loop marker (t6 trace) | M (CDP trace, loopback marker only: see t6) |
| 7 Viz DrawAndSwap -> GPU CopyOutput done | 1.2/n/a/n/a (3.45-2.25) | see loop marker (t6 trace) | see loop marker (t6 trace) | see loop marker (t6 trace) | see loop marker (t6 trace) | see loop marker (t6 trace) | M (trace) |
| 8 GPU copy done -> capture_end (copy result delivery, waits for present) | 12.8/13.25/13.63 | see loop marker (t6 trace) | see loop marker (t6 trace) | see loop marker (t6 trace) | see loop marker (t6 trace) | see loop marker (t6 trace) | M (trace) -> product-side wait |
| 9 draw end -> capture_begin (capture phase vs page rAF) | 1.6/1.84/2.26 | 1.58/1.8/2.17 | 33.63/33.93/34.3 | 17.04/17.36/17.68 | 16.84/17.18/17.64 | 16.98/17.26/17.62 | M (run-level phase lock 1.6..34 ms) |
| 10 capture_begin -> capture_end (viz capture) | 14.39/14.78/15.13 | 14.34/14.65/14.99 | 15.43/15.82/16.11 | 15.5/15.75/16.09 | 15.49/15.92/16.21 | 15.53/15.85/16.16 | M |
| 11 capture_end -> endpoint OnFrame (Mojo) | 0.79/0.85/1.08 | 0.82/0.88/0.99 | 0.84/0.93/1.34 | 0.86/0.95/1.14 | 0.81/0.93/1.62 | 0.85/0.94/1.16 | M |
| 12 OnFrame -> copy/VPP submit -> source ready -> Encode() | 0.34/0.41/0.54 | 0.38/0.45/0.55 | 0.37/0.47/0.61 | 0.42/0.51/0.6 | 0.37/0.47/0.69 | 0.39/0.5/0.64 | M (B3 only; B4+B5 ~0.25) |
| 13 encode (Encode -> OnEncodedImage) | 7.34/7.73/14.61 | 7.29/7.71/13.72 | 6.53/10.11/13.51 | 6.75/10.68/15.69 | 6.85/9.9/13.71 | 6.65/9.32/13.7 | M |
| 14 encoded -> first packet at relay (RTP pack + pacer) | 0.36/0.44/0.58 | 0.32/0.41/0.54 | 0.4/0.46/0.62 | 0.34/0.41/0.53 | 0.37/0.47/0.7 | 0.33/0.43/0.59 | M |
| 15 frame packet spread (first->last at relay in) | 0.18/0.22/0.36 | 0.04/0.06/0.1 | 0.76/0.89/1.12 | 0.2/0.34/0.44 | 0.15/0.21/0.39 | 0.04/0.06/0.1 | M |
| 16 network: relay dwell of the LAST packet | 0/0/0 | 31/34.22/37.23 | 0/0/0 | 38.74/44.93/52.53 | 0/0/0 | 30.66/33.83/37.04 | M |
| 17 relay out -> viewer receiveTime (OS + libwebrtc rx) | 0.21/0.26/0.33 | 0.3/0.38/0.49 | 0.09/0.14/0.24 | 0.3/0.42/0.55 | 0.23/0.29/0.42 | 0.25/0.34/0.46 | M |
| 18 receiveTime -> presentationTime (jitter buffer + decode) | 1.4/2.1/4.6 | 1.8/2.2/2.7 | 2.6/3.5/4.7 | 2.9/3.8/4.7 | 1.7/2/3.9 | 2.1/2.5/3.1 | M |
| 19 presentationTime -> expectedDisplayTime (present/display) | 18.5/23/25.6 | 18.2/23.1/25.4 | 19.2/23.7/25.8 | 18/22.8/25.3 | 17.8/22.8/25.1 | 18/22.4/25 | I (compositor estimate; headed also an estimate, same value) |
| T1 capture_end -> expectedDisplayTime | 30.71/35.05/38.79 | 61.34/66.37/70.58 | 32.97/37.28/41.33 | 71.56/79.59/87.1 | 31.07/35.09/39.4 | 61.18/66.54/71.17 | M |
| T2 page draw end -> expectedDisplayTime | 45/49.7/53.6 | 74.56/79.86/84.56 | 79.91/84.71/88.21 | 102.76/110.56/118.56 | 60.26/65.86/69.96 | 89.85/95.85/101.05 | M |
| T3 viewer send -> flip detected (harness i2p) | 54/56.6/64.5 | 109.8/118/127.8 | 90.6/99.2/104.7 | 138.3/146.4/149.6 | 73.8/80.1/89.9 | 126.2/134.3/138.8 | M |
