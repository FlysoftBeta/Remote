# Real endpoint preset

`--preset m3-combined` launches the control plane and real endpoint with VPL, 60 fps capture, `--cr-fake-vsync-rate=60`, the default high-resolution endpoint timer, and:

```text
WebRTC-Video-Pacing/factor:2.5/WebRTC-Bwe-LimitPacingFactorByUpperLinkCapacityEstimate/Enabled/
```

Stop the previous test chain first. Set `ENDPOINT_PATH` and `ENDPOINT_ARGS` as for the ordinary control plane, including the Chrome executable and a dedicated browser profile. PowerShell example from the repository root:

```powershell
$env:ENDPOINT_PATH = 'C:/src/chromium/src/out/dev/prts_endpoint.exe'
$env:ENDPOINT_ARGS = '--chrome=C:/src/chromium/src/out/dev/chrome.exe --user-data-dir=C:/src/tmp/m3/matrix2/adhoc-profile --cr-enable-logging --cr-remote-debugging-port=9333'
$env:PTRACK_TMP = 'C:/src/tmp/m3/matrix2/adhoc'
node tools/latency/run.mjs --preset m3-combined --scenario S --port 8300 --secs 60 --netem d25j5-c12 --remote-cdp 9333 --viewer-window-size 1928,1162 --endpoint-log C:/src/tmp/m3/matrix2/adhoc-chain.log *> C:/src/tmp/m3/matrix2/adhoc-chain.log
```

Create the parent output directory before redirecting the log. The control plane's existing space-separated `ENDPOINT_ARGS` format requires argument paths without spaces. The preset implies `--no-mock`; scenario defaults to `marker`, or select `S`, `V`, `G`. Add `--headed` for a visible Edge viewer. S/V/G retain the baseline content workloads (V/G canvas animation requests 30 fps); capture and the measurement marker request 60 fps. The preset rejects an already-running control plane, conflicting `--fps`, or `--no-hires-timer`. Explicit preset settings override equivalent settings in `ENDPOINT_ARGS`.

The VPL rate/IDR fix must be included in the endpoint binary: older binaries rebuilt the encoder on rate changes and could reject a zero driver frame-cap readback, causing software fallback. The report records the actual backend, negotiated limits and latest VPL driver readback; verify these rather than relying on the requested encoder. This preset reproduces a complete pipeline configuration and does not establish an isolated benefit from the pacing trial.

The M3 matrix found one runtime fallback under Wi-Fi shaping: Reset requested 2000 kbps, the driver returned 2005 kbps with warning 5, and strict target-rate readback validation rejected it. The preset does not fix that encoder compatibility issue. Pipeline reports label mixed runs, preserve the fallback reason and VPL readback, and prefer a logged fallback over a stale sender snapshot.

