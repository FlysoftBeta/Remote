# Viewer frame-rate negotiation

Validated 2026-10-29 SGT on Intel UHD 630, Chromium 157 remote browser and Edge 156 receiver.

## Policy

- Default precise remote vsync; supported endpoint ceiling 60 fps. `--fps` is an optional administrator ceiling.
- Viewer measures its animation clock, advertises refresh capability, and intersects it with the endpoint ceiling. Headless receivers use a 60 Hz fallback because they have no physical display.
- Codec compatibility uses the existing SDP codec negotiation. Runtime decode and encode cost constrain the target when either device is overloaded.
- GCC/TWCC owns bitrate. The viewer reacts to sustained loss, jitter-buffer queueing, receiver drops, and fresh-source versus encoded frame counts. A static source alone is not evidence of overload.
- Three consecutive bad one-second samples reduce offered fps; downward changes have a five-second cooldown. Ten healthy active samples allow a recovery probe, with a ten-second cooldown.
- Accepted targets control Viz capture, WebRTC sender limits and the hardware encoder. Hardware target changes produce new SPS/PPS and an IDR. Stats expose the target and adjustment reason.
- Default bitrate floor is 300 kbps, with explicit administrator overrides preserved. The Intel runtime's actual supported bitrate is taken from readback; a discovered floor is remembered to avoid repeated rejected resets. Other encoder parameter validation remains strict.

## Real-chain checks

Continuous DOM article scrolling at 360 CSS px/s, LPAC browser, shared-fence capture, oneVPL hardware encoder, headless Edge receiver. Actual captured size **1890 x 938**; these are decoded-throughput measurements, not physical-display/scanout measurements. The normal harness warmup is three seconds; full-run means retain remaining cold-start and adaptation transients. Last-ten-sample means show later behavior and can differ slightly from nominal rates because of sampling boundaries.

| Check | Measurement duration | Full-run mean decoded fps | Last 10 samples mean | Accepted target at report | Receiver framesDropped |
|---|---:|---:|---:|---:|---:|
| Explicit 30 fps administrator ceiling | 25 s | 28.47 | 29.99 | 30 | 0 |
| Automatic, normal loopback | 25 s | 53.45 | 60.01 | 60 | 0 |
| 4 Mbps, 25 ms one-way delay, 5 ms jitter | 45 s | 57.03 | 59.91 | 60 | 0 |
| 1 Mbps, 25 ms one-way delay, 5 ms jitter | 45 s | 22.64 | 30.30 | 30 | 0 |

The 30 fps and 4 Mbps rows were measured before lowering the default bitrate floor and adding raw-source feedback. The normal automatic and 1 Mbps rows were rerun on the final implementation. All listed runs used oneVPL; intermediate rejected builds that fell back to OpenH264 are not listed as passes.

In the final 1 Mbps run, the target stepped 60 -> 30 -> 20, then probed back to 30. At report time sender throughput was 29.96 fps and 0.72 Mbps. The relay recorded 83 queue-dropped packets, mostly during adaptation; zero receiver `framesDropped` does not imply zero network loss. Probing continues, so a later target can change again. The driver's nominal target floor at 30 fps was 924 kbps; actual VBR traffic was measured independently.

The first local HTTP fixture attempt was not a valid dynamic benchmark under LPAC. Successful checks used local fixture files inside the permitted dedicated test profile. No isolation protections were disabled for the benchmark.

Raw reports, CSV and logs: `C:/src/tmp/rk-frame-rate/`; the latest file for each case is indexed by `summary.json`. Reproduction driver: `C:/src/tmp/rk-frame-rate/run-check.mjs` (accepts `fps30`, `fps60`, `network4m`, or `network1m`).

## Verification and limits

The native endpoint builds successfully. Viewer production build, TypeScript check, ten tests, and `git diff --check` pass. Tests cover refresh estimation with missed callbacks, capability intersection, static content, sender dropping, decoder/encoder pressure, queueing, loss, recovery, hidden tabs and reconnect reset.

These short tests do not establish long-run stability, real Wi-Fi behavior, physical display latency, or NVIDIA hardware support. Thresholds are heuristics. This path still preserves resolution, so a constrained connection can reduce picture quality and frame rate. High-refresh physical monitors were covered by estimator/controller tests rather than hardware measurements.
