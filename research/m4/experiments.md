# M4 endpoint experiments

Recommendation:60 fps,8 Mbps ceiling,frame factor 1,one requested VBV frame,shared fence,intra-refresh/FEC off. Keep endpoint defaults unchanged: no clear latency win in two matched scenarios. c12 decoded input P80 still fails in S/G; exact switches, per-run tables, pass counts and trace budget follow.

Status: complete. Endpoint knobs integrated; incremental build and 20 s VPL smoke passed. All 63 required 60 s runs completed;65 accepted reports including smoke/trace. No latency default promoted. Primary acceptance: capture/input -> decoded P80 <100 ms and P99 <200 ms.

All latency cells are P50/P80/P95/P99 in ms. Capture->decoded uses summary.latDecoded and input->decoded uses summary.i2decoded, ending at rVFC presentationTime (decoded frame installed for composition). Capture->display and input->display use expectedDisplayTime and are glass estimates; input->flip is the legacy callback detection metric. The capture starting point remains the page's embedded draw-marker timestamp mapped through RKClock, not endpoint capture_end. Display/scanout is excluded from primary acceptance; no physical display was measured.

Relay counters are shown as queue/loss at report time -> final after optional trace collection. Both include startup; report-time sampling is approximately one second apart. Final totals can grow after the measured window. In particular E2-2-S-r1 has 0 queue drops at report but10 final, all after measurement. Baseline S-r2's6 drops appeared at 58.747 s, S-r3's256 at 49.502-54.820 s, and supplemental Btrace's95 at 54.550 s, all within their60 s windows.

Each required B/E5/E2/E3/E1 configuration uses S and G,3 independent 60 s runs each. Wi-Fi W uses S/V/G,3 repeats. E6 and E13 use 2 repeats as labeled. Per-scenario medians take the median of each run's quantile (not pooled samples). Capture samples are conditional on CRC-valid unique markers; input flips are independent. Requested capture fps is separate from observed mean interval FPS. Encoded byte quantiles and cap overshoots use all encoded PRTS TR X frames within the measurement window, aligned by median logger-wall/source-TimeTicks offset. MaxFrameSize is advisory [V: SDK mfxstructures.h:1642-1649], and overshooting reference pictures are retained. Relay queue/loss drops and max queue delay cover the entire relay session, including startup.

Evidence: C:/src/tmp/m4/experiments/ (raw per-run JSON/CSV, chain.log, launch metadata, host process counts, endpoint/source/viewer traces; RELAY_TRACE also enabled after the initial six B runs). Supplemental Btrace supplies the baseline packet correlation without replacing any required repeats. Each launch sets build processes Idle and records active clang-cl count in host.json and the tables; the active out/rel build is never killed or modified. S/G share the same host and are interleaved within each configuration. Differences below~20 ms require repetition; capture phase locks per run and background build load is a confound.

Existing shared-fence path retained. The rejected capture-phase scheduling candidate was already removed and stays disabled. No commits by this task and no task changes to Chromium/out/rel. New endpoint defaults preserve existing rate/codec choices, with intended bounded VPL readback tolerance. Max-frame factor is Init-only and does not follow GCC rate changes; actual VBV readbacks can collapse requested settings into the same hardware configuration. ULPFEC removes generic NACK while retaining PLI/FIR, so E13 changes recovery policy.

Baseline context: M3 d25j5-c12 S capture->display P80/P99=108.61/121.29 and 124.03/136.09 ms; G 104.22/117.42 and 112.61/132.15 ms. M3 input callback P80/P99 S 145.40/159.20 and 164.70/181.90; G 137.90/155.70 and 147.60/176.80. These are display/callback metrics, not decoded. The M4 capture-phase rejected candidate had shaped decoded capture P80=135-143 ms, potentially build-load confounded; the fence report had decoded capture P80=73.6-81.8 and input115.7-128.2 ms. Cross-session differences do not isolate a knob effect.


## smoke: loopback

Endpoint switches: `(defaults)`. Preset m3-combined; 20 s each, 60 fps request. Percentiles P50/P80/P95/P99. Compiler counts are recorded at each launch after setting build processes Idle. All runs have PRTS_TRACE=1.

| Run | Capture->decoded | Input->decoded | Capture->display glass | Input->display glass | Input->flip callback | clang running/count |
| --- | --- | --- | --- | --- | --- | --- |
| smoke-marker-r1 | 35.5/36.5/37.5/38.8 | 52.5/55.5/66.7/88.1 | 52.4/56.3/59.1/60.2 | 65.0/73.7/82.1/109.4 | 66.1/76.4/78.4/98.5 | yes/16 |
| marker median (1 runs) | 35.5/36.5/37.5/38.8 | 52.5/55.5/66.7/88.1 | 52.4/56.3/59.1/60.2 | 65.0/73.7/82.1/109.4 | 66.1/76.4/78.4/98.5 | see above |

| Run | FPS (mean sampled intervals) | Encoded bytes P50/P99 | VPL cap bytes / violations | Queue/loss at report -> final; max queue ms | Backend / mixed |
| --- | --- | --- | --- | --- | --- |
| smoke-marker-r1 | 60.0 | 2337/6387 | 25000/0 | 0/0 -> 0/0; 0 | PRTS oneVPL D3D11 AVC/false |
| marker median (1) | 60.0 | 2337/6387 | 25000/0 | 0/0 -> 0/0; 0 | VPL; see rows |

Actual VPL last readbacks: VBV_KB=51, initial_KB=25, cap_bytes=25000, rate_control=2, intra_refresh=0, slices=1. Requested and actual settings, rounding logs and negotiation are retained in every report pipeline.

## B: d25j5-c12

Endpoint switches: `(defaults)`. Preset m3-combined; 60 s each, 60 fps request. Percentiles P50/P80/P95/P99. Compiler counts are recorded at each launch after setting build processes Idle. All runs have PRTS_TRACE=1.

| Run | Capture->decoded | Input->decoded | Capture->display glass | Input->display glass | Input->flip callback | clang running/count |
| --- | --- | --- | --- | --- | --- | --- |
| B-S-r1 | 87.2/93.2/102.1/115.9 | 143.6/154.1/173.0/183.2 | 104.1/111.7/120.9/131.2 | 159.8/173.7/188.5/198.2 | 157.8/170.2/188.1/195.2 | yes/16 |
| B-G-r1 | 85.9/93.9/98.7/103.1 | 138.3/150.5/164.7/191.8 | 103.5/111.4/117.7/122.9 | 155.1/163.5/187.5/205.9 | 153.5/162.9/175.9/205.2 | yes/16 |
| B-S-r2 | 88.4/96.0/104.6/123.5 | 139.4/159.4/173.9/180.7 | 105.9/114.0/123.0/141.6 | 155.0/177.7/192.3/198.1 | 152.2/172.0/187.2/193.0 | yes/16 |
| B-G-r2 | 88.6/96.5/103.2/115.2 | 143.7/154.5/170.8/184.1 | 106.0/114.1/121.7/132.1 | 163.3/172.7/186.5/200.8 | 157.8/168.9/185.3/201.8 | yes/16 |
| B-S-r3 | 76.2/84.0/97.8/158.7 | 130.3/148.1/184.0/207.6 | 93.1/101.5/115.5/179.7 | 149.3/167.1/205.0/224.4 | 147.0/159.1/197.6/220.4 | yes/16 |
| B-G-r3 | 84.3/92.6/97.7/102.5 | 139.8/151.4/161.6/177.7 | 102.1/110.4/116.7/123.0 | 157.8/166.3/177.6/194.3 | 153.6/163.5/172.4/190.4 | yes/16 |
| S median (3 runs) | 87.2/93.2/102.1/123.5 | 139.4/154.1/173.9/183.2 | 104.1/111.7/120.9/141.6 | 155.0/173.7/192.3/198.2 | 152.2/170.2/188.1/195.2 | see above |
| G median (3 runs) | 85.9/93.9/98.7/103.1 | 139.8/151.4/164.7/184.1 | 103.5/111.4/117.7/123.0 | 157.8/166.3/186.5/200.8 | 153.6/163.5/175.9/201.8 | see above |

| Run | FPS (mean sampled intervals) | Encoded bytes P50/P99 | VPL cap bytes / violations | Queue/loss at report -> final; max queue ms | Backend / mixed |
| --- | --- | --- | --- | --- | --- |
| B-S-r1 | 56.0 | 16122/27760 | 25000/142 | 0/0 -> 0/0; 47.19 | PRTS oneVPL D3D11 AVC/false |
| B-G-r1 | 57.4 | 8996/21090 | 25000/0 | 0/0 -> 0/0; 20.38 | PRTS oneVPL D3D11 AVC/false |
| B-S-r2 | 56.1 | 17844/27614 | 25000/171 | 6/0 -> 6/0; 48.94 | PRTS oneVPL D3D11 AVC/false |
| B-G-r2 | 57.6 | 13500/19487 | 25000/0 | 0/0 -> 0/0; 37.23 | PRTS oneVPL D3D11 AVC/false |
| B-S-r3 | 56.1 | 18720/28357 | 25000/205 | 256/0 -> 256/0; 49.59 | PRTS oneVPL D3D11 AVC/false |
| B-G-r3 | 58.0 | 9165/20565 | 25000/0 | 0/0 -> 0/0; 22.46 | PRTS oneVPL D3D11 AVC/false |
| S median (3) | 56.1 | 17844/27760 | 25000/171 | 6/0 -> 6/0; 48.94 | VPL; see rows |
| G median (3) | 57.6 | 9165/20565 | 25000/0 | 0/0 -> 0/0; 22.46 | VPL; see rows |

Actual VPL last readbacks: VBV_KB=51, initial_KB=25, cap_bytes=25000, rate_control=2, intra_refresh=0, slices=1. Requested and actual settings, rounding logs and negotiation are retained in every report pipeline.

## Btrace: d25j5-c12

Endpoint switches: `(defaults)`. Preset m3-combined; 60 s each, 60 fps request. Percentiles P50/P80/P95/P99. Compiler counts are recorded at each launch after setting build processes Idle. All runs have PRTS_TRACE=1.

| Run | Capture->decoded | Input->decoded | Capture->display glass | Input->display glass | Input->flip callback | clang running/count |
| --- | --- | --- | --- | --- | --- | --- |
| Btrace-S-r1 | 91.1/99.0/112.9/179.3 | 141.4/156.7/177.1/218.4 | 108.2/116.9/130.6/196.6 | 160.6/175.5/198.4/229.9 | 157.3/171.2/189.2/231.3 | yes/16 |
| S median (1 runs) | 91.1/99.0/112.9/179.3 | 141.4/156.7/177.1/218.4 | 108.2/116.9/130.6/196.6 | 160.6/175.5/198.4/229.9 | 157.3/171.2/189.2/231.3 | see above |

| Run | FPS (mean sampled intervals) | Encoded bytes P50/P99 | VPL cap bytes / violations | Queue/loss at report -> final; max queue ms | Backend / mixed |
| --- | --- | --- | --- | --- | --- |
| Btrace-S-r1 | 56.1 | 18882/27683 | 25000/171 | 95/0 -> 95/0; 49.77 | PRTS oneVPL D3D11 AVC/false |
| S median (1) | 56.1 | 18882/27683 | 25000/171 | 95/0 -> 95/0; 49.77 | VPL; see rows |

Actual VPL last readbacks: VBV_KB=51, initial_KB=25, cap_bytes=25000, rate_control=2, intra_refresh=0, slices=1. Requested and actual settings, rounding logs and negotiation are retained in every report pipeline.

## E5-10: d25j5-c12

Endpoint switches: `--max-kbps=10000`. Preset m3-combined; 60 s each, 60 fps request. Percentiles P50/P80/P95/P99. Compiler counts are recorded at each launch after setting build processes Idle. All runs have PRTS_TRACE=1.

| Run | Capture->decoded | Input->decoded | Capture->display glass | Input->display glass | Input->flip callback | clang running/count |
| --- | --- | --- | --- | --- | --- | --- |
| E5-10-S-r1 | 73.7/79.3/83.6/98.9 | 124.7/134.3/146.5/166.8 | 90.5/96.8/103.4/117.0 | 143.3/154.1/167.7/180.4 | 138.9/147.6/158.8/184.4 | yes/16 |
| E5-10-G-r1 | 84.2/92.7/97.9/103.0 | 140.0/148.2/160.0/173.7 | 101.7/110.4/116.5/124.6 | 156.9/167.2/180.7/189.0 | 153.6/163.7/176.0/187.4 | yes/16 |
| E5-10-S-r2 | 88.0/92.8/100.2/111.6 | 138.7/152.8/169.0/183.4 | 104.3/111.2/118.6/131.6 | 157.0/168.7/184.7/198.0 | 155.4/164.2/188.6/200.5 | yes/16 |
| E5-10-G-r2 | 83.6/91.1/98.3/107.1 | 137.6/149.2/168.8/186.3 | 101.0/108.7/116.8/125.5 | 156.9/167.0/184.1/198.1 | 152.5/162.0/182.4/200.2 | yes/15 |
| E5-10-S-r3 | 73.3/79.7/88.5/98.9 | 129.5/136.8/149.8/153.0 | 90.1/98.1/106.4/119.0 | 144.8/158.4/167.0/175.5 | 140.5/152.3/164.0/169.8 | yes/16 |
| E5-10-G-r3 | 83.2/91.5/95.7/101.2 | 137.1/146.4/156.5/163.4 | 100.9/108.7/115.0/121.0 | 153.9/164.9/175.4/179.3 | 151.4/158.6/168.8/176.0 | yes/16 |
| S median (3 runs) | 73.7/79.7/88.5/98.9 | 129.5/136.8/149.8/166.8 | 90.5/98.1/106.4/119.0 | 144.8/158.4/167.7/180.4 | 140.5/152.3/164.0/184.4 | see above |
| G median (3 runs) | 83.6/91.5/97.9/103.0 | 137.6/148.2/160.0/173.7 | 101.0/108.7/116.5/124.6 | 156.9/167.0/180.7/189.0 | 152.5/162.0/176.0/187.4 | see above |

| Run | FPS (mean sampled intervals) | Encoded bytes P50/P99 | VPL cap bytes / violations | Queue/loss at report -> final; max queue ms | Backend / mixed |
| --- | --- | --- | --- | --- | --- |
| E5-10-S-r1 | 56.0 | 15589/22841 | 20833/168 | 0/0 -> 0/0; 46.06 | PRTS oneVPL D3D11 AVC/false |
| E5-10-G-r1 | 57.5 | 9046/20523 | 20833/1 | 0/0 -> 0/0; 29.69 | PRTS oneVPL D3D11 AVC/false |
| E5-10-S-r2 | 56.1 | 16027/23660 | 20833/205 | 0/0 -> 0/0; 48.37 | PRTS oneVPL D3D11 AVC/false |
| E5-10-G-r2 | 57.6 | 9162/20549 | 20833/1 | 0/0 -> 0/0; 30.14 | PRTS oneVPL D3D11 AVC/false |
| E5-10-S-r3 | 56.0 | 15524/23110 | 20833/188 | 0/0 -> 0/0; 42.37 | PRTS oneVPL D3D11 AVC/false |
| E5-10-G-r3 | 57.4 | 9010/20430 | 20833/1 | 0/0 -> 0/0; 29.54 | PRTS oneVPL D3D11 AVC/false |
| S median (3) | 56.0 | 15589/23110 | 20833/188 | 0/0 -> 0/0; 46.06 | VPL; see rows |
| G median (3) | 57.5 | 9046/20523 | 20833/1 | 0/0 -> 0/0; 29.69 | VPL; see rows |

Actual VPL last readbacks: VBV_KB=42, initial_KB=20, cap_bytes=20833, rate_control=2, intra_refresh=0, slices=1. Requested and actual settings, rounding logs and negotiation are retained in every report pipeline.

## E5-8: d25j5-c12

Endpoint switches: `--max-kbps=8000`. Preset m3-combined; 60 s each, 60 fps request. Percentiles P50/P80/P95/P99. Compiler counts are recorded at each launch after setting build processes Idle. All runs have PRTS_TRACE=1.

| Run | Capture->decoded | Input->decoded | Capture->display glass | Input->display glass | Input->flip callback | clang running/count |
| --- | --- | --- | --- | --- | --- | --- |
| E5-8-S-r1 | 89.4/96.2/100.4/104.6 | 136.0/148.1/161.6/173.2 | 105.9/113.4/119.1/125.3 | 152.6/162.6/183.0/192.2 | 150.2/162.0/175.1/185.7 | yes/16 |
| E5-8-G-r1 | 84.1/91.4/96.0/102.4 | 139.8/152.5/162.9/176.6 | 101.5/109.1/115.4/121.0 | 158.0/169.8/183.9/195.7 | 155.6/164.9/180.7/188.6 | yes/16 |
| E5-8-S-r2 | 72.3/78.8/82.5/87.7 | 122.2/134.0/150.0/162.0 | 89.0/96.2/102.3/107.6 | 140.4/151.7/167.7/176.0 | 137.7/147.5/161.7/170.8 | yes/16 |
| E5-8-G-r2 | 85.2/93.3/98.0/102.5 | 136.2/148.7/165.8/173.5 | 103.2/110.8/116.8/122.9 | 153.2/166.4/181.0/185.5 | 151.8/161.9/178.1/187.8 | yes/16 |
| E5-8-S-r3 | 71.8/80.1/83.9/88.0 | 117.9/131.6/138.0/148.3 | 89.2/97.0/102.9/108.3 | 134.5/147.1/157.3/161.1 | 135.0/145.0/153.7/160.7 | yes/15 |
| E5-8-G-r3 | 84.3/92.4/96.3/100.9 | 139.4/146.9/162.9/178.4 | 101.9/109.8/115.4/120.9 | 156.5/165.2/185.3/193.6 | 154.7/160.8/176.9/191.1 | yes/16 |
| S median (3 runs) | 72.3/80.1/83.9/88.0 | 122.2/134.0/150.0/162.0 | 89.2/97.0/102.9/108.3 | 140.4/151.7/167.7/176.0 | 137.7/147.5/161.7/170.8 | see above |
| G median (3 runs) | 84.3/92.4/96.3/102.4 | 139.4/148.7/162.9/176.6 | 101.9/109.8/115.4/121.0 | 156.5/166.4/183.9/193.6 | 154.7/161.9/178.1/188.6 | see above |

| Run | FPS (mean sampled intervals) | Encoded bytes P50/P99 | VPL cap bytes / violations | Queue/loss at report -> final; max queue ms | Backend / mixed |
| --- | --- | --- | --- | --- | --- |
| E5-8-S-r1 | 56.0 | 12110/17733 | 16666/155 | 0/0 -> 0/0; 36.44 | PRTS oneVPL D3D11 AVC/false |
| E5-8-G-r1 | 57.5 | 9716/15972 | 16666/3 | 0/0 -> 0/0; 27.58 | PRTS oneVPL D3D11 AVC/false |
| E5-8-S-r2 | 56.1 | 12040/18097 | 16666/151 | 0/0 -> 0/0; 38.42 | PRTS oneVPL D3D11 AVC/false |
| E5-8-G-r2 | 57.3 | 9652/15695 | 16666/5 | 0/0 -> 0/0; 22.18 | PRTS oneVPL D3D11 AVC/false |
| E5-8-S-r3 | 56.1 | 12581/17903 | 16666/162 | 0/0 -> 0/0; 45.81 | PRTS oneVPL D3D11 AVC/false |
| E5-8-G-r3 | 57.4 | 9721/15927 | 16666/4 | 0/0 -> 0/0; 25.72 | PRTS oneVPL D3D11 AVC/false |
| S median (3) | 56.1 | 12110/17903 | 16666/155 | 0/0 -> 0/0; 38.42 | VPL; see rows |
| G median (3) | 57.4 | 9716/15927 | 16666/4 | 0/0 -> 0/0; 25.72 | VPL; see rows |

Actual VPL last readbacks: VBV_KB=34, initial_KB=16, cap_bytes=16666, rate_control=2, intra_refresh=0, slices=1. Requested and actual settings, rounding logs and negotiation are retained in every report pipeline.

## E2-1: d25j5-c12

Endpoint switches: `--max-kbps=8000 --vpl-max-frame-factor=1.0`. Preset m3-combined; 60 s each, 60 fps request. Percentiles P50/P80/P95/P99. Compiler counts are recorded at each launch after setting build processes Idle. All runs have PRTS_TRACE=1.

| Run | Capture->decoded | Input->decoded | Capture->display glass | Input->display glass | Input->flip callback | clang running/count |
| --- | --- | --- | --- | --- | --- | --- |
| E2-1-S-r1 | 88.0/93.9/97.1/101.0 | 139.0/149.4/160.5/165.9 | 104.6/111.3/116.7/121.7 | 155.9/165.7/180.9/186.0 | 151.6/160.8/170.5/174.5 | yes/16 |
| E2-1-G-r1 | 87.2/96.3/101.4/104.9 | 137.2/146.7/167.6/171.3 | 104.6/113.3/119.5/125.1 | 153.3/165.2/184.0/195.2 | 151.9/161.0/179.8/185.9 | yes/16 |
| E2-1-S-r2 | 72.4/79.6/83.6/89.9 | 120.5/134.7/142.2/148.1 | 89.5/97.1/102.7/111.0 | 139.6/150.7/161.4/166.4 | 135.8/146.8/156.0/164.2 | yes/16 |
| E2-1-G-r2 | 83.6/91.8/96.4/101.0 | 138.6/147.5/157.0/168.7 | 100.7/109.0/115.1/120.4 | 156.6/165.1/176.0/189.2 | 152.6/161.4/170.1/182.5 | yes/16 |
| E2-1-S-r3 | 85.0/92.3/97.1/105.2 | 134.4/144.0/157.3/172.2 | 102.5/109.9/116.7/125.0 | 151.5/164.1/176.8/195.0 | 149.2/160.7/171.1/184.9 | yes/16 |
| E2-1-G-r3 | 84.2/92.4/96.1/100.3 | 138.6/150.4/160.0/164.3 | 101.9/109.6/115.7/120.5 | 157.6/165.8/181.0/188.0 | 153.8/163.6/172.5/181.0 | yes/15 |
| S median (3 runs) | 85.0/92.3/97.1/101.0 | 134.4/144.0/157.3/165.9 | 102.5/109.9/116.7/121.7 | 151.5/164.1/176.8/186.0 | 149.2/160.7/170.5/174.5 | see above |
| G median (3 runs) | 84.2/92.4/96.4/101.0 | 138.6/147.5/160.0/168.7 | 101.9/109.6/115.7/120.5 | 156.6/165.2/181.0/189.2 | 152.6/161.4/172.5/182.5 | see above |

| Run | FPS (mean sampled intervals) | Encoded bytes P50/P99 | VPL cap bytes / violations | Queue/loss at report -> final; max queue ms | Backend / mixed |
| --- | --- | --- | --- | --- | --- |
| E2-1-S-r1 | 56.5 | 12159/18146 | 16666/154 | 0/0 -> 0/0; 27.6 | PRTS oneVPL D3D11 AVC/false |
| E2-1-G-r1 | 57.5 | 9753/16004 | 16666/5 | 0/0 -> 0/0; 27.13 | PRTS oneVPL D3D11 AVC/false |
| E2-1-S-r2 | 56.3 | 12476/18047 | 16666/170 | 0/0 -> 0/0; 37.79 | PRTS oneVPL D3D11 AVC/false |
| E2-1-G-r2 | 57.4 | 9761/16055 | 16666/4 | 0/0 -> 0/0; 26.4 | PRTS oneVPL D3D11 AVC/false |
| E2-1-S-r3 | 56.2 | 12825/18319 | 16666/185 | 0/0 -> 0/0; 33.97 | PRTS oneVPL D3D11 AVC/false |
| E2-1-G-r3 | 57.2 | 9736/15904 | 16666/4 | 0/0 -> 0/0; 19.83 | PRTS oneVPL D3D11 AVC/false |
| S median (3) | 56.3 | 12476/18146 | 16666/170 | 0/0 -> 0/0; 33.97 | VPL; see rows |
| G median (3) | 57.4 | 9753/16004 | 16666/4 | 0/0 -> 0/0; 26.4 | VPL; see rows |

Actual VPL last readbacks: VBV_KB=34, initial_KB=16, cap_bytes=16666, rate_control=2, intra_refresh=0, slices=1. Requested and actual settings, rounding logs and negotiation are retained in every report pipeline.

## E2-2: d25j5-c12

Endpoint switches: `--max-kbps=8000 --vpl-max-frame-factor=2.0`. Preset m3-combined; 60 s each, 60 fps request. Percentiles P50/P80/P95/P99. Compiler counts are recorded at each launch after setting build processes Idle. All runs have PRTS_TRACE=1.

| Run | Capture->decoded | Input->decoded | Capture->display glass | Input->display glass | Input->flip callback | clang running/count |
| --- | --- | --- | --- | --- | --- | --- |
| E2-2-S-r1 | 73.6/77.8/81.9/91.0 | 123.7/134.5/147.4/152.0 | 89.6/95.4/101.0/109.7 | 141.1/151.4/165.8/168.0 | 136.3/147.1/161.3/172.8 | yes/16 |
| E2-2-G-r1 | 86.7/94.7/99.6/105.3 | 141.1/150.3/163.5/184.0 | 103.9/111.9/118.1/125.3 | 158.3/168.5/180.5/198.7 | 154.3/163.5/177.3/195.8 | yes/16 |
| E2-2-S-r2 | 72.5/78.6/82.2/91.5 | 120.4/134.0/149.5/157.4 | 89.0/96.0/101.9/109.6 | 137.7/151.8/168.8/175.1 | 135.1/147.5/167.1/171.9 | yes/16 |
| E2-2-G-r2 | 83.5/90.9/98.2/102.7 | 134.2/151.9/166.5/175.9 | 101.1/108.8/116.0/121.9 | 153.5/168.3/182.9/195.9 | 148.4/164.9/179.9/188.8 | yes/16 |
| E2-2-S-r3 | 87.0/93.7/98.1/103.4 | 140.5/153.4/168.2/176.1 | 103.6/111.0/116.8/124.7 | 156.3/171.7/186.0/194.6 | 154.6/166.2/180.9/189.0 | yes/16 |
| E2-2-G-r3 | 86.6/94.3/99.3/104.9 | 141.7/149.0/169.5/176.6 | 104.0/112.1/118.3/124.4 | 158.8/166.7/185.1/194.3 | 154.1/161.7/182.8/188.5 | yes/16 |
| S median (3 runs) | 73.6/78.6/82.2/91.5 | 123.7/134.5/149.5/157.4 | 89.6/96.0/101.9/109.7 | 141.1/151.8/168.8/175.1 | 136.3/147.5/167.1/172.8 | see above |
| G median (3 runs) | 86.6/94.3/99.3/104.9 | 141.1/150.3/166.5/176.6 | 103.9/111.9/118.1/124.4 | 158.3/168.3/182.9/195.9 | 154.1/163.5/179.9/188.8 | see above |

| Run | FPS (mean sampled intervals) | Encoded bytes P50/P99 | VPL cap bytes / violations | Queue/loss at report -> final; max queue ms | Backend / mixed |
| --- | --- | --- | --- | --- | --- |
| E2-2-S-r1 | 56.1 | 13034/21644 | 33333/0 | 0/0 -> 10/0; 48.99 | PRTS oneVPL D3D11 AVC/false |
| E2-2-G-r1 | 57.5 | 9029/20878 | 33333/0 | 0/0 -> 0/0; 29.74 | PRTS oneVPL D3D11 AVC/false |
| E2-2-S-r2 | 56.1 | 12321/20974 | 33333/0 | 0/0 -> 0/0; 46.5 | PRTS oneVPL D3D11 AVC/false |
| E2-2-G-r2 | 57.3 | 9004/20580 | 33333/0 | 0/0 -> 0/0; 30.92 | PRTS oneVPL D3D11 AVC/false |
| E2-2-S-r3 | 56.0 | 12654/20933 | 33333/0 | 0/0 -> 0/0; 42.83 | PRTS oneVPL D3D11 AVC/false |
| E2-2-G-r3 | 57.4 | 9009/20882 | 33333/0 | 0/0 -> 0/0; 26.42 | PRTS oneVPL D3D11 AVC/false |
| S median (3) | 56.1 | 12654/20974 | 33333/0 | 0/0 -> 0/0; 46.5 | VPL; see rows |
| G median (3) | 57.4 | 9009/20878 | 33333/0 | 0/0 -> 0/0; 29.74 | VPL; see rows |

Actual VPL last readbacks: VBV_KB=34, initial_KB=16, cap_bytes=33333, rate_control=2, intra_refresh=0, slices=1. Requested and actual settings, rounding logs and negotiation are retained in every report pipeline.

## E3-2: d25j5-c12

Endpoint switches: `--max-kbps=8000 --vpl-max-frame-factor=1.0 --vpl-vbv-frames=2`. Preset m3-combined; 60 s each, 60 fps request. Percentiles P50/P80/P95/P99. Compiler counts are recorded at each launch after setting build processes Idle. All runs have PRTS_TRACE=1.

| Run | Capture->decoded | Input->decoded | Capture->display glass | Input->display glass | Input->flip callback | clang running/count |
| --- | --- | --- | --- | --- | --- | --- |
| E3-2-S-r1 | 71.9/78.7/82.1/90.9 | 128.0/137.4/149.1/167.1 | 88.9/96.2/102.6/109.4 | 144.2/155.6/169.0/182.5 | 140.3/151.5/160.8/179.8 | yes/15 |
| E3-2-G-r1 | 90.3/98.3/103.0/107.2 | 134.8/144.2/161.6/176.0 | 107.6/115.7/121.6/127.7 | 152.3/163.4/182.6/190.9 | 149.3/158.7/175.4/187.4 | yes/16 |
| E3-2-S-r2 | 86.3/91.8/97.0/102.0 | 134.6/144.5/152.7/154.4 | 103.1/109.5/116.3/124.1 | 152.1/164.2/174.9/178.1 | 148.4/160.8/164.4/166.3 | yes/16 |
| E3-2-G-r2 | 85.8/93.7/98.5/103.8 | 140.4/149.8/168.6/172.5 | 103.6/111.2/116.9/123.3 | 156.6/166.0/185.3/190.1 | 154.5/162.3/178.1/184.4 | yes/16 |
| E3-2-S-r3 | 70.7/77.4/80.9/85.8 | 121.9/135.2/150.5/157.4 | 87.8/94.9/100.2/105.9 | 139.4/153.6/165.8/172.9 | 136.4/146.7/161.5/176.8 | yes/16 |
| E3-2-G-r3 | 86.6/94.0/103.7/107.9 | 136.8/143.9/153.8/170.3 | 103.2/112.3/121.2/129.0 | 152.6/164.5/170.5/184.6 | 151.1/158.5/167.7/182.7 | yes/15 |
| S median (3 runs) | 71.9/78.7/82.1/90.9 | 128.0/137.4/150.5/157.4 | 88.9/96.2/102.6/109.4 | 144.2/155.6/169.0/178.1 | 140.3/151.5/161.5/176.8 | see above |
| G median (3 runs) | 86.6/94.0/103.0/107.2 | 136.8/144.2/161.6/172.5 | 103.6/112.3/121.2/127.7 | 152.6/164.5/182.6/190.1 | 151.1/158.7/175.4/184.4 | see above |

| Run | FPS (mean sampled intervals) | Encoded bytes P50/P99 | VPL cap bytes / violations | Queue/loss at report -> final; max queue ms | Backend / mixed |
| --- | --- | --- | --- | --- | --- |
| E3-2-S-r1 | 56.0 | 12770/18119 | 16666/192 | 0/0 -> 0/0; 46.44 | PRTS oneVPL D3D11 AVC/false |
| E3-2-G-r1 | 57.5 | 9712/15957 | 16666/3 | 0/0 -> 0/0; 22.96 | PRTS oneVPL D3D11 AVC/false |
| E3-2-S-r2 | 55.8 | 11967/17711 | 16666/134 | 0/0 -> 0/0; 43.36 | PRTS oneVPL D3D11 AVC/false |
| E3-2-G-r2 | 57.5 | 9722/16013 | 16666/3 | 0/0 -> 0/0; 20.24 | PRTS oneVPL D3D11 AVC/false |
| E3-2-S-r3 | 56.0 | 11201/17444 | 16666/115 | 0/0 -> 0/0; 32.89 | PRTS oneVPL D3D11 AVC/false |
| E3-2-G-r3 | 57.3 | 9720/15817 | 16666/3 | 0/0 -> 0/0; 31.36 | PRTS oneVPL D3D11 AVC/false |
| S median (3) | 56.0 | 11967/17711 | 16666/134 | 0/0 -> 0/0; 43.36 | VPL; see rows |
| G median (3) | 57.5 | 9720/15957 | 16666/3 | 0/0 -> 0/0; 22.96 | VPL; see rows |

Actual VPL last readbacks: VBV_KB=34, initial_KB=17, cap_bytes=16666, rate_control=2, intra_refresh=0, slices=1. Requested and actual settings, rounding logs and negotiation are retained in every report pipeline.

## E3-4: d25j5-c12

Endpoint switches: `--max-kbps=8000 --vpl-max-frame-factor=1.0 --vpl-vbv-frames=4`. Preset m3-combined; 60 s each, 60 fps request. Percentiles P50/P80/P95/P99. Compiler counts are recorded at each launch after setting build processes Idle. All runs have PRTS_TRACE=1.

| Run | Capture->decoded | Input->decoded | Capture->display glass | Input->display glass | Input->flip callback | clang running/count |
| --- | --- | --- | --- | --- | --- | --- |
| E3-4-S-r1 | 87.7/94.5/98.0/106.2 | 133.1/148.5/165.7/176.0 | 104.4/111.7/117.7/123.5 | 151.3/167.5/186.1/199.5 | 147.6/162.0/177.6/187.1 | yes/16 |
| E3-4-G-r1 | 86.8/93.1/99.6/106.5 | 137.0/146.9/169.3/178.1 | 103.9/111.1/118.0/127.4 | 156.0/165.5/189.8/196.8 | 152.3/159.4/181.7/191.4 | yes/16 |
| E3-4-S-r2 | 72.9/77.6/81.0/91.8 | 121.0/134.1/145.7/157.6 | 89.4/95.6/101.6/108.2 | 137.5/150.9/163.0/175.1 | 134.7/146.5/155.8/169.2 | yes/16 |
| E3-4-G-r2 | 82.3/91.3/96.7/109.6 | 140.7/150.2/163.3/178.1 | 99.5/108.1/115.1/130.1 | 156.4/167.0/180.3/193.8 | 153.4/162.8/177.4/191.0 | yes/15 |
| E3-4-S-r3 | 72.1/78.6/83.4/89.7 | 123.3/135.3/148.2/163.2 | 89.2/96.4/102.5/110.7 | 140.1/152.3/167.4/178.3 | 136.4/149.3/161.3/176.6 | yes/16 |
| E3-4-G-r3 | 84.2/90.3/96.7/100.5 | 137.4/143.7/156.3/170.9 | 100.9/108.0/115.3/120.9 | 156.0/161.4/172.2/181.9 | 152.7/157.0/170.1/187.3 | yes/16 |
| S median (3 runs) | 72.9/78.6/83.4/91.8 | 123.3/135.3/148.2/163.2 | 89.4/96.4/102.5/110.7 | 140.1/152.3/167.4/178.3 | 136.4/149.3/161.3/176.6 | see above |
| G median (3 runs) | 84.2/91.3/96.7/106.5 | 137.4/146.9/163.3/178.1 | 100.9/108.1/115.3/127.4 | 156.0/165.5/180.3/193.8 | 152.7/159.4/177.4/191.0 | see above |

| Run | FPS (mean sampled intervals) | Encoded bytes P50/P99 | VPL cap bytes / violations | Queue/loss at report -> final; max queue ms | Backend / mixed |
| --- | --- | --- | --- | --- | --- |
| E3-4-S-r1 | 56.0 | 11473/17865 | 16666/144 | 0/0 -> 0/0; 42.57 | PRTS oneVPL D3D11 AVC/false |
| E3-4-G-r1 | 57.4 | 9691/15951 | 16666/4 | 0/0 -> 0/0; 25.13 | PRTS oneVPL D3D11 AVC/false |
| E3-4-S-r2 | 55.8 | 12216/18051 | 16666/161 | 0/0 -> 0/0; 43.47 | PRTS oneVPL D3D11 AVC/false |
| E3-4-G-r2 | 57.5 | 9780/15546 | 16666/3 | 0/0 -> 0/0; 34.37 | PRTS oneVPL D3D11 AVC/false |
| E3-4-S-r3 | 56.0 | 12300/17808 | 16666/153 | 0/0 -> 0/0; 44.77 | PRTS oneVPL D3D11 AVC/false |
| E3-4-G-r3 | 57.4 | 9721/15900 | 16666/3 | 0/0 -> 0/0; 26.3 | PRTS oneVPL D3D11 AVC/false |
| S median (3) | 56.0 | 12216/17865 | 16666/153 | 0/0 -> 0/0; 43.47 | VPL; see rows |
| G median (3) | 57.4 | 9721/15900 | 16666/3 | 0/0 -> 0/0; 26.3 | VPL; see rows |

Actual VPL last readbacks: VBV_KB=67, initial_KB=33, cap_bytes=16666, rate_control=2, intra_refresh=0, slices=1. Requested and actual settings, rounding logs and negotiation are retained in every report pipeline.

## E1-30: d25j5-c12

Endpoint switches: `--max-kbps=8000 --vpl-max-frame-factor=1.0 --vpl-vbv-frames=1`. Preset m3-combined; 60 s each, 30 fps request. Percentiles P50/P80/P95/P99. Compiler counts are recorded at each launch after setting build processes Idle. All runs have PRTS_TRACE=1.

| Run | Capture->decoded | Input->decoded | Capture->display glass | Input->display glass | Input->flip callback | clang running/count |
| --- | --- | --- | --- | --- | --- | --- |
| E1-30-S-r1 | 93.8/100.3/106.7/116.4 | 141.2/148.1/164.2/183.6 | 111.5/119.2/127.1/135.1 | 158.7/165.4/178.8/199.1 | 152.9/158.6/174.1/193.3 | yes/16 |
| E1-30-G-r1 | 116.2/121.5/126.4/137.1 | 174.0/178.0/180.5/181.8 | 133.3/140.1/146.7/153.8 | 190.5/197.2/200.9/203.5 | 184.4/186.9/190.6/197.5 | yes/16 |
| E1-30-S-r2 | 109.7/115.8/121.9/129.9 | 162.5/170.8/174.8/179.9 | 126.6/134.5/141.5/149.3 | 179.3/187.9/193.9/203.7 | 175.0/182.0/188.2/197.5 | yes/16 |
| E1-30-G-r2 | 115.3/119.6/125.2/138.5 | 174.4/178.4/179.9/182.9 | 132.6/138.8/145.6/159.9 | 190.7/195.8/200.9/205.3 | 184.3/186.7/192.1/197.2 | yes/16 |
| E1-30-S-r3 | 87.3/94.6/102.4/108.4 | 143.3/152.1/158.8/169.4 | 105.1/113.3/122.0/129.8 | 161.5/171.6/178.9/191.1 | 156.4/164.8/170.7/182.6 | yes/16 |
| E1-30-G-r3 | 115.5/120.0/125.0/137.4 | 176.5/180.8/182.5/183.7 | 133.4/139.9/146.8/155.3 | 193.2/199.7/203.9/206.5 | 186.2/189.6/192.3/197.3 | yes/16 |
| S median (3 runs) | 93.8/100.3/106.7/116.4 | 143.3/152.1/164.2/179.9 | 111.5/119.2/127.1/135.1 | 161.5/171.6/178.9/199.1 | 156.4/164.8/174.1/193.3 | see above |
| G median (3 runs) | 115.5/120.0/125.2/137.4 | 174.4/178.4/180.5/182.9 | 133.3/139.9/146.7/155.3 | 190.7/197.2/200.9/205.3 | 184.4/186.9/192.1/197.3 | see above |

| Run | FPS (mean sampled intervals) | Encoded bytes P50/P99 | VPL cap bytes / violations | Queue/loss at report -> final; max queue ms | Backend / mixed |
| --- | --- | --- | --- | --- | --- |
| E1-30-S-r1 | 29.0 | 23182/52685 | 33333/136 | 0/0 -> 0/0; 39.85 | PRTS oneVPL D3D11 AVC/false |
| E1-30-G-r1 | 29.3 | 30332/34686 | 33333/81 | 0/0 -> 0/0; 41.4 | PRTS oneVPL D3D11 AVC/false |
| E1-30-S-r2 | 28.9 | 22008/48913 | 33333/115 | 0/0 -> 0/0; 40.06 | PRTS oneVPL D3D11 AVC/false |
| E1-30-G-r2 | 29.4 | 30285/34689 | 33333/76 | 0/0 -> 0/0; 43.71 | PRTS oneVPL D3D11 AVC/false |
| E1-30-S-r3 | 29.0 | 18120/48616 | 33333/94 | 0/0 -> 0/0; 39.43 | PRTS oneVPL D3D11 AVC/false |
| E1-30-G-r3 | 29.2 | 30333/34680 | 33333/72 | 0/0 -> 0/0; 43.45 | PRTS oneVPL D3D11 AVC/false |
| S median (3) | 29.0 | 22008/48913 | 33333/115 | 0/0 -> 0/0; 39.85 | VPL; see rows |
| G median (3) | 29.3 | 30332/34686 | 33333/76 | 0/0 -> 0/0; 43.45 | VPL; see rows |

Actual VPL last readbacks: VBV_KB=67, initial_KB=33, cap_bytes=33333, rate_control=2, intra_refresh=0, slices=1. Requested and actual settings, rounding logs and negotiation are retained in every report pipeline.

## E6-off: d25j5-c10

Endpoint switches: `--max-kbps=8000 --vpl-max-frame-factor=1.0 --vpl-vbv-frames=1`. Preset m3-combined; 60 s each, 60 fps request. Percentiles P50/P80/P95/P99. Compiler counts are recorded at each launch after setting build processes Idle. All runs have PRTS_TRACE=1.

| Run | Capture->decoded | Input->decoded | Capture->display glass | Input->display glass | Input->flip callback | clang running/count |
| --- | --- | --- | --- | --- | --- | --- |
| E6-off-S-r1 | 71.6/77.2/81.2/88.0 | 119.4/131.0/146.7/151.2 | 88.4/94.8/100.8/107.3 | 137.5/149.8/164.2/173.4 | 134.2/146.7/159.6/162.7 | yes/16 |
| E6-off-S-r2 | 75.9/81.0/84.8/94.5 | 123.4/137.9/154.2/172.3 | 92.3/98.4/104.4/111.8 | 144.2/155.1/170.8/184.7 | 138.6/151.9/163.9/184.6 | yes/16 |
| S median (2 runs) | 73.7/79.1/83.0/91.3 | 121.4/134.5/150.5/161.8 | 90.4/96.6/102.6/109.6 | 140.9/152.5/167.5/179.1 | 136.4/149.3/161.8/173.6 | see above |

| Run | FPS (mean sampled intervals) | Encoded bytes P50/P99 | VPL cap bytes / violations | Queue/loss at report -> final; max queue ms | Backend / mixed |
| --- | --- | --- | --- | --- | --- |
| E6-off-S-r1 | 56.0 | 11832/18072 | 16666/140 | 0/0 -> 0/0; 45.31 | PRTS oneVPL D3D11 AVC/false |
| E6-off-S-r2 | 55.9 | 12921/18273 | 16666/203 | 10/0 -> 10/0; 48.87 | PRTS oneVPL D3D11 AVC/false |
| S median (2) | 56.0 | 12376.5/18172.5 | 16666/171.5 | 5/0 -> 5/0; 47.09 | VPL; see rows |

Actual VPL last readbacks: VBV_KB=34, initial_KB=16, cap_bytes=16666, rate_control=2, intra_refresh=0, slices=1. Requested and actual settings, rounding logs and negotiation are retained in every report pipeline.

## E6-30: d25j5-c10

Endpoint switches: `--max-kbps=8000 --vpl-max-frame-factor=1.0 --vpl-vbv-frames=1 --vpl-intra-refresh=30`. Preset m3-combined; 60 s each, 60 fps request. Percentiles P50/P80/P95/P99. Compiler counts are recorded at each launch after setting build processes Idle. All runs have PRTS_TRACE=1.

| Run | Capture->decoded | Input->decoded | Capture->display glass | Input->display glass | Input->flip callback | clang running/count |
| --- | --- | --- | --- | --- | --- | --- |
| E6-30-S-r1 | 84.2/95.0/99.7/109.2 | 136.4/149.7/156.8/173.2 | 102.5/112.1/118.7/126.9 | 152.2/167.2/176.8/188.8 | 149.6/162.5/172.6/185.7 | yes/16 |
| E6-30-S-r2 | 74.5/80.1/83.7/94.3 | 120.9/135.5/145.2/153.5 | 91.1/97.7/103.6/113.1 | 141.2/150.5/164.7/171.4 | 136.3/147.9/156.4/168.7 | yes/16 |
| S median (2 runs) | 79.3/87.6/91.7/101.8 | 128.6/142.6/151.0/163.4 | 96.8/104.9/111.2/120.0 | 146.7/158.9/170.8/180.1 | 143.0/155.2/164.5/177.2 | see above |

| Run | FPS (mean sampled intervals) | Encoded bytes P50/P99 | VPL cap bytes / violations | Queue/loss at report -> final; max queue ms | Backend / mixed |
| --- | --- | --- | --- | --- | --- |
| E6-30-S-r1 | 55.9 | 12786/17883 | 16666/183 | 16/0 -> 16/0; 48.71 | PRTS oneVPL D3D11 AVC/false |
| E6-30-S-r2 | 56.1 | 13162/17933 | 16666/198 | 0/0 -> 0/0; 47.07 | PRTS oneVPL D3D11 AVC/false |
| S median (2) | 56.0 | 12974/17908 | 16666/190.5 | 8/0 -> 8/0; 47.89 | VPL; see rows |

Actual VPL last readbacks: VBV_KB=34, initial_KB=16, cap_bytes=16666, rate_control=2, intra_refresh=30, slices=1. Requested and actual settings, rounding logs and negotiation are retained in every report pipeline.

## E13-ulpfec: d25j5-c10

Endpoint switches: `--max-kbps=8000 --vpl-max-frame-factor=1.0 --vpl-vbv-frames=1 --fec=ulpfec`. Preset m3-combined; 60 s each, 60 fps request. Percentiles P50/P80/P95/P99. Compiler counts are recorded at each launch after setting build processes Idle. All runs have PRTS_TRACE=1.

| Run | Capture->decoded | Input->decoded | Capture->display glass | Input->display glass | Input->flip callback | clang running/count |
| --- | --- | --- | --- | --- | --- | --- |
| E13-ulpfec-S-r1 | 72.9/79.8/84.0/91.5 | 124.4/136.4/153.4/160.7 | 90.2/97.0/103.2/111.2 | 142.9/152.3/172.3/176.7 | 138.2/145.6/165.8/176.7 | yes/16 |
| E13-ulpfec-S-r2 | 88.2/95.4/100.5/115.2 | 141.7/155.2/169.5/183.2 | 105.2/112.7/119.3/135.0 | 159.9/171.6/185.9/199.0 | 155.3/166.6/181.9/194.1 | yes/16 |
| S median (2 runs) | 80.6/87.6/92.2/103.4 | 133.1/145.8/161.5/172.0 | 97.7/104.9/111.2/123.1 | 151.4/162.0/179.1/187.8 | 146.8/156.1/173.9/185.4 | see above |

| Run | FPS (mean sampled intervals) | Encoded bytes P50/P99 | VPL cap bytes / violations | Queue/loss at report -> final; max queue ms | Backend / mixed |
| --- | --- | --- | --- | --- | --- |
| E13-ulpfec-S-r1 | 54.5 | 12495/17937 | 16666/182 | 14/0 -> 33/0; 49.84 | PRTS oneVPL D3D11 AVC/false |
| E13-ulpfec-S-r2 | 54.9 | 11992/17666 | 16666/148 | 5/0 -> 17/0; 49.08 | PRTS oneVPL D3D11 AVC/false |
| S median (2) | 54.7 | 12243.5/17801.5 | 16666/165 | 9.5/0 -> 25/0; 49.46 | VPL; see rows |

Actual VPL last readbacks: VBV_KB=34, initial_KB=16, cap_bytes=16666, rate_control=2, intra_refresh=0, slices=1. Requested and actual settings, rounding logs and negotiation are retained in every report pipeline.

## W: d25j5-c12-wifi

Endpoint switches: `--max-kbps=8000 --vpl-max-frame-factor=1.0 --vpl-vbv-frames=1`. Preset m3-combined; 60 s each, 60 fps request. Percentiles P50/P80/P95/P99. Compiler counts are recorded at each launch after setting build processes Idle. All runs have PRTS_TRACE=1.

| Run | Capture->decoded | Input->decoded | Capture->display glass | Input->display glass | Input->flip callback | clang running/count |
| --- | --- | --- | --- | --- | --- | --- |
| W-S-r1 | 74.2/79.9/104.7/128.2 | 121.8/136.5/172.3/184.5 | 90.6/97.8/122.0/145.6 | 139.0/160.0/192.3/196.5 | 137.3/149.1/184.2/194.8 | yes/16 |
| W-V-r1 | 78.2/85.1/94.7/142.1 | 129.8/141.1/176.1/202.8 | 95.3/103.2/113.5/160.3 | 147.9/158.6/189.4/225.6 | 144.3/154.8/189.1/223.1 | yes/16 |
| W-G-r1 | 84.8/95.1/119.9/144.4 | 141.5/153.2/185.1/205.5 | 102.3/113.1/137.0/161.6 | 161.3/171.5/205.2/227.0 | 157.2/167.5/198.7/220.0 | yes/15 |
| W-S-r2 | 81.7/88.5/98.7/134.0 | 130.6/141.9/167.6/199.7 | 98.6/106.6/118.9/150.7 | 147.4/162.7/189.6/220.6 | 143.3/155.7/186.4/213.8 | yes/16 |
| W-V-r2 | 84.2/94.1/105.7/144.2 | 137.3/153.9/174.9/210.0 | 101.6/111.1/123.9/162.9 | 155.6/174.0/193.2/224.3 | 151.8/168.2/188.9/220.9 | yes/16 |
| W-G-r2 | 90.7/100.2/116.7/153.6 | 143.4/152.1/185.8/199.8 | 107.9/118.1/134.9/169.1 | 161.9/174.4/202.6/219.1 | 158.2/167.6/198.8/215.3 | yes/16 |
| W-S-r3 | 90.2/96.2/123.9/143.3 | 140.1/156.0/176.9/205.4 | 106.8/114.1/141.0/160.2 | 157.4/173.9/199.0/220.0 | 154.4/166.0/192.4/222.3 | yes/16 |
| W-V-r3 | 65.0/73.2/82.6/118.5 | 115.4/127.0/149.9/170.1 | 83.2/91.6/103.7/136.4 | 131.7/148.1/167.2/190.5 | 128.5/142.0/164.2/186.6 | yes/15 |
| W-G-r3 | 85.1/93.0/118.6/145.1 | 139.8/151.1/192.4/202.1 | 102.4/111.1/134.9/164.0 | 158.4/169.8/211.1/221.9 | 153.6/165.3/206.8/213.5 | yes/16 |
| S median (3 runs) | 81.7/88.5/104.7/134.0 | 130.6/141.9/172.3/199.7 | 98.6/106.6/122.0/150.7 | 147.4/162.7/192.3/220.0 | 143.3/155.7/186.4/213.8 | see above |
| V median (3 runs) | 78.2/85.1/94.7/142.1 | 129.8/141.1/174.9/202.8 | 95.3/103.2/113.5/160.3 | 147.9/158.6/189.4/224.3 | 144.3/154.8/188.9/220.9 | see above |
| G median (3 runs) | 85.1/95.1/118.6/145.1 | 141.5/152.1/185.8/202.1 | 102.4/113.1/134.9/164.0 | 161.3/171.5/205.2/221.9 | 157.2/167.5/198.8/215.3 | see above |

| Run | FPS (mean sampled intervals) | Encoded bytes P50/P99 | VPL cap bytes / violations | Queue/loss at report -> final; max queue ms | Backend / mixed |
| --- | --- | --- | --- | --- | --- |
| W-S-r1 | 56.1 | 12265/17493 | 16666/111 | 0/331 -> 0/335; 45.2 | PRTS oneVPL D3D11 AVC/false |
| W-V-r1 | 57.6 | 3089/7593 | 16666/1 | 0/137 -> 0/137; 11.37 | PRTS oneVPL D3D11 AVC/false |
| W-G-r1 | 57.4 | 9488/15334 | 16666/7 | 0/279 -> 0/279; 23.49 | PRTS oneVPL D3D11 AVC/false |
| W-S-r2 | 56.0 | 3993/9143 | 16666/2 | 0/154 -> 0/154; 19.31 | PRTS oneVPL D3D11 AVC/false |
| W-V-r2 | 57.5 | 6042/15133 | 16666/1 | 0/162 -> 0/162; 16.23 | PRTS oneVPL D3D11 AVC/false |
| W-G-r2 | 46.3 | 11793/14484 | 16666/3 | 0/192 -> 0/192; 17.17 | PRTS oneVPL D3D11 AVC/false |
| W-S-r3 | 56.0 | 12335/18053 | 16666/145 | 0/296 -> 0/296; 42.99 | PRTS oneVPL D3D11 AVC/false |
| W-V-r3 | 57.6 | 2881/5554 | 16666/1 | 0/104 -> 0/104; 10.63 | PRTS oneVPL D3D11 AVC/false |
| W-G-r3 | 57.3 | 9548/15687 | 16666/6 | 0/255 -> 0/255; 19.83 | PRTS oneVPL D3D11 AVC/false |
| S median (3) | 56.0 | 12265/17493 | 16666/111 | 0/296 -> 0/296; 42.99 | VPL; see rows |
| V median (3) | 57.6 | 3089/7593 | 16666/1 | 0/137 -> 0/137; 11.37 | VPL; see rows |
| G median (3) | 57.3 | 9548/15334 | 16666/6 | 0/255 -> 0/255; 19.83 | VPL; see rows |

Actual VPL last readbacks: VBV_KB=34, initial_KB=16, cap_bytes=16666, rate_control=2, intra_refresh=0, slices=1. Requested and actual settings, rounding logs and negotiation are retained in every report pipeline.

## Correlated trace stages

P50/P80/P95 ms, M3 stage-table ordering. Baseline uses the supplemental Btrace S run; best uses the S repeat nearest its median input/capture P80. Endpoint trace, exact RTP translation, relay packet trace, source page counters and viewer rVFC metadata are joined. Stages 6-8 lack separate GPU timestamps; fence capture_end is early handoff, not copy completion. Quantiles cover different populations and must not be summed.

| Stage | Btrace-S-r1 | E5-8-S-r2 | Evidence |
| --- | --- | --- | --- |
| 1 viewer send -> endpoint DataChannel rx | 27.27/31.82/38.87 (n=103) | 28.26/32.57/36.76 (n=104) | [V] measured |
| 2 endpoint rx -> parse -> Mojo Key post | 0.08/0.16/0.42 (n=103) | 0.08/0.12/0.23 (n=104) | [V] measured |
| 3 Mojo Key -> renderer keydown | 3.52/5.70/9.19 (n=103) | 2.68/4.32/7.12 (n=104) | [V] measured |
| 4 keydown -> next rAF tick | 5.90/8.30/9.80 (n=103) | 4.60/10.90/12.20 (n=104) | [V] measured |
| 5 rAF JS draw | 0.20/0.20/0.30 (n=2168) | 0.20/0.20/0.30 (n=2236) | [V] measured |
| 6 renderer commit/raster -> Viz DrawAndSwap | unmeasured | unmeasured | unmeasured |
| 7 Viz DrawAndSwap -> GPU copy done | unmeasured | unmeasured | unmeasured |
| 8 GPU copy done -> capture handoff | unmeasured | unmeasured | unmeasured |
| 9 draw end -> capture_begin (phase) | 32.00/32.43/32.71 (n=2168) | 17.12/17.60/17.98 (n=2236) | [V] measured |
| 10 capture_begin -> capture_end (early handoff) | 2.17/2.56/3.06 (n=2380) | 2.35/2.59/3.10 (n=2449) | [V] measured |
| 11 capture_end -> endpoint OnFrame | 2.57/2.90/3.42 (n=2380) | 2.55/2.90/3.27 (n=2449) | [V] measured |
| 12 OnFrame -> VPP/source ready -> Encode | 1.76/2.29/2.82 (n=2380) | 1.76/2.28/2.84 (n=2449) | [V] measured |
| 13 Encode -> OnEncodedImage | 8.73/10.57/12.35 (n=2380) | 8.56/10.48/11.83 (n=2449) | [V] measured |
| 14 encoded -> first packet at relay | 0.49/0.62/1.24 (n=2380) | 0.45/0.56/1.17 (n=2449) | [V] measured |
| 15 first -> last packet relay in | 0.65/0.81/0.99 (n=2380) | 0.41/0.53/0.66 (n=2449) | [V] measured |
| 16 last packet relay dwell | 43.25/48.13/60.18 (n=2380) | 38.07/41.45/45.17 (n=2449) | [V] measured |
| 17 relay out -> viewer receiveTime | 0.62/1.06/1.57 (n=2380) | 0.75/1.13/1.51 (n=2449) | [V] measured |
| 18 receiveTime -> presentationTime (jitter + decode) | 5.90/7.60/9.20 (n=2380) | 6.40/7.70/8.70 (n=2449) | [V] measured |
| 19 presentationTime -> expectedDisplayTime | 17.10/21.60/24.40 (n=2380) | 17.40/21.60/24.50 (n=2449) | [I] compositor estimate |
| T1 capture_end -> expectedDisplayTime | 82.70/90.89/105.04 (n=2380) | 77.85/82.81/87.96 (n=2449) | [V] measured |
| T2 page draw -> decoded (trace join) | 97.27/101.57/116.37 (n=2168) | 77.79/81.69/84.19 (n=2236) | [V] measured |
| T2 page draw -> expectedDisplayTime (trace join) | 113.77/120.67/134.77 (n=2168) | 94.09/99.49/105.19 (n=2236) | [V] measured |
| T3 input -> decoded (matched trace inputs) | 141.40/153.40/172.30 (n=103) | 122.20/134.00/150.00 (n=104) | [V] measured |
| T3 input -> expectedDisplayTime (matched trace inputs) | 160.60/174.00/192.50 (n=103) | 140.40/151.70/167.70 (n=104) | [V] measured |

Btrace-S-r1: RTP offset 1265170987, 3446 dominant-offset votes / 3535 matches; viewer clock drift 0.031 ms, viewer/remote min sync RTT 1.90/1.13 ms. Phase joins below -2 ms: 1/2168; retained as a timestamp/frame-association limitation.

E5-8-S-r2: RTP offset 3201630282, 3450 dominant-offset votes / 3508 matches; viewer clock drift 0.011 ms, viewer/remote min sync RTT 1.80/1.33 ms. Phase joins below -2 ms: 7/2236; retained as a timestamp/frame-association limitation.

## Decision and acceptance

Recommend the conservative 8 Mbps ceiling at 60 fps with the existing one-frame cap/VBV requests, shared-fence capture, intra-refresh off and FEC off. This is an operational choice for link headroom, **not a demonstrated latency win across two scenarios**. Keep endpoint latency defaults unchanged: the new tuning switches remain opt-in. The intended bounded VPL readback tolerance is integrated and tested; no software fallback occurred in any accepted row.

Exact endpoint settings (m3-combined also supplies VPL, the60 Hz fake-vsync clock, hires timer and factor 2.5/upper-link-capacity pacing trials):

```text
--capture-sync=fence --fps=60 --min-kbps=2000 --start-kbps=8000 --max-kbps=8000
--vpl-rc=vbr --vpl-max-frame-factor=1.0 --vpl-vbv-frames=1 --vpl-tu=7
--vpl-lowpower=on --vpl-intra-refresh=0 --vpl-slices=1 --vpl-profile=cb
--playout-delay=0,0 --fec=off
```

Use these in ENDPOINT_ARGS with the Chrome path/profile from PRESETS.md; use run.mjs --preset m3-combined --fps 60 --port 8300 --secs 60 --remote-cdp 9333 and the labeled scenario/netem profile. The c12 recommendation uses the E5-8 repeats; E2-1 explicitly requests factor 1.0 and is physically equivalent because Init uses the maximum rate. Variations between those equivalent runs are evidence of phase/load noise, not a factor effect.

Each cell below is median P80/P99 ms, P80/P99 verdict (P=pass, F=fail), and number of individual runs passing BOTH thresholds. Primary acceptance is the first two latency columns. Capture/display and input/display are expectedDisplayTime glass estimates; input/flip is the old callback detection.

| Network/scenario | Capture decoded | Input decoded | Capture display glass | Input display glass | Input flip callback | Build running |
| --- | --- | --- | --- | --- | --- | --- |
| d25j5-c12/S | 80.1/88.0 [P/P] (3/3) | 134.0/162.0 [F/P] (0/3) | 97.0/108.3 [P/P] (2/3) | 151.7/176.0 [F/P] (0/3) | 147.5/170.8 [F/P] (0/3) | yes, Idle |
| d25j5-c12/G | 92.4/102.4 [P/P] (3/3) | 148.7/176.6 [F/P] (0/3) | 109.8/121.0 [F/P] (0/3) | 166.4/193.6 [F/P] (0/3) | 161.9/188.6 [F/P] (0/3) | yes, Idle |
| d25j5-c12-wifi/S | 88.5/134.0 [P/P] (3/3) | 141.9/199.7 [F/P] (0/3) | 106.6/150.7 [F/P] (1/3) | 162.7/220.0 [F/F] (0/3) | 155.7/213.8 [F/F] (0/3) | yes, Idle |
| d25j5-c12-wifi/V | 85.1/142.1 [P/P] (3/3) | 141.1/202.8 [F/F] (0/3) | 103.2/160.3 [F/P] (1/3) | 158.6/224.3 [F/F] (0/3) | 154.8/220.9 [F/F] (0/3) | yes, Idle |
| d25j5-c12-wifi/G | 95.1/145.1 [P/P] (2/3) | 152.1/202.1 [F/F] (0/3) | 113.1/164.0 [F/P] (0/3) | 171.5/221.9 [F/F] (0/3) | 167.5/215.3 [F/F] (0/3) | yes, Idle |

Neither S nor G meets both primary latency requirements on c12 because input P80 remains above 100 ms. Displaying a passing capture median does not imply input acceptance. Wi-Fi verdicts above are from three independent runs per scenario; there is no matched M4 default-ceiling Wi-Fi baseline, so no isolated Wi-Fi improvement is claimed.

## Lever interpretation

- **E5 ceiling:** both 10 and 8 Mbps eliminated measured c12 queue drops in all six runs/configuration, versus B S totals0/6/256 (the latter two events are within measurement). Relative to B,8 Mbps S median capture/input decoded P99 changes 123.5->88.0 and 183.2->162.0 ms. G changes little (decoded input P80 151.4->148.7 ms). The traced S phase also shifts32->17 ms; credit the repeated queue result, qualify latency causality.8vs10 latency differences are mostly below 20 ms. Choose 8 for headroom, not proven superior latency or quality.
- **E2 frame factor:** factor 1.0 has 16666-byte cap; factor 2.0 has 33333. Doubling eliminates cap overshoots but gives no repeated >20 ms latency benefit in both scenarios. Factor 2 S-r1 has 0 drops at report and 10 final after measurement; do not use those10 to explain its latency. Retain 1.0.
- **E3 VBV:** one requested frame rounds to 34 KB/16 KB initial on this driver, two to 34/17, four to 67/33. Two frames therefore barely changes the actual buffer; four is distinct but supplies no repeated >20 ms latency win. Retain one requested frame. No text/visual-quality comparison was performed.
- **E1 fps:** G 30 fps decoded capture/input P80 medians are 120.0/178.4 ms versus60 fps 92.4/148.7 ms. G slowdown repeats in all three runs, exceeding20 ms. Observed~29 fps also halves frame cadence. Return 60 fps. Frame-derived cap/VBV also increase at 30 fps, so this is not a pure cadence-only attribution.
- **E6-off:** c10 S decoded capture/input P80/P99=79.1/91.3 / 134.5/161.8; queue drops at report=0/10, final=0/10.
- **E6-30:** c10 S decoded capture/input P80/P99=87.6/101.8 / 142.6/163.4; queue drops at report=16/0, final=16/0.
- **E13-ulpfec:** c10 S decoded capture/input P80/P99=87.6/103.4 / 145.8/172.0; queue drops at report=14/5, final=33/17.

E6 vertical refresh 30 is accepted by driver readback but gives no repeated >20 ms latency benefit; leave it off. The supplied c10 profile has no injected random loss and only small congestion-loss events here, so burst recovery is not established. E13 negotiates RED/ULPFEC and removes generic NACK (PLI/FIR retained); neither latency benefit nor actual repair/recovery is established. Repair counters were not collected. Leave FEC off.

## Remaining budget and baseline comparison

The correlated S trace has encoded->viewer receive P50 about 40 ms (baseline45 ms), input upstream transport about 28 ms, draw->capture phase 17 ms (baseline32 ms), encode 8.6 ms, and receive->decoded6.4 ms. Network delay/serialization is the largest measured downstream term; capture/render phase remains a large variable term and input also pays the upstream path. These are overlapping/different populations, not additive percentile budgets. Presentation adds about 17 ms at P50 but is excluded from primary acceptance. The next latency work should address frame admission/render/capture phase and transport bursts. Encoder-buffer tuning did not close the input latency gap. This task implements no new scheduling proposal.

Current B glass capture P80 S/G=111.7/111.4 ms, within/near M3 S 108.61-124.03/G 104.22-112.61 ms. Current B old input-flip P80=170.2/163.5, slower than M3 S 145.40-164.70/G 137.90-147.60 ms. B decoded capture P80~93-94 ms is below the rejected M4 capture-phase candidate135-143 ms, but these sessions differ in capture implementation, phase and build load; no isolated effect follows. Background clang-cl was present at 65/65 accepted launches; build processes were set Idle before each launch (individual counts in tables/host.json); out/rel was never built, modified or killed by this task.

## Validation, exclusions and limits

63 required 60 s runs are complete, plus one 20 s loopback smoke and one supplemental 60 s B packet trace:65 accepted reports. One additional E6-30 r2 report lost its relay connection near the end, with missing final netem stats; archived under E6-30-S-r2-invalid-relay and replaced by a fresh valid 60 s run. Its latency is excluded from the accepted matrix; the raw report is retained. The initial malformed smoke invocation failed before measurement and remains in smoke-1-r1.

The endpoint patch applied cleanly over the fence work, incremental be.sh endpoint build printed The build has finished successfully, and smoke verified 18 knob fields plus 2000->2005kbps driver rounding with no fallback. Source syntax/whitespace and all accepted report validations pass. Endpoint/experiment_options.{cc,h}, BUILD.gn, main.cc, peer.cc and vpl_encoder.cc integrate the knob draft; tools/latency/{run.mjs,pipeline.mjs,PRESETS.md} add30 fps experiment support and accurate pipeline/metric documentation. Existing fence edits are retained. No commits by this task, Chromium edits, full build, clean or test suite. The orchestrator externally committed the shared endpoint/tool work as 778d8a7 at 02:45:58 SGT; current Chromium HEAD is f8c6abc256e33788a67f7e312d2c05576910e646. The measured out/dev endpoint binary was last written 01:51:55 SGT,9671680 bytes.

Two reports (E2-1-S-r1 and E1-30-G-r3) lack an end selected-pair snapshot; valid full-window relay stats/packet traces,0 unshaped packets and normal independent input measurements support retaining them with artifact warnings. Host relay addresses changed from 192.168.110.42 to 192.168.3.31 around the E6 connection incident; a host network change is a plausible cause, not proven. All accepted final shaper profiles match the requested profile and record 0 unshaped packets.

| Configuration | Marker samples/failures (sum) | Input timeouts | Gaps>200ms (sum) | Maximum all-frame gap ms | Build counts |
| --- | --- | --- | --- | --- | --- |
| B | 11621/972 | 0 | 4 | 672.6 | 16 |
| E5-10 | 11684/967 | 0 | 3 | 652.9 | 16,15 |
| E5-8 | 11720/1018 | 0 | 3 | 622.8 | 16,15 |
| E2-1 | 11908/968 | 0 | 3 | 631.4 | 16,15 |
| E2-2 | 11728/1009 | 0 | 3 | 663.1 | 16 |
| E3-2 | 11905/1022 | 0 | 3 | 623.7 | 15,16 |
| E3-4 | 11839/1011 | 0 | 3 | 674.4 | 16,15 |
| E1-30 | 9650/749 | 0 | 3 | 641.2 | 16 |
| E6-off | 3995/449 | 0 | 0 | 129.2 | 16 |
| E6-30 | 4011/441 | 0 | 0 | 151.9 | 16 |
| E13-ulpfec | 3782/434 | 0 | 0 | 150.7 | 16 |
| W | 16776/1333 | 0 | 9 | 658.0 | 16,15 |
| smoke | 857/0 | 0 | 0 | 55.4 | 16 |
| Btrace | 1917/212 | 0 | 2 | 320.0 | 16 |

Capture quantiles are conditional on valid unique markers; the marker failures and isolated long frame gaps above are retained. Trace draw/capture joins include a few negative phases (see trace table), so frame association is not universally proven. Primary metrics retain the existing marker/clock method; trace totals use independent clock alignment and populations. Physical scanout, objective visual/text quality, other GPUs/drivers, release build performance, high profile, CBR/TU/LowPower/playout alternatives, FlexFEC and actual FEC repair/recovery remain unverified.

[V] API read at the pin/SDK: mfxstructures.h:1633-1649 (Init-only refresh cycle>=2; VBR advisory cap may overshoot); mfxdefs.h:306 (warning5); third_party/webrtc/call/rtp_video_sender.cc:110-167 (H264+generic NACK disables ULPFEC); api/video/video_timing.h:116-139 (minimal0,0 playout delay). [I] Phase/load and host-network causal interpretations remain inference. Full citations, launch argv, logs, validation.json and analysis are in the task directory.
