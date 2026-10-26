# Measurements

Two interleaved 60-second marker runs per configuration, plus one 60-second scenario-S scrolling run with the recommended defaults. Loopback, default VPL, headless Edge156, Chromium157 out/dev component+dcheck. No relay or netem. Marker fixture in tab mode; decoded1890x938. rVFC expectedDisplayTime estimates compositor display, not scanout. Latencies in ms P50/P80/P95/P99. FPS is mean of one-second getStats intervals / mean endpoint source fps during measurement (not last interval).

| Run | Actual encoder | Capture->display | Input->decoded flip | Decoded/source FPS | Samples capture/input | Freezes>200ms/marker failures/input timeouts |
|---|---|---|---|---|---|---|
| baseline-1 | PRTS oneVPL D3D11 AVC | 84.27/92.37/150.71/173.87 | 95.70/113.10/170.80/188.90 | 28.10/29.87 | 1644/120 | 0/0/0 |
| baseline-2 | PRTS oneVPL D3D11 AVC | 64.38/71.78/128.07/154.48 | 81.40/92.10/143.70/162.40 | 28.12/29.89 | 1644/119 | 0/0/0 |
| combined-1 | PRTS oneVPL D3D11 AVC | 43.90/48.54/51.50/53.03 | 44.00/49.30/53.40/56.20 | 59.82/60.00 | 2937/120 | 0/0/0 |
| combined-2 | PRTS oneVPL D3D11 AVC | 43.83/48.31/51.45/52.53 | 43.70/52.00/56.20/64.80 | 59.97/59.99 | 2949/120 | 0/0/0 |
| fps60-1 | OpenH264 | 79.21/83.60/86.00/88.09 | 84.80/91.30/99.10/106.10 | 53.02/53.03 | 2899/120 | 0/0/0 |
| fps60-2 | PRTS oneVPL D3D11 AVC | 76.73/81.19/84.93/88.63 | 81.80/89.30/94.70/98.10 | 52.83/53.27 | 2906/119 | 0/0/0 |
| pacer-1 | PRTS oneVPL D3D11 AVC | 60.02/65.76/117.54/147.86 | 69.20/83.50/141.50/167.70 | 28.25/29.86 | 1676/120 | 0/0/0 |
| pacer-2 | PRTS oneVPL D3D11 AVC | 59.52/64.95/116.52/148.66 | 68.90/81.00/135.20/155.10 | 28.20/29.87 | 1677/120 | 0/0/0 |
| scroll-1 | PRTS oneVPL D3D11 AVC | 79.57/84.53/89.24/150.24 | 90.20/102.50/111.70/183.50 | 29.08/29.62 | 1611/110 | 0/123/0 |
| timer-1 | PRTS oneVPL D3D11 AVC | 59.45/64.55/116.34/147.51 | 67.60/81.40/137.00/165.40 | 28.17/29.89 | 1670/119 | 0/0/0 |
| timer-2 | PRTS oneVPL D3D11 AVC | 60.05/66.08/116.48/149.42 | 68.30/80.50/140.80/166.60 | 28.17/29.85 | 1672/120 | 0/0/0 |
| vsync120-1 | PRTS oneVPL D3D11 AVC | 56.01/62.33/124.61/136.70 | 59.60/69.70/127.30/166.20 | 28.32/29.99 | 1676/120 | 0/0/0 |
| vsync120-2 | PRTS oneVPL D3D11 AVC | 47.94/53.90/58.32/64.33 | 57.20/63.70/73.10/85.60 | 29.90/29.99 | 1793/120 | 0/0/0 |
| vsync60-1 | OpenH264 | 47.43/52.10/55.35/56.44 | 53.20/55.70/61.60/71.70 | 59.92/59.91 | 2920/120 | 0/0/0 |
| vsync60-2 | OpenH264 | 47.52/52.14/55.45/56.57 | 48.40/53.00/57.00/71.20 | 59.95/59.95 | 2968/120 | 0/0/0 |

Endpoint stages: median of five-second rolling window P50/P95 during the measurement, not pooled per-frame percentiles; native/I420 stages have different populations.

| Run | Viz copy | Native VPP | Staging Map | ARGB->I420 | Encode | Capture->encoded |
|---|---|---|---|---|---|---|
| baseline-1 | 10.72/12.91 | 9.30/16.30 | 4.21/4.79 | 1.03/1.48 | 5.19/9.14 | 16.61/78.16 |
| baseline-2 | 10.15/11.96 | 9.17/16.22 | 4.12/4.51 | 1.02/1.38 | 5.19/9.16 | 16.80/80.50 |
| combined-1 | 14.54/15.25 | 2.04/3.76 | 4.51/13.35 | 1.50/2.61 | 4.97/5.35 | 7.93/10.10 |
| combined-2 | 14.43/15.11 | 2.03/3.93 | 4.60/9.48 | 1.55/2.56 | 4.98/5.36 | 7.95/10.30 |
| fps60-1 | 10.63/13.14 | - | 4.34/5.68 | 0.84/1.34 | 4.35/5.38 | 10.74/12.61 |
| fps60-2 | 10.62/13.30 | 2.17/3.78 | 4.16/4.56 | 0.99/1.22 | 4.93/5.21 | 7.98/10.01 |
| pacer-1 | 10.06/11.83 | 2.15/3.23 | 3.94/4.38 | 0.78/0.93 | 5.07/8.94 | 8.15/65.72 |
| pacer-2 | 10.19/11.90 | 2.17/3.31 | 4.10/4.71 | 0.81/1.09 | 5.08/8.94 | 8.14/67.07 |
| scroll-1 | 11.62/13.24 | 2.16/3.30 | 4.11/4.60 | 0.87/1.27 | 5.18/9.26 | 8.46/13.13 |
| timer-1 | 10.18/11.82 | 2.15/3.27 | 4.19/4.61 | 0.81/0.99 | 5.08/8.90 | 8.12/66.41 |
| timer-2 | 10.08/11.86 | 2.16/3.31 | 4.06/4.72 | 0.80/1.08 | 5.08/8.96 | 8.16/68.16 |
| vsync120-1 | 8.80/9.47 | 5.78/7.10 | 7.33/8.08 | 1.38/1.79 | 4.81/8.65 | 11.63/80.28 |
| vsync120-2 | 9.06/9.58 | 6.37/7.24 | 7.38/8.96 | 1.49/2.09 | 5.16/5.74 | 12.46/13.54 |
| vsync60-1 | 14.28/15.04 | - | 4.43/6.17 | 0.98/1.42 | 4.72/6.12 | 11.40/13.49 |
| vsync60-2 | 14.20/14.95 | - | 4.46/6.23 | 0.98/1.38 | 4.63/5.88 | 11.34/13.41 |

CPU means in logical cores from process CPU-time deltas. Chrome sums browser/GPU/renderers; viewer sums all running Edge processes, so unrelated Edge work may inflate that column. GPU is max per-engine utilization after summing process utilization per physical engine; mean/max snapshots every~5s, not integrated whole-device busy or a microbenchmark. Cost sampling includes only measurement-window samples. Sender dwell=delta totalPacketSendDelay/delta packetsSent over available five-second outbound RTP snapshots (pacer+send queue, mean per packet).

| Run | CPU endpoint/Chrome/Edge cores | GPU busiest engine mean/max % | Sender dwell ms/packet | Late probe log count |
|---|---|---|---|---|
| baseline-1 | 0.20/0.53/0.41 | 43.73/47.00 | 0.0015 | 0 |
| baseline-2 | 0.20/0.49/0.37 | 43.09/45.00 | 0.0015 | 0 |
| combined-1 | 0.09/0.46/0.69 | 60.27/62.00 | 0.0016 | 0 |
| combined-2 | 0.12/0.47/0.72 | 60.36/63.00 | 0.0015 | 0 |
| fps60-1 | 0.34/0.55/0.73 | 65.18/67.00 | 0.0004 | 0 |
| fps60-2 | 0.08/0.53/0.65 | 54.09/56.00 | 0.0013 | 0 |
| pacer-1 | 0.16/0.51/0.42 | 43.09/46.00 | 0.0013 | 0 |
| pacer-2 | 0.16/0.52/0.43 | 43.58/47.00 | 0.0013 | 0 |
| scroll-1 | 0.08/0.75/0.49 | 47.27/54.00 | 0.0016 | 0 |
| timer-1 | 0.16/0.50/0.43 | 43.33/46.00 | 0.0013 | 0 |
| timer-2 | 0.17/0.49/0.44 | 43.18/45.00 | 0.0013 | 0 |
| vsync120-1 | 0.19/0.82/0.41 | 72.00/75.00 | 0.0013 | 0 |
| vsync120-2 | 0.06/0.81/0.41 | 77.64/80.00 | 0.0015 | 0 |
| vsync60-1 | 0.45/0.54/0.78 | 74.27/76.00 | 0.0004 | 0 |
| vsync60-2 | 0.40/0.54/0.78 | 73.18/74.00 | 0.0004 | 0 |

Pipeline configuration (requested encoder always default VPL; actual fallback is explicit in the first table): hardware ANGLE D3D11 Intel UHD630 driver30.0.100.9805; VPL path is Viz ARGB shared texture via Mojo -> native D3D11 VPP BGRA->NV12 -> oneVPL AVC; fallback path is shared texture -> staging Map -> CPU I420 -> OpenH264. mfxhw64 API1.35 driver libmfxhw64.dll; VBR, LowPower ON, AsyncDepth1, constrained baseline; startup/rate-reinit frames may bridge through I420. MAINTAIN_RESOLUTION, fluid hint, sender playout delay0/0 and receiver target0. Initial bitrate start/min/max and effective driver readback below. Each report contains its full pipeline block including GPU, selected loopback candidate pair, encoder, viewer and display point; config.json preserves exact args.

| Run | Additional switches | Decoded size | CDP Graphite feature | Initial kbps; effective readback |
|---|---|---|---|---|
| baseline-1 | --no-hires-timer | 1890x938 | enabled_on | 1885/30/3608; PRTS VPL GetVideoParam status=0 kbps=2831/2831 fps=30 VBV_KB=24 initial_KB=11 cap_bytes=11795 LDB=16 LP=16 rate_control=2 AsyncDepth=1 level=40 profile=578 |
| baseline-2 | --no-hires-timer | 1890x938 | enabled_on | 1885/30/3608; PRTS VPL GetVideoParam status=0 kbps=3007/3007 fps=30 VBV_KB=26 initial_KB=12 cap_bytes=12529 LDB=16 LP=16 rate_control=2 AsyncDepth=1 level=40 profile=578 |
| combined-1 | --fps=60 --cr-fake-vsync-rate=60 --webrtc-field-trials=WebRTC-Video-Pacing/factor:2.5/WebRTC-Bwe-LimitPacingFactorByUpperLinkCapacityEstimate/Enabled/ | 1890x938 | enabled_on | 2000/2000/12000; PRTS VPL GetVideoParam status=0 kbps=3608/3608 fps=60 VBV_KB=16 initial_KB=7 cap_bytes=7516 LDB=16 LP=16 rate_control=2 AsyncDepth=1 level=42 profile=578 |
| combined-2 | --fps=60 --cr-fake-vsync-rate=60 --webrtc-field-trials=WebRTC-Video-Pacing/factor:2.5/WebRTC-Bwe-LimitPacingFactorByUpperLinkCapacityEstimate/Enabled/ | 1890x938 | enabled_on | 2000/2000/12000; PRTS VPL GetVideoParam status=0 kbps=3608/3608 fps=60 VBV_KB=16 initial_KB=7 cap_bytes=7516 LDB=16 LP=16 rate_control=2 AsyncDepth=1 level=42 profile=578 |
| fps60-1 | --fps=60 | 1890x938 | enabled_on | 1885/30/3608;  |
| fps60-2 | --fps=60 | 1890x938 | enabled_on | 3906/2000/12000; PRTS VPL GetVideoParam status=0 kbps=3608/3608 fps=53 VBV_KB=18 initial_KB=8 cap_bytes=8509 LDB=16 LP=16 rate_control=2 AsyncDepth=1 level=42 profile=578 |
| pacer-1 | --webrtc-field-trials=WebRTC-Video-Pacing/factor:2.5/WebRTC-Bwe-LimitPacingFactorByUpperLinkCapacityEstimate/Enabled/ | 1890x938 | enabled_on | 1885/30/3608; PRTS VPL GetVideoParam status=0 kbps=3007/3007 fps=30 VBV_KB=26 initial_KB=12 cap_bytes=12529 LDB=16 LP=16 rate_control=2 AsyncDepth=1 level=40 profile=578 |
| pacer-2 | --webrtc-field-trials=WebRTC-Video-Pacing/factor:2.5/WebRTC-Bwe-LimitPacingFactorByUpperLinkCapacityEstimate/Enabled/ | 1890x938 | enabled_on | 1885/30/3608; PRTS VPL GetVideoParam status=0 kbps=3007/3007 fps=30 VBV_KB=26 initial_KB=12 cap_bytes=12529 LDB=16 LP=16 rate_control=2 AsyncDepth=1 level=40 profile=578 |
| scroll-1 | (timer enabled; other defaults) | 1890x938 | enabled_on | 1885/30/3608; PRTS VPL GetVideoParam status=0 kbps=3608/3608 fps=30 VBV_KB=31 initial_KB=15 cap_bytes=15033 LDB=16 LP=16 rate_control=2 AsyncDepth=1 level=40 profile=578 |
| timer-1 | (timer enabled; other defaults) | 1890x938 | enabled_on | 1885/30/3608; PRTS VPL GetVideoParam status=0 kbps=3007/3007 fps=30 VBV_KB=26 initial_KB=12 cap_bytes=12529 LDB=16 LP=16 rate_control=2 AsyncDepth=1 level=40 profile=578 |
| timer-2 | (timer enabled; other defaults) | 1890x938 | enabled_on | 3568/30/3608; PRTS VPL GetVideoParam status=0 kbps=3007/3007 fps=30 VBV_KB=26 initial_KB=12 cap_bytes=12529 LDB=16 LP=16 rate_control=2 AsyncDepth=1 level=40 profile=578 |
| vsync120-1 | --cr-fake-vsync-rate=120 | 1890x938 | enabled_on | 1885/30/3608; PRTS VPL GetVideoParam status=0 kbps=3007/3007 fps=30 VBV_KB=26 initial_KB=12 cap_bytes=12529 LDB=16 LP=16 rate_control=2 AsyncDepth=1 level=40 profile=578 |
| vsync120-2 | --cr-fake-vsync-rate=120 | 1890x938 | enabled_on | 1885/30/3608; PRTS VPL GetVideoParam status=0 kbps=3608/3608 fps=30 VBV_KB=31 initial_KB=15 cap_bytes=15033 LDB=16 LP=16 rate_control=2 AsyncDepth=1 level=40 profile=578 |
| vsync60-1 | --fps=60 --cr-fake-vsync-rate=60 | 1890x938 | enabled_on | 1885/30/3608;  |
| vsync60-2 | --fps=60 --cr-fake-vsync-rate=60 | 1890x938 | enabled_on | 1885/30/3608;  |

Capture path counts are sums of one-second delivery logs during the measurement; startup fallback count includes pre-measurement. Rebuild count is log-based and may include driver-requested rate changes.

| Run | Source native/I420 frames | Fallback events total/measurement | Rate rebuild notices during measurement |
|---|---|---|---|
| baseline-1 | 1611/200 | 0/0 | 102 |
| baseline-2 | 1582/200 | 0/0 | 106 |
| combined-1 | 3588/15 | 1/0 | 3 |
| combined-2 | 3601/3 | 1/0 | 1 |
| fps60-1 | 0/3186 | 1/0 | 0 |
| fps60-2 | 3166/33 | 1/0 | 9 |
| pacer-1 | 1621/172 | 0/0 | 104 |
| pacer-2 | 1620/174 | 0/0 | 105 |
| scroll-1 | 1725/54 | 0/0 | 27 |
| timer-1 | 1624/171 | 0/0 | 104 |
| timer-2 | 1610/183 | 0/0 | 108 |
| vsync120-1 | 1604/197 | 0/0 | 102 |
| vsync120-2 | 1792/9 | 0/0 | 5 |
| vsync60-1 | 0/3605 | 1/0 | 0 |
| vsync60-2 | 0/3605 | 1/0 | 0 |
