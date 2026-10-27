# Robustness checks

Windows / Node 22+ harness for a real endpoint selected by `ENDPOINT_PATH` and
`ENDPOINT_ARGS` (or `ENDPOINT_ARGS_JSON`). No builds or external test services.
The worker **imports `tools/latency/run.mjs`** for the control-plane, Edge launch,
fixture and viewer automation. The viewer's existing `RKMeasure` and `RKClock`
provide decoded pixel-marker latency. This also keeps the soak driver independent
of the endpoint implementation: a different endpoint build must implement the same stdio and
Chrome launch options as `re.exe`.

```powershell
$env:ENDPOINT_PATH='C:/src/chromium/src/out/dev/re.exe'
$env:ENDPOINT_ARGS='--chrome=C:/src/chromium/src/out/dev/chrome.exe --encoder=vpl --fps=60 --cr-fake-vsync-rate=60 --cr-enable-logging'
node tools/soak/run.mjs --all --out C:/src/tmp/m6/soak/nightly-1
# Short check of every fault (typically several minutes):
node tools/soak/run.mjs --all --smoke --port 8460 --out C:/src/tmp/m6/soak/smoke-new
```

For paths with spaces, use an argument array:

```powershell
$env:ENDPOINT_ARGS_JSON=ConvertTo-Json -Compress -InputObject @('--chrome=C:/src/chromium/src/out/dev/chrome.exe','--encoder=vpl','--fps=60')
```

Each launch replaces `--user-data-dir=`, `--cr-remote-debugging-port=` and `--url=`
with dedicated values. Use `--key=value` for these switches. Other endpoint
arguments pass through unchanged; tuning flags are not imposed. The default workload is S (scrolling); `--scenario V` selects the local
moving-pattern video fallback. Both draw a timestamp marker.

Select any combination of these flags; `--all` runs them in the following order:

| Flag | Default | Criterion |
|---|---|---|
| `--long-run` | 5,400 seconds, sample every 30 seconds | Every window >=1 fps with valid decoded marker latency; no freezes >500ms. After >=15 min, regression slopes <=100 MiB/hour and <=100 handles/hour for each process group. |
| `--reconnect` | 5 close/reopen cycles | Each new viewer tab decodes its first frame within 30s. The headless Edge process stays open with a temporary blank tab. |
| `--browser-crash` | Kill owned browser root PID | New frames within 30s, or endpoint/session removed with an explicit diagnostic. Then a harness/operator chain restart must decode. |
| `--endpoint-crash` | Kill owned endpoint PID | CP health clears endpoint/session; observe automatic recovery for 30s; a harness/operator chain restart must decode. |
| `--gpu-restart` | Kill owned Chrome GPU PID | A replacement GPU PID and three new viewer frames within 30s. |
| `--concurrent` | 3 sessions, 30s observation | Independent CP/endpoint/browser profiles and ports all continue decoding with valid markers. |
| `--resize` | 60s, alternate viewport at ~10 Hz | End at 1280x720 viewport; decoded dimensions equal the video rectangle in device pixels and three new frames arrive within 30s. |

Duration/count controls: `--long-secs`, `--sample-secs`, `--resize-secs`,
`--reconnects`, `--sessions` (2 or 3), `--concurrent-secs`, `--recovery-secs`,
`--startup-secs`. A hard wall-clock `--budget-secs` defaults to 7200 (540 for
`--smoke`), writes partial evidence and cleans up on expiry. This budget includes
startup and cleanup; it does not shorten the 90 minute measurement silently.
Smoke defaults: 16s long run / 4s samples, 4s resize, one reconnect, two concurrent
sessions for 5s, 6s recovery and 45s startup. Smoke leak checks are `partial`, not a
leak pass. Change `--max-private-mib-hour`, `--max-handles-hour`, `--max-freezes`,
`--min-fps` to adjust explicit acceptance limits.

`--port 8400` reserves CP 8400, viewer CDP 9400, remote CDP 10400. Concurrent
sessions add 10 per slot. Existing listeners cause failure; the harness never
attaches to an existing CP. `--viewer-exe` overrides installed Edge. Each scenario
uses fresh profiles and stops only descendants of its own worker. Faults and
cleanup check recorded PID **and creation time** before stopping a process;
there is no name-based kill, kill.sh, build, or out/rel interaction. Before
measurements, existing siso/clang-cl/lld-link processes are set to Idle and recorded
in the JSON as required for this workspace. Concurrent compiler activity remains
a source of scheduling noise.

Each output directory contains:

- `soak.json`: incremental scenario status/criteria, configuration, endpoint args,
  build-process priority, samples, fault target identities and diagnostics.
- `soak.md`: compact pass/fail/partial table with numbers and hourly slopes.
- Per-chain `chain.log`, latency runner viewer stderr, preserved profiles and
  `samples.jsonl`. Output directories are never reused or deleted automatically.

Samples include cumulative inbound frames decoded, frames dropped, packets lost,
per-window fps/payload bitrate, freezes exceeding 500ms, and window decoded-latency
P50/P99/sample count. Freezes use rVFC interarrival and a 100ms watchdog so an
unrecovered final stall is counted. Private bytes and handle counts are summed for
the remote browser tree; endpoint metrics cover its own process separately.
Hourly slopes use least-squares regression over sample timestamps, not just the
first/last delta. Startup settling is 3.2s after first valid marker; this is not a
long-term warmup. Slopes from short runs are noisy and remain unassessed below
15 minutes. Exit 0 means no scenario failed (a shortened long run can be partial);
exit 1 means a scenario/cleanup failed, 124 means budget expiry, 130/143 interrupt.
Inspect the statuses before treating exit 0 as a full soak pass.

Current limits:

- The CP has one auto-launched endpoint and no endpoint respawn policy. The
  endpoint-crash scenario reports automatic recovery separately from the explicit
  operator restart, which restarts the owned CP+endpoint chain. A `pass` can mean
  clear failure reporting plus successful operator restart; it does not imply HA.
- Concurrent checks exercise separate CP instances. Shared-CP instance routing
  exists, but the CP launcher does not provision multiple native endpoints.
- GPU memory is omitted: no cheap/reliable per-process GPU counter is assumed.
  `undecodableH264Frames` is null because standard receiver statistics do not
  expose that exact counter. Pixel-marker failures and dropped-frame deltas are
  reported separately; neither is labelled an undecodable H264 count.
- Measurements are loopback/headless, source marker to decoded frame presentation,
  not physical display latency. S/V are deterministic local fixtures. There is no
  network impairment, real media service, or real display in this check.
- A hard process termination cannot run cleanup; interrupted JSON/logs remain in
  the output directory. Prefer Ctrl+C or the budget timer. Browser profiles and
  logs can be large; remove the output directory manually after review.
