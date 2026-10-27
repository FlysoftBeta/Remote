# M4 S5 pacing and final matrix

Started 2026-10-27 06:10 SGT. Primary: capture/input to rVFC presentationTime, P80 <100 ms and P99 <200 ms. Glass estimates retain expectedDisplayTime. Raw evidence: C:/src/tmp/m4/s5/.

**S5 rejected; retained default is fence capture + 8 Mbps, ordinary browser clock.** All preceding S5 mode tables are diagnostic/rejected candidates. Final matrix follows baseline rebuild. Run differences below ~20 ms are noise unless repeated. Background compiler status will be recorded per batch.

## off: none, --max-kbps=8000 --start-kbps=8000, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|
| off-marker-r1 | 34.7/40.0 PASS | 48.3/63.2 PASS | 56.8/60.6 PASS | 70.9/78.6 PASS | no |
| off-marker-r2 | 33.2/37.9 PASS | 51.6/68.9 PASS | 55.7/59.1 PASS | 74.4/85.0 PASS | no |
| off-marker-r3 | 19.0/23.5 PASS | 37.4/54.3 PASS | 41.3/44.7 PASS | 60.1/70.9 PASS | no |
| off-marker-r4 | 19.3/23.3 PASS | 35.6/47.6 PASS | 41.8/45.4 PASS | 54.3/64.4 PASS | no |
| off-marker-r5 | 19.2/24.8 PASS | 24.5/40.8 PASS | 41.7/45.4 PASS | 46.8/56.6 PASS | no |

## Off phase verification

60 s loopback marker launches; 8 Mbps/fence, existing binaries. Phase uses request capture_begin joined to pixel frame ID through exact encoder/wire RTP translation. Negative pairs are retained. These trace populations include warmup.

| Run | Phase P50/P80/P95/P99 ms | Phase < -2 ms | Build running |
|---|---|---|---|
| off r1 | 16.97/17.25/17.50/17.87 | 0/3109 | no |
| off r2 | 15.84/16.08/16.30/16.44 | 5/3144 | no |
| off r3 | 1.47/1.64/1.82/3.64 | 549/3154 | no |
| off r4 | 1.71/1.88/2.14/3.78 | 533/3165 | no |
| off r5 | 1.56/1.72/1.93/3.66 | 538/3171 | no |

Off is not phase stable: two launches retain the +16.7 ms cluster and three have a low median with about 17% negative joins. These are existing frame-association limitations, not discarded samples.

## fixed-stock: none, --begin-frame-mode=fixed, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|

## off-rebuilt: none, --begin-frame-mode=off, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|
| fixed-stock-marker-r1 | run failed (1); see chain.log | - | - | - | 0 |
| off-rebuilt-marker-r1 | 19.0/23.2 PASS | 37.6/56.8 PASS | 41.4/44.7 PASS | 57.2/72.8 PASS | no |

## fixed-retry: none, --begin-frame-mode=fixed, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|
| fixed-retry-marker-r1 | 20.2/27.2 PASS | 36.3/54.9 PASS | 43.0/48.0 PASS | 57.6/72.6 PASS | no |

## fixed: none, --begin-frame-mode=fixed, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|
| fixed-marker-r1 | 36.1/44.4 PASS | 43.0/66.6 PASS | 58.1/64.4 PASS | 65.2/87.2 PASS | no |
| fixed-marker-r2 | 35.8/41.7 PASS | 52.1/69.2 PASS | 57.3/62.0 PASS | 74.0/86.5 PASS | no |
| fixed-marker-r3 | 34.9/41.1 PASS | 52.1/60.5 PASS | 56.8/61.2 PASS | 71.9/77.5 PASS | no |
| fixed-marker-r4 | 36.1/42.5 PASS | 50.8/70.6 PASS | 58.0/63.0 PASS | 72.7/92.1 PASS | no |
| fixed-marker-r5 | 19.1/23.8 PASS | 30.1/61.2 PASS | 41.6/45.2 PASS | 53.2/87.1 PASS | no |

## Implementation and first rejection

The candidate is opt-in with endpoint `--begin-frame-mode=fixed|demand` (forwarding browser `--prts-external-begin-frame`). Browser Hello advertises protocol 2 and the external-control capability; the endpoint sends pacing RPCs only after that capability. Browser helpers are per registered root/generation and serialize the compositor RPC. Endpoint precise DeadlineTimer uses an absolute 60 Hz grid with IntDiv to skip elapsed ticks; input demand is coalesced, requires >=4 ms separation, follows input on the same Mojo pipe and shifts the grid. Existing bounded GPU/native leases plus a raw-queue credit hint limit admission. Display ACK, capture result and GPU fence are kept distinct. Root removal and weak callbacks invalidate late work; an unacknowledged tick has a 2 s terminal chain watchdog. This recovery policy is experimental.

[V] At this pin `DesktopWindowTreeHostWin::Init` can select immutable external root control before `OnAcceleratedWidgetAvailable`; `ui::Compositor::IssueExternalBeginFrame` retains one pending request until the controller binds. `ExternalBeginFrameSourceMojo` rejects overlaps. Source IDs come from the browser's BeginFrameSource allocator, sequences increase per generation. `SurfaceAggregator` notifies damage then collects newly added copy requests in the same aggregation. Capture sampling is relaxed to 4 ms only for controlled roots so an admitted early demand tick is not independently sampled away. Request-side source/sequence/token and actual aggregated child metadata are traced; shared-fence ownership/delivery is unchanged.

The initial successful fixed trial used a mistakenly sliding ~57 Hz timer (TimeDelta division returns double); its 1.55 ms median is diagnostic only. The timer is corrected to IntDiv before the five `fixed` runs. Corrected fixed with stock renderer scheduling retains a ~16.7 ms phase despite current-tick child ACKs. [I] Current ACKs can tag draws of stale active-tree content; ACK identity alone is not pixel freshness proof. Full-pipeline and ACK policy comparisons follow. The first fixed launch failed before Hello and produced no metric; fresh rebuilt-off/fixed launches succeeded. Its cause is unestablished.

## Phase and admission: fixed

Phase is page draw end �� capture request begin. No negative samples are clamped. All runs below have compiler count 0. ACK source/sequence is necessary but does not prove current active-tree pixels.

| Run | Phase P50/P80/P95/P99 ms | Phase < -2 ms | Tick Hz | ACK P80/P99 ms | Overlaps/rejects | Content aggregations matching tick |
|---|---|---|---|---|---|---|
| fixed-marker-r1 | 16.81/17.69/18.44/20.52 | 0/3066 | 59.59 | 2.38/7.67 | 0/1 | 180/180 |
| fixed-marker-r2 | 16.43/17.35/18.25/19.55 | 129/3130 | 59.50 | 3.33/11.58 | 0/0 | 180/180 |
| fixed-marker-r3 | 16.86/17.53/18.20/19.58 | 1/3088 | 59.45 | 2.38/7.31 | 0/1 | 181/181 |
| fixed-marker-r4 | 16.88/17.62/18.35/19.68 | 0/3105 | 59.46 | 2.36/7.86 | 0/1 | 181/181 |
| fixed-marker-r5 | 1.48/1.65/1.91/3.06 | 513/3115 | 59.48 | 3.53/11.37 | 0/0 | 181/181 |

## fixed-full: none, --begin-frame-mode=fixed --cr-run-all-compositor-stages-before-draw, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|
| fixed-full-marker-r1 | 20.1/27.0 PASS | 27.2/42.1 PASS | 42.7/46.7 PASS | 47.0/62.9 PASS | no |

## fixed-full-noack: none, --begin-frame-mode=fixed --cr-run-all-compositor-stages-before-draw --cr-enable-features=NoCompositorFrameAcks, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|
| fixed-full-noack-marker-r1 | 20.0/25.8 PASS | 35.4/50.5 PASS | 42.6/46.5 PASS | 57.9/66.4 PASS | no |

## fixed-full: none, --begin-frame-mode=fixed --cr-run-all-compositor-stages-before-draw, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|

## Phase and admission: fixed-full, fixed-full-noack

Phase is page draw end �� capture request begin. No negative samples are clamped. All runs below have compiler count 0. ACK source/sequence is necessary but does not prove current active-tree pixels.

| Run | Phase P50/P80/P95/P99 ms | Phase < -2 ms | Tick Hz | ACK P80/P99 ms | Overlaps/rejects | Content aggregations matching tick |
|---|---|---|---|---|---|---|
| fixed-full-marker-r1 | 1.50/1.71/2.03/5.03 | 510/3123 | 59.37 | 3.51/11.67 | 0/0 | 180/180 |
| fixed-full-noack-marker-r1 | 1.43/1.65/1.98/4.33 | 510/3115 | 59.47 | 3.50/11.42 | 0/0 | 181/181 |
| fixed-full-marker-r2 | 20.0/26.0 PASS | 36.9/52.9 PASS | 42.6/46.5 PASS | 49.2/68.1 PASS | no |
| fixed-full-marker-r3 | 20.1/26.2 PASS | 35.9/58.1 PASS | 42.6/46.5 PASS | 58.5/72.2 PASS | no |
| fixed-full-marker-r4 | 19.8/25.9 PASS | 30.2/47.2 PASS | 42.3/46.3 PASS | 53.7/65.7 PASS | no |
| fixed-full-marker-r5 | 20.1/27.2 PASS | 28.7/43.8 PASS | 42.6/46.3 PASS | 51.5/62.1 PASS | no |

## demand-full: none, --begin-frame-mode=demand --cr-run-all-compositor-stages-before-draw, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|

## Phase and admission: fixed-full

Phase is page draw end �� capture request begin. No negative samples are clamped. All runs below have compiler count 0. ACK source/sequence is necessary but does not prove current active-tree pixels.

| Run | Phase P50/P80/P95/P99 ms | Phase < -2 ms | Tick Hz | ACK P80/P99 ms | Overlaps/rejects | Content aggregations matching tick |
|---|---|---|---|---|---|---|
| fixed-full-marker-r1 | 1.50/1.71/2.03/5.03 | 510/3123 | 59.37 | 3.51/11.67 | 0/0 | 180/180 |
| fixed-full-marker-r2 | 1.47/1.69/2.01/4.42 | 530/3126 | 59.38 | 3.50/11.27 | 0/0 | 181/181 |
| fixed-full-marker-r3 | 1.41/1.63/1.95/4.66 | 514/3124 | 59.38 | 3.54/11.52 | 0/1 | 181/181 |
| fixed-full-marker-r4 | 1.38/1.59/1.92/4.64 | 505/3105 | 59.18 | 3.52/11.31 | 0/0 | 180/180 |
| fixed-full-marker-r5 | 1.40/1.61/1.96/4.91 | 502/3109 | 59.36 | 3.44/11.19 | 0/0 | 181/181 |
| demand-full-marker-r1 | 19.8/27.7 PASS | 35.6/48.0 PASS | 42.4/46.5 PASS | 53.6/68.9 PASS | no |
| demand-full-marker-r2 | 19.9/28.0 PASS | 37.4/55.2 PASS | 42.6/46.7 PASS | 53.7/73.5 PASS | no |

## demand-boundary: none, --begin-frame-mode=demand --cr-run-all-compositor-stages-before-draw, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|
| demand-boundary-marker-r1 | 20.0/28.6 PASS | 35.4/52.4 PASS | 42.4/48.3 PASS | 53.0/65.9 PASS | no |
| demand-boundary-marker-r2 | 20.0/27.9 PASS | 32.7/51.0 PASS | 42.4/46.9 PASS | 52.8/64.7 PASS | no |
| demand-boundary-marker-r3 | 20.0/27.3 PASS | 33.5/46.7 PASS | 42.3/47.4 PASS | 54.2/62.5 PASS | no |
| demand-boundary-marker-r4 | 19.6/26.4 PASS | 32.9/52.5 PASS | 41.8/46.3 PASS | 53.2/74.8 PASS | no |
| demand-boundary-marker-r5 | 19.7/27.0 PASS | 32.0/48.1 PASS | 41.8/46.8 PASS | 53.0/68.9 PASS | no |

## functional: none, --begin-frame-mode=demand --cr-run-all-compositor-stages-before-draw, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|

## Phase and admission: demand-boundary

Phase is page draw end �� capture request begin. No negative samples are clamped. All runs below have compiler count 0. ACK source/sequence is necessary but does not prove current active-tree pixels.

| Run | Phase P50/P80/P95/P99 ms | Phase < -2 ms | Tick Hz | ACK P80/P99 ms | Overlaps/rejects | Content aggregations matching tick |
|---|---|---|---|---|---|---|
| demand-boundary-marker-r1 | 1.35/1.57/1.94/5.32 | 549/3089 | 61.40 | 3.56/11.47 | 0/1 | 187/187 |
| demand-boundary-marker-r2 | 1.36/1.60/2.02/5.09 | 563/3121 | 61.30 | 3.61/11.46 | 0/1 | 187/187 |
| demand-boundary-marker-r3 | 1.42/1.64/2.03/5.01 | 546/3112 | 61.34 | 3.62/11.47 | 0/2 | 187/187 |
| demand-boundary-marker-r4 | 1.50/1.72/2.07/4.79 | 552/3123 | 61.25 | 3.60/11.22 | 0/0 | 187/187 |
| demand-boundary-marker-r5 | 1.46/1.70/2.08/4.92 | 540/3106 | 61.34 | 3.66/11.55 | 0/1 | 187/187 |

## Median comparison: off, fixed, fixed-full, fixed-full-noack, demand-full, demand-boundary

Each metric is median P80/P99 ms across independent runs; parenthesis counts individual runs passing both P80<100/P99<200. Primary acceptance uses decoded columns. Glass is expectedDisplayTime.

| Configuration/network/scenario | Capture decoded | Input decoded | Capture display glass | Input display glass | Build running |
|---|---|---|---|---|---|
| off/none/marker | 19.3/24.8 PASS (5/5) | 37.4/54.3 PASS (5/5) | 41.8/45.4 PASS (5/5) | 60.1/70.9 PASS (5/5) | no (all launches) |
| fixed/none/marker | 35.8/41.7 PASS (5/5) | 50.8/66.6 PASS (5/5) | 57.3/62.0 PASS (5/5) | 71.9/87.1 PASS (5/5) | no (all launches) |
| fixed-full/none/marker | 20.1/26.2 PASS (5/5) | 30.2/47.2 PASS (5/5) | 42.6/46.5 PASS (5/5) | 51.5/65.7 PASS (5/5) | no (all launches) |
| fixed-full-noack/none/marker | 20.0/25.8 PASS (1/1) | 35.4/50.5 PASS (1/1) | 42.6/46.5 PASS (1/1) | 57.9/66.4 PASS (1/1) | no (all launches) |
| demand-full/none/marker | 19.9/27.9 PASS (2/2) | 36.5/51.6 PASS (2/2) | 42.5/46.6 PASS (2/2) | 53.6/71.2 PASS (2/2) | no (all launches) |
| demand-boundary/none/marker | 20.0/27.3 PASS (5/5) | 32.9/51.0 PASS (5/5) | 42.3/46.9 PASS (5/5) | 53.0/65.9 PASS (5/5) | no (all launches) |

| Configuration/network/scenario | Mean decoded fps (range) | Marker valid/failed totals | Input timeouts | Gaps >200 ms / max gap ms | Queue/loss drops (sum, final) | Backend |
|---|---|---|---|---|---|---|
| off/none/marker | 59.99�C60.02 | 14459/0 | 0 | 0 / 61.4 | 0/0 | oneVPL throughout |
| fixed/none/marker | 59.89�C59.98 | 14528/0 | 0 | 0 / 75.8 | 0/0 | oneVPL throughout |
| fixed-full/none/marker | 59.87�C59.90 | 14607/0 | 0 | 0 / 74.5 | 0/0 | oneVPL throughout |
| fixed-full-noack/none/marker | 59.92�C59.92 | 2910/0 | 0 | 0 / 73.3 | 0/0 | oneVPL throughout |
| demand-full/none/marker | 60.35�C60.38 | 5853/0 | 0 | 0 / 74.7 | 0/0 | oneVPL throughout |
| demand-boundary/none/marker | 61.43�C61.72 | 14580/0 | 0 | 0 / 73.0 | 0/0 | oneVPL throughout |

## functional-noack: none, --begin-frame-mode=demand --cr-run-all-compositor-stages-before-draw --cr-enable-features=NoCompositorFrameAcks, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|
| functional-noack-marker-r1 | 18.8/26.4 PASS | 46.8/1025.1 FAIL | 41.1/46.6 PASS | 61.4/1050.7 FAIL | no |

## default: none, --capture-sync=fence, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|
| default-marker-r1 | 20.1/27.9 PASS | 35.4/52.3 PASS | 42.4/47.0 PASS | 55.0/63.2 PASS | no |
| default-marker-r2 | 20.1/28.5 PASS | 34.3/45.2 PASS | 42.5/49.1 PASS | 55.8/64.9 PASS | no |
| default-marker-r3 | 19.9/27.3 PASS | 32.2/45.5 PASS | 42.6/46.9 PASS | 53.9/67.8 PASS | no |
| default-marker-r4 | 20.1/27.7 PASS | 33.6/53.2 PASS | 42.5/48.6 PASS | 55.0/76.5 PASS | no |
| default-marker-r5 | 19.3/26.4 PASS | 30.7/44.8 PASS | 41.6/45.9 PASS | 52.0/61.3 PASS | no |

## Phase and admission: default

Phase is page draw end �� capture request begin. No negative samples are clamped. All runs below have compiler count 0. ACK source/sequence is necessary but does not prove current active-tree pixels.

| Run | Phase P50/P80/P95/P99 ms | Phase < -2 ms | Tick Hz | ACK P80/P99 ms | Overlaps/rejects | Content aggregations matching tick |
|---|---|---|---|---|---|---|
| default-marker-r1 | 1.43/1.66/2.05/4.70 | 541/3109 | 61.44 | 3.65/11.50 | 0/0 | 188/188 |
| default-marker-r2 | 1.41/1.65/2.10/5.40 | 558/3099 | 61.30 | 3.59/11.39 | 0/0 | 187/187 |
| default-marker-r3 | 1.43/1.67/2.07/4.81 | 579/3119 | 61.36 | 3.62/11.39 | 0/1 | 187/187 |
| default-marker-r4 | 1.44/1.70/2.11/5.12 | 542/3107 | 61.39 | 3.60/11.53 | 0/0 | 187/187 |
| default-marker-r5 | 1.43/1.65/2.00/4.80 | 563/3119 | 61.49 | 3.63/11.29 | 0/0 | 187/187 |

## functional-long: none, --capture-sync=fence, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|
| functional-long-marker-r1 | 18.8/25.6 PASS | 32.4/1061.4 FAIL | 41.1/45.7 PASS | 56.3/1082.1 FAIL | no |

## final-c12: d25j5-c12, --capture-sync=fence, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|
| final-c12-S-r1 | run failed (1); see chain.log | - | - | - | 0 |

## recovery-S: d25j5-c12, --capture-sync=fence, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|

## recovery-S2: d25j5-c12, --capture-sync=fence, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|

## recovery-off: d25j5-c12, --begin-frame-mode=off --capture-sync=fence, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|
| recovery-off-S-r1 | 88.1/94.5 PASS | 137.1/141.3 FAIL | 107.4/115.1 FAIL | 153.7/157.5 FAIL | no |

## recovery-S3: d25j5-c12, --capture-sync=fence, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|
| recovery-S3-S-r1 | 96.2/107.3 PASS | 128.4/2980.1 FAIL | 115.5/127.3 FAIL | 148.9/2993.1 FAIL | no |

## final-c12: d25j5-c12, --capture-sync=fence, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|
| final-c12-S-r1 | run failed (1); see chain.log | - | - | - | 0 |

## final-c12: d25j5-c12, --capture-sync=fence, dev

Each cell P80/P99 ms; PASS requires both thresholds.

| Run | Capture decoded | Input decoded | Capture display glass | Input display glass | Compilers running |
|---|---|---|---|---|---|
| final-c12-S-r1 | 72.2/80.0 PASS | 122.6/139.8 FAIL | 91.6/100.7 PASS | 139.0/158.2 FAIL | no |
| final-c12-V-r1 | 71.6/79.9 PASS | 117.4/143.1 FAIL | 90.7/102.2 PASS | 137.5/168.2 FAIL | no |
| final-c12-G-r1 | 73.9/82.1 PASS | 118.9/140.1 FAIL | 92.5/103.7 PASS | 136.9/159.0 FAIL | no |
| final-c12-S-r2 | 75.8/82.4 PASS | 123.0/138.0 FAIL | 95.3/103.1 PASS | 144.3/152.7 FAIL | no |
| final-c12-V-r2 | 72.7/80.5 PASS | 115.4/127.4 FAIL | 91.1/101.7 PASS | 135.7/142.9 FAIL | no |
| final-c12-G-r2 | 73.1/84.1 PASS | 124.0/134.6 FAIL | 91.8/104.3 PASS | 137.9/154.6 FAIL | no |
| final-c12-S-r3 | 74.4/81.8 PASS | 123.5/140.9 FAIL | 94.2/102.3 PASS | 144.1/157.7 FAIL | no |
