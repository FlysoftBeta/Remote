# Tools

Everything under `tools/`. Node 22+ (Node 24 for some scripts), Windows. Measurement conventions: [video-pipeline](video-pipeline.md#measurement).
Temporary output goes to `C:\src\tmp\...`, never into the repo.

## Latency harness
`tools/latency/`. Runs the control-plane, a (mock or real) endpoint and a headless viewer (Edge by default, any Chromium incl. Chrome 89) on one machine, prints
P50/P80/P95/P99 and writes a per-frame CSV plus a JSON report (`pipeline` section: actual encoder/backend and readbacks, ICE pair, viewer launch).

- `run.mjs`: runner. Main options: `--secs`, `--fps`, `--scenario marker|S|V|G`, `--netem <profile>`, `--preset m3-combined`, `--no-mock`
  (real endpoint from `ENDPOINT_PATH`/`ENDPOINT_ARGS`), `--remote-cdp <port>` (GPU info), `--viewer-exe` / `--viewer-flags`, `--headed`,
  `--presenter`, `--trace`, `--endpoint-log`. Full option list in the file header.
- `marker.js`, `clock.js`, `measure.js`: pixel-marker codec, viewer/endpoint clock mapping, rVFC measurement; `scenarios.js`: deterministic load
  fixtures (S scrolling article, V 1080p30 canvas video, G game canvas); `remote-page.html`: the page loaded in the remote browser;
  `pipeline.mjs`: pipeline snapshot per report.
- **Preset `m3-combined`**: real endpoint with oneVPL, 60 fps, `--cr-fake-vsync-rate=60`, default hires timer and field trials
  `WebRTC-Video-Pacing/factor:2.5/WebRTC-Bwe-LimitPacingFactorByUpperLinkCapacityEstimate/Enabled/`; implies `--no-mock`, requires a stopped
  control-plane, rejects fps other than 30/60 and `--no-hires-timer`; explicit preset settings override `ENDPOINT_ARGS`. Details, example
  command and encoder switch list: [tools/latency/PRESETS.md](../tools/latency/PRESETS.md). It reproduces a complete configuration, not an isolated benefit.
- Metrics: acceptance = `summary.latDecoded` and `summary.i2decoded` (end at rVFC `presentationTime`); `lat`/`i2display` are glass estimates
  (`expectedDisplayTime`); `i2p` is the legacy callback detection. Set `RB_TRACE=1`, `RELAY_TRACE=1`, `TRACE_OUT` and `--trace` for correlated traces.
- Related: `tools/mock-endpoint/` (a browser page that plays the endpoint: canvas capture, marker, input echo, fake text field for IME) and
  `tools/smoke/smoke.mjs` (CDP input -> viewer -> WebRTC -> mock endpoint smoke).

## Netem
`tools/netem/`. A minimal UDP TURN relay (RFC 5766 subset) whose relay-to-relay forwarding passes through a per-direction link emulator, so both peers are
shaped once per direction (media, SCTP, STUN). `shaper.mjs`: loss (uniform + Gilbert-Elliott), drop-tail byte queue, rate serialiser, delay +
jitter (+ spikes) on a hybrid-timer scheduler. `relay.mjs --profile <name> [--queue-ms 50] [--seed N]`; `selftest.mjs`; profiles in `profiles.json`:

| Profile | One-way delay / jitter | Rate | Extra |
|---|---|---|---|
| `d25j5-c16`, `-c12`, `-c10` | 25 / 5 ms | 16 / 12 / 10 Mbps | RTT 50 ms |
| `d50j10-c16`, `-c12`, `-c10` | 50 / 10 ms | 16 / 12 / 10 Mbps | one-way 50 ms |
| `d25j5-c12-wifi`, `d50j10-c12-wifi` | as above, 12 Mbps | | Gilbert-Elliott loss (p 0.0025, r 0.25), 30-50 ms spike every 5 s for 100 ms |

`d25j5-c12` is the reference acceptance profile. The harness starts the relay with `--netem <profile>`. Evidence: [netem (m2)](research/m2/netem.md),
[netem matrix OpenH264 baseline](research/m3/netem-matrix-openh264.md).

## Soak
`tools/soak/`. Robustness harness for a real endpoint (`ENDPOINT_PATH`, `ENDPOINT_ARGS`/`ENDPOINT_ARGS_JSON`): imports `tools/latency/run.mjs` for the chain,
launch and viewer automation. Scenarios (`--all` runs them in order): `--long-run` (90 min, memory/handle slopes), `--reconnect`,
`--browser-crash`, `--endpoint-crash`, `--gpu-restart`, `--concurrent` (3 sessions), `--resize`; `--smoke` shortens everything (leak checks
become `partial`). Writes `soak.json` / `soak.md` per run, never reuses output directories, kills only PIDs it owns (PID + creation time
checked). Criteria, ports, budget and limits: [tools/soak/README.md](../tools/soak/README.md).

## Field test
Two-machine test: Windows server on wired LAN, a phone/tablet/PC with real Chrome 89+ on Wi-Fi. `tools/field-test/start-server.ps1` (in `tools/field-test/`) checks
prerequisites, prints firewall rules and every IPv4 viewer URL (`http://<ip>:8300/?measure=60&scenario=S`), starts control-plane, `re` and
Chrome with the `m3-combined` settings (no shaping, so the path is the real LAN). The viewer overlay `viewer-field.js` runs the 60 s test on
tap, shows live P50/P80/P99 for capture->decoded, input->decoded and the glass estimates, and POSTs a JSON report to `/field-report` (saved
under the run directory with per-frame CSV). Includes troubleshooting (firewall, UDP, ICE/mDNS) and a 240 fps phone-camera glass-to-glass
check. See [tools/field-test/README.md](../tools/field-test/README.md).

## Encoder tuning
`tools/encoder-tuning/`. Standalone oneVPL/OpenH264 experiment bench (not product code): `tune.cpp` encodes a packed 1080p NV12 clip with explicit parameters,
`experiment.mjs` / `followup.mjs` / `run-batches.ps1` run reproducible batches, `analyze.mjs` models link metrics, `inspect.mjs` parses
SPS/NAL/slice headers, `audit.mjs` validates, `aggregate.mjs` pools repeats, plus PSNR-Y/SSIM-Y quality. `setup.ps1` prepares media and
dispatcher, `build.bat` builds. Results: [t1-encoder-tuning](research/t1-encoder-tuning.md); setup and parameter table:
[tools/encoder-tuning/README.md](../tools/encoder-tuning/README.md). `tools/s7-vpl-probe/` is the earlier parameter-matrix probe
([s7-onevpl-probe](research/s7-onevpl-probe.md)).

## Release
`tools/release/package.ps1 -Version 0.1.0 -Out <dir> [-ReExe ...] [-ChromeOut ...] [-ReRuntimeDll ...]` assembles a portable folder from an existing
non-component rel build (no builds are run): rb files per Chromium's `chrome.release` patterns (locales, Widevine tree, V8 snapshots, MSVC
runtimes), `re.exe`, the control-plane with locked production npm dependencies, a pinned Node distribution (digest verified), the viewer,
start scripts, third-party credits (`LICENSES.md`). Output: versioned folder + ZIP + `SHA256SUMS.txt` (+ `FILES.sha256`); a version that
already exists is rejected; archives of 1.9 GB or more are split into numbered parts with reassembly instructions. `README.release.md` is the
end-user readme (extract, `start.cmd`, open <http://localhost:8080/>). Remaining before publishing: smoke-test staged rb via `/json/version`,
qualify real re/WebRTC, review licensing, dumpbin check of the rel `re.exe`. See [tools/release/README.md](../tools/release/README.md).

## Isolation helper
`tools/isolation/grant-lpac-acl.ps1` grants read/execute on deployed binaries to sandboxed Chromium processes (run elevated after copying a
component build). Part of the planned [isolation](isolation.md) work.
