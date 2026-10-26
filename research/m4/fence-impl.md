# M4 shared-fence implementation and measurements

Shared-fence early hand-off is applied as the default capture path when both devices support it; explicit `--capture-sync=mutex` retains keyed synchronization. 14/14 required 60 s runs are available. Shaped decoded acceptance (both capture and input P80 <100 ms, P99 <200 ms): fence 0/4; mutex 0/4. No commits.

2026-10-27 SGT. Logs, scripts, JSON/CSV, trace records, NOTES and applied diffs: `C:/src/tmp/m4/fence-impl/`. Chromium pin `d511e7e648162f4df099eb5c1e2e25ebab3fe411`; remotekit base `bcaa3379575996137d178343ae5b20b187ef04da`. [V] = read at pin / measured; [I] = inference.

## Decision and implementation

[V] The host probe in [shared-fence.md](shared-fence.md) passed driver capability and cross-process copy -> GPU Wait -> VPP -> oneVPL ordering with decoded-content validation and failing no-Wait controls. The decision rule therefore selects the fence implementation, not completion-polling delivery. The initial Chromium working tree was clean; the neighboring capture-phase report records that its rejected scheduling experiment had already been restored. That starting state is preserved.

[V] Implemented narrowly negotiated `kPrtsSharedFence` pools. Only these allocate shared NT textures without keyed mutexes; other capture and GMB clients retain their existing allocation. Pool creation checks the actual Graphite D3D11 writer's shared-fence support and falls back to a keyed pool when unavailable. Endpoint `--capture-sync=fence|mutex` defaults to fence after a consumer capability probe. Invalid switch values fail launch.

[V] Viz inserts/submits the RGBA copy, calls `submitAndFlushBackend()`, closes destination write access, flushes any EndAccess-generated work, then Signals the writer's immediate-context fence and Flushes. It sends the result immediately with an immutable `{handle, token, value, adapter_luid}` snapshot. One timeline is owned by each output/device generation. The snapshot crosses the existing result callback and capture Mojo path through a Windows-only `GetPrtsReadyFence()` response on the existing per-frame release lease. The browser sends the OS fence handle to the endpoint once per token in each import epoch; subsequent frames carry the token/value. A new token is used when the writer device changes.

[V] Endpoint opens each token with `OpenSharedFence`, verifies adapter identity and texture sync mode, and queues `ID3D11DeviceContext4::Wait` on the device used for native VPP/oneVPL. The retained private BGRA copy precedes a dedicated source-read EVENT query; VPP and encode proceed without waiting for this query on the CPU. The import sequence polls it asynchronously at 1 ms with DONOTFLUSH. A successful query returns `consumed=true`, allowing Viz `Done()` and pool reuse. An explicit `consumed=false` reply or a dropped fence-mode reply closes the lease without Done: Viz invalidates the old pool before releasing its allocation, rejects late results from that generation, and prevents marked-frame resurrection. Fence-mode static refresh issues a new copy.

[V] Existing VPL device adoption requests fresh headers/IDR on device-generation changes (`endpoint/vpl_encoder.cc:977-991`); no rate-control code, `peer.cc`, or `vpl_encoder.*` was edited. Reset clears imported fence identities and textures. Outstanding source reads are abandoned, and a new consumer device replaces potentially blocked old GPU waits.


[V] Buffer reuse replaces the keyed mutex with two dependencies: producer GPU Signal -> consumer GPU Wait for incoming readiness; a completed EVENT after the private BGRA source copy -> Done for returning the capture allocation. VPP is already submitted before asynchronous release polling runs. Because Done follows actual source-read completion, Viz needs no reverse GPU fence before overwriting that slot. A disconnected or explicitly abandoned lease invalidates the pool first. This retains the private BGRA copy.

[V] The per-frame lease query adds an IPC round trip and currently duplicates a fence handle into the browser for each response. Browser -> endpoint handle transmission and OpenSharedFence occur once per timeline/import epoch. Eliminating those intermediate duplicate handles is a remaining optimization.

## Build and content verification

[V] Only incremental `out/dev` chrome/prts_endpoint targets were built. `build8.log` verifies a no-work exit 0 after linking; `build9.log` records the final formatting rebuild exit 0. No full build, clean or test suite; no out/rel changes or process termination. Generic VideoFrameInfo, CopyOutputResult and BlitRequest header experiments were restored; the final contract is the capture-specific result subtype and lease RPC. Existing interface mocks were adjusted but their test targets were not built.

Four distant CRC-protected copies of the page frame counter are checked in each decoded image. Observed frameId/timestamp pairs must exist in sender draw records; backward counters and spatial disagreement fail.

| Fence content run | Decoded samples | Distinct counters | Repeats | Torn / backward / unknown-source | Undecodable | Strict zero-error verdict |
| --- | --- | --- | --- | --- | --- | --- |
| marker | 4536 | 4075 | 461 | 0/0/0 | 0 | pass |
| G | 4378 | 4191 | 187 | 0/0/0 | 0 | pass |
| G-restart | 4326 | 4147 | 177 | 0/0/0 | 2 | fail |

[V] Marker and loaded G each exceed 2000 distinct counters without coherence/order faults. Repeated page counters are retained separately; capture refresh can repeat a page draw. This checks spatial coherence and counter order, not a calibrated proof against a uniformly old but ordered image. The adversarial raw+decoded probe in [shared-fence.md](shared-fence.md) supplies separate cross-engine ordering evidence, including failing no-Wait controls.

[V] GPU restart: Browser.crashGpuProcess at 2026-10-26T17:29:57.412Z; source Reset at 01:29:58.611 SGT, new timeline OpenSharedFence at 01:29:58.958, IDR output (key=1) at 01:29:58.981. Token changed from F23560F950FEE1A10C15581E8FE6A0E1 to 660494BC9FB09C6423EE61E703125D4C. There were 2670 distinct counters after injection and a maximum decoded-callback gap of 1606.2 ms. Two recovery frames had all four markers undecodable; their errors remain in checksum-G-restart/checksum.json and the strict verdict is false. No decoded marker disagreed or moved backwards. Recovery is not seamless.

[V] Content checks use task-local checksum-harness.mjs (repository harness with viewer CDP enabled) and four added page markers. These instrumented 100 s runs and 20 s smoke runs are excluded from the latency matrix. One initial cold Chrome launch missed the existing 15 s Hello deadline, and an initial checker raced page loading; both failed attempts are retained in task logs and excluded from measurements.

## Latency matrix

[V] Each row is 60 s with --preset m3-combined and PRTS_TRACE=1; each mode starts a fresh chain/profile and pairs alternate in the same host session. Marker is loopback; S/G use d25j5-c12 (25 ms delay, 5 ms jitter, 12000 kbps). Intel UHD630 driver30.0.100.9805; Chromium157.0.8103.15; headless Edge156 viewer; recorded encoded frame1904x1024. All report JSON files retain pipeline and shaping details.

Percentiles are **P50/P80/P95/P99**, milliseconds. Primary decoded metrics use same-frame rVFC metadata.presentationTime. Display uses expectedDisplayTime and is a glass estimate, excluding a measured physical display. Input->flip is the legacy callback-detection metric, shown separately from expectedDisplayTime input glass. Capture latency is the page marker draw-to-viewer estimate used by the existing harness, not an independently measured sensor exposure timestamp.

| Run | Capture->decoded | Input->decoded | Capture->display glass | Input->display glass | Input->flip callback |
| --- | --- | --- | --- | --- | --- |
| marker-fence-1 | 18.3/19.0/20.5/23.6 | 34.8/36.5/51.4/53.3 | 37.1/41.4/43.8/45.1 | 54.0/59.2/65.3/68.7 | 48.4/51.7/57.3/63.9 |
| marker-mutex-1 | 28.5/29.4/31.0/37.1 | 36.7/45.1/53.0/63.4 | 46.3/50.9/53.9/55.4 | 57.6/62.7/71.2/79.1 | 50.2/55.2/68.2/75.2 |
| marker-fence-2 | 18.2/18.8/20.2/23.6 | 32.9/34.1/49.0/53.4 | 36.8/41.3/43.6/44.7 | 54.1/56.6/63.6/70.3 | 45.1/50.3/55.7/65.3 |
| marker-mutex-2 | 28.7/29.5/30.9/36.7 | 44.9/47.1/62.7/79.6 | 46.5/51.2/54.1/55.6 | 61.8/65.6/79.1/105.1 | 54.5/61.9/72.0/86.2 |
| marker-fence-3 | 18.5/19.1/20.3/23.1 | 35.6/38.5/52.7/57.7 | 37.2/41.6/43.9/45.0 | 53.3/61.4/67.3/69.2 | 49.5/53.5/59.3/64.4 |
| marker-mutex-3 | 28.5/29.4/31.0/35.7 | 42.3/44.0/59.5/61.2 | 46.2/50.7/53.8/55.4 | 60.9/65.4/73.9/82.8 | 54.4/60.0/65.3/72.7 |
| S-fence-1 | 76.9/81.8/86.8/92.6 | 119.8/127.8/134.3/146.2 | 95.1/101.3/107.1/112.9 | 136.2/147.3/152.9/159.7 | 128.3/138.5/145.0/151.7 |
| S-mutex-1 | 86.3/91.3/95.5/99.7 | 131.6/141.0/149.5/151.7 | 104.4/110.5/115.8/119.6 | 150.8/158.5/167.9/176.2 | 142.5/149.7/156.9/157.9 |
| S-fence-2 | 74.3/80.3/86.4/92.8 | 119.5/128.2/141.7/146.3 | 93.1/99.1/106.2/112.0 | 136.8/149.6/158.0/159.4 | 128.0/140.9/149.3/152.1 |
| S-mutex-2 | 86.4/91.7/96.1/100.4 | 129.8/140.3/149.2/155.7 | 104.6/110.6/116.4/121.2 | 148.9/157.5/167.2/175.5 | 142.0/154.1/160.1/166.2 |
| G-fence-1 | 68.9/73.6/77.3/80.8 | 107.6/115.7/133.0/134.7 | 86.8/92.7/98.2/102.2 | 128.5/135.9/156.4/157.3 | 121.6/132.3/138.0/140.6 |
| G-mutex-1 | 83.6/89.2/93.5/97.3 | 131.9/138.7/147.6/151.5 | 101.4/108.0/114.1/119.5 | 147.8/157.7/166.4/168.0 | 140.1/147.0/156.0/158.6 |
| G-fence-2 | 71.2/76.8/81.3/85.8 | 118.5/123.8/130.7/133.1 | 89.0/95.5/102.1/107.4 | 134.3/142.5/150.0/153.2 | 125.7/132.0/139.0/140.1 |
| G-mutex-2 | 80.1/84.6/89.1/93.4 | 124.8/131.5/147.7/149.2 | 97.7/103.8/109.6/114.8 | 142.0/151.9/166.3/171.5 | 136.3/146.8/153.8/155.0 |

## Capture stages

[V] PRTS_TRACE S/P/F rows, capture_begin restricted to the viewer measurement window. Logger wall time minus adjacent source cv TimeTicks aligns clocks by their median offset (millisecond prefix precision). Submit stages use native frames delivered to the source. P50/P80/P95/P99 in ms; full-log counts and clock offsets are retained in summary.json. Capture_end is early delivery in fence mode and completed-copy delivery in mutex mode. Open counts cover the full log; S/G each reopened the same token after startup viewport Reset, with no per-frame reopening.

| Run | Begin->receive | Receive->VPP submit | Begin->VPP submit | Begin->capture_end | Pending fence / receives; timeline opens |
| --- | --- | --- | --- | --- | --- |
| marker-fence-1 | 2.2/2.4/2.7/5.7 | 0.3/0.4/0.5/1.0 | 2.5/2.7/3.1/7.8 | 1.1/1.3/1.5/3.2 | 3595/3600; 1 |
| marker-mutex-1 | 15.2/15.6/15.9/18.2 | 0.3/0.4/0.5/1.2 | 15.6/15.9/16.3/19.5 | 14.4/14.8/15.1/15.4 | 0/0; 0 |
| marker-fence-2 | 2.2/2.4/2.6/5.4 | 0.3/0.4/0.4/0.9 | 2.5/2.7/3.0/6.3 | 1.1/1.3/1.4/2.8 | 3594/3600; 1 |
| marker-mutex-2 | 15.3/15.6/15.9/17.5 | 0.3/0.4/0.5/1.2 | 15.6/16.0/16.3/18.5 | 14.4/14.8/15.1/15.3 | 0/0; 0 |
| marker-fence-3 | 2.2/2.4/2.6/5.4 | 0.3/0.4/0.4/0.8 | 2.5/2.7/3.0/7.0 | 1.1/1.3/1.5/3.5 | 3596/3600; 1 |
| marker-mutex-3 | 15.2/15.6/16.0/18.0 | 0.3/0.4/0.5/1.2 | 15.6/16.0/16.3/19.2 | 14.4/14.8/15.1/15.4 | 0/0; 0 |
| S-fence-1 | 2.7/2.9/3.1/3.6 | 0.5/0.6/0.7/0.8 | 3.1/3.4/3.7/4.5 | 1.6/1.8/1.9/2.1 | 3396/3449; 2 |
| S-mutex-1 | 16.4/16.7/17.0/17.1 | 0.4/0.5/0.6/0.6 | 16.8/17.1/17.4/17.7 | 15.6/15.8/16.1/16.2 | 0/0; 0 |
| S-fence-2 | 2.7/2.9/3.2/3.5 | 0.5/0.6/0.7/0.8 | 3.2/3.4/3.8/4.3 | 1.6/1.8/1.9/2.1 | 3395/3445; 2 |
| S-mutex-2 | 16.4/16.7/17.0/17.2 | 0.4/0.5/0.6/0.7 | 16.8/17.1/17.4/17.7 | 15.5/15.8/16.1/16.2 | 0/0; 0 |
| G-fence-1 | 2.8/3.7/4.2/5.1 | 0.5/0.6/0.7/0.9 | 3.3/4.2/4.8/6.5 | 1.4/2.3/2.8/3.1 | 3555/3560; 2 |
| G-mutex-1 | 16.5/16.8/17.1/17.6 | 0.4/0.5/0.6/0.7 | 16.9/17.2/17.5/18.2 | 15.6/15.9/16.2/16.4 | 0/0; 0 |
| G-fence-2 | 2.8/3.6/4.2/4.8 | 0.5/0.6/0.7/0.9 | 3.3/4.1/4.8/5.8 | 1.4/2.3/2.8/3.0 | 3559/3559; 2 |
| G-mutex-2 | 16.5/16.8/17.1/17.8 | 0.4/0.5/0.5/0.7 | 16.9/17.2/17.5/18.4 | 15.7/15.9/16.2/16.6 | 0/0; 0 |

## Acceptance and pipeline checks

| Shaped run | Both decoded P80<100 / P99<200 | Capture/input samples | Encoder / mixed | Source/fence faults |
| --- | --- | --- | --- | --- |
| S-fence-1 | fail | 2616/110 | vpl/false | 0 |
| S-mutex-1 | fail | 2611/110 | vpl/false | 0 |
| S-fence-2 | fail | 2620/109 | vpl/false | 0 |
| S-mutex-2 | fail | 2580/109 | vpl/false | 0 |
| G-fence-1 | fail | 2678/117 | vpl/false | 0 |
| G-mutex-1 | fail | 2667/117 | vpl/false | 0 |
| G-fence-2 | fail | 2599/117 | vpl/false | 0 |
| G-mutex-2 | fail | 2704/117 | vpl/false | 0 |

[V] Across shaped fence runs, capture->decoded P80 spans 73.6-81.8 ms and P99 80.8-92.8 ms; input->decoded P80 spans 115.7-128.2 ms and P99 133.1-146.3 ms. Capture->display glass P80 spans 92.7-101.3 ms; legacy input->flip P80 spans 132.0-140.9 ms. Corresponding mutex capture/input decoded P80 ranges are 84.6-91.7/131.5-141.0 ms. The input P80 requirement remains unmet.

[V] Repeated loopback capture->decoded P80: fence 18.8-19.1 ms versus mutex 29.4-29.5 ms. This repeated roughly 10 ms gain is smaller than the run-to-run noise rule, so it is supported by repetition and the per-stage trace rather than one pair. No larger cross-scenario gain is assumed.

## Pin API evidence

- [V] `gpu/command_buffer/service/graphite_shared_context.h:104`: backend flush is required for delayed D3D11 submission; submit alone may defer commands.
- [V] `gpu/command_buffer/service/shared_context_state.cc:1507`: Graphite uses the actual Dawn D3D11 device, rather than the independent IO allocation device.
- [V] `ui/gfx/win/d3d_shared_fence.cc:299`: IncrementAndSignal issues the GPU timeline signal on the owning device.
- [V] `gpu/command_buffer/service/shared_image/d3d_image_backing.cc:1523`: Dawn EndAccess exports access-completion fences; no-keyed backings already use fence interop.
- [V] `mojo/public/cpp/bindings/callback_helpers.h:163`: the drop-handler wrapper distinguishes a successful reply from callback destruction.
- [V] `chrome/browser/devtools/protocol/browser_handler.cc`: Browser.crashGpuProcess is the pinned GPU restart injection used for the recovery check.


## Artifacts and reproduction

- Applied changes: `chromium.diff` and `endpoint.diff` in the task directory; newly added capture-result files are included. All changes remain applied, uncommitted.
- `run.mjs`: fourteen runs (seven pairs) using the unmodified repository latency harness; set up mode/preset/trace/profile arguments automatically. `runs.jsonl` records launch/exit times.
- `checksum.mjs marker`, `checksum.mjs G`, `checksum.mjs G --restart`: content/recovery checks; sender/decoded rows and verdict.json retained.
- `analyze.mjs`: raw JSON/trace extraction into summary.json and tables.md. Every per-run directory retains chain.log, latency JSON/CSV, pipeline fingerprints and trace/remote.json.
- No vpl_encoder.* rate-control or peer.cc changes. Endpoint changes are limited to d3d_frame_source.cc, d3d_native_frame.cc/.h and endpoint_link.cc.

## Limits and risks

[I] Same-host, single-adapter/driver evidence only. Multi-adapter negotiation, unsupported-writer automatic fallback and repeated device removal were not exercised. A shared-handle/import failure fails closed; explicit mutex is available, but runtime automatic renegotiation after unexpected failure is not implemented. The two undecodable recovery frames remain unresolved. Reported display values are headless estimates. Capture phase locks per run: differences below roughly 20 ms require repetition, and a source-stage reduction does not imply the same end-to-end improvement.
