# Real endpoint preset

`--preset m3-combined` launches the control plane and real endpoint with VPL, 60 fps capture, `--cr-fake-vsync-rate=60`, the default high-resolution endpoint timer, and:

```text
WebRTC-Video-Pacing/factor:2.5/WebRTC-Bwe-LimitPacingFactorByUpperLinkCapacityEstimate/Enabled/
```

M4 endpoint start/max defaults are now **8000 kbps**. Shared-fence capture remains the probed default. S5 external/demand BeginFrame pacing was rejected after phase and lifecycle experiments and is not enabled in the retained code; the final matrix uses the ordinary browser clock. See [S5 pacing and fallback results](../../research/m4/s5-pacing.md).

Stop the previous test chain first. Set `ENDPOINT_PATH` and `ENDPOINT_ARGS` as for the ordinary control plane, including the Chrome executable and a dedicated browser profile. PowerShell example from the repository root:

```powershell
$env:ENDPOINT_PATH = 'C:/src/chromium/src/out/dev/prts_endpoint.exe'
$env:ENDPOINT_ARGS = '--chrome=C:/src/chromium/src/out/dev/chrome.exe --user-data-dir=C:/src/tmp/m3/matrix2/adhoc-profile --cr-enable-logging --cr-remote-debugging-port=9333'
$env:PTRACK_TMP = 'C:/src/tmp/m3/matrix2/adhoc'
node tools/latency/run.mjs --preset m3-combined --scenario S --port 8300 --secs 60 --netem d25j5-c12 --remote-cdp 9333 --viewer-window-size 1928,1162 --endpoint-log C:/src/tmp/m3/matrix2/adhoc-chain.log *> C:/src/tmp/m3/matrix2/adhoc-chain.log
```

Create the parent output directory before redirecting the log. The control plane's existing space-separated `ENDPOINT_ARGS` format requires argument paths without spaces. The preset implies `--no-mock`; scenario defaults to `marker`, or select `S`, `V`, `G`. Add `--headed` for a visible Edge viewer. S/V/G retain the baseline content workloads (V/G canvas animation requests 30 fps); capture and the measurement marker request 60 fps by default. For the M4 E1 comparison, explicit `--fps 30` requests 30 fps capture/encoding while retaining the 60 Hz fake-vsync clock and pacing trials. The preset rejects an already-running control plane, FPS values other than 30/60, or `--no-hires-timer`. Explicit preset settings override equivalent settings in `ENDPOINT_ARGS`.

The VPL rate/IDR fix must be included in the endpoint binary: older binaries rebuilt the encoder on rate changes and could reject a zero driver frame-cap readback, causing software fallback. The report records the actual backend, negotiated limits and latest VPL driver readback; verify these rather than relying on the requested encoder. This preset reproduces a complete pipeline configuration and does not establish an isolated benefit from the pacing trial.

The M3 matrix found one runtime fallback under Wi-Fi shaping: Reset requested 2000 kbps, the driver returned 2005 kbps with warning 5, and strict target-rate readback validation rejected it. The integrated M4 encoder accepts bounded driver rounding and logs requested/actual differences; its 20 s smoke accepted 2000->2005 kbps without fallback. Pipeline reports label mixed runs, preserve the fallback reason and VPL readback, and prefer a logged fallback over a stale sender snapshot.

M4 endpoint tuning switches belong in `ENDPOINT_ARGS`: `--min-kbps`, `--start-kbps`, `--max-kbps` (min <= start <= max), `--vpl-rc=vbr|cbr`, `--vpl-max-frame-factor`, `--vpl-vbv-frames` or `--vpl-vbv-ms`, `--vpl-tu`, `--vpl-lowpower=on|off`, `--vpl-intra-refresh`, `--vpl-slices`, `--vpl-profile=cb`, `--playout-delay=0,0|none`, and `--fec=off|ulpfec|flexfec`. High profile is gated. Frame-size factor is applied at Init and retained on Reset. Reports preserve requested options, actual VPL readbacks and negotiated peer settings in `pipeline.encoder`; verify the actual backend throughout each run. ULPFEC disables generic NACK at this pin, retaining PLI/FIR, so it changes the recovery policy. Results and limitations: [M4 experiments](../../research/m4/experiments.md).

M4 acceptance uses `summary.latDecoded` and `summary.i2decoded` ending at rVFC `presentationTime`. `summary.lat` and `summary.i2display` retain `expectedDisplayTime` glass estimates; real-endpoint `summary.i2p` is the legacy input-flip callback metric. Set `PRTS_TRACE=1`, `RELAY_TRACE=1`, `TRACE_OUT=<task trace directory>`, and pass `--trace` for correlated endpoint/relay/viewer/source traces.

For an alternate viewer, use `--viewer-exe <path>` (alias of `--viewer-browser`) and `--viewer-flags '<switches>'`. Extra flags apply only to the viewer; double-quote values containing spaces within that string. Edge remains the default. Genuine Google Chrome 89 on this Windows 11 host was measured headed with:

```powershell
--headed --viewer-exe C:/src/tmp/chrome89-google/app/chrome.exe --viewer-flags '--user-data-dir=C:/src/tmp/chrome89-google/profile --no-first-run --no-default-browser-check --disable-component-update --disable-background-networking --disable-sync --no-sandbox --enable-features=NetworkServiceInProcess --remote-debugging-port=9444'
```

The last two compatibility switches before CDP were necessary after a plain headed attempt disconnected during negotiation. Reports retain `viewerLaunch` and full viewer stderr is saved beside the CSV as `viewer-stderr.log`. Chrome 89 jitter-buffer counters come from legacy `track` stats; reports preserve that source plus codec `sdpFmtpLine`. See [Chrome 89 acceptance](../../research/m4/chrome89.md) for measurement caveats and results.
