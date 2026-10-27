# Two-machine field test

Windows server on wired Ethernet; a phone, tablet, or PC running **real Google
Chrome 89+** on the same subnet, usually over Wi-Fi. Chrome on iOS uses WebKit
and is not the Chrome 89 baseline. The viewer needs no CDP, automation, camera
permission, or TURN server. Use one viewer at a time: a second viewer replaces
the first controller.

## Prepare and start

1. Install Node.js 22+ and the control-plane dependencies once:

   ```powershell
   npm --prefix control-plane ci
   ```

2. Use the patched Chromium 157.0.8103.15 and matching `rb_legacy_endpoint.exe`.
   If an incremental dev build is needed, coordinate with whoever owns the
   running chain first, then use the existing Git Bash helpers:

   ```bash
   bash /c/src/tmp/m1/be.sh chrome
   bash /c/src/tmp/m1/be.sh
   ```

   These helpers stop the test chain. Never run them during another measurement.
   Do not clean, start a full build, or interfere with an ongoing `out/rel` build.

3. From the repository root, start the server with one command:

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\tools\field-test\start-server.ps1 -Scenario S -Seconds 60
   ```

   The launcher chooses `C:\src\chromium\src\out\rel` only when **both** binaries
   exist there, otherwise `out\dev`. To select a known ready build and output root:

   ```powershell
   .\tools\field-test\start-server.ps1 -BinDir C:\src\chromium\src\out\dev -Port 8300 -Scenario marker -Seconds 60 -OutDir C:\src\tmp\field-runs
   ```

   `-Scenario` accepts `marker`, `S` (scrolling article), `V` (30 fps canvas video
   workload), or `G` (30 fps game canvas). This is the source workload, set at
   launch. Editing only the viewer URL's scenario does not change the source;
   a mismatch blocks Start. For additional endpoint switches, invoke from a
   PowerShell prompt with an array, for example `-EndpointArgs @('--cr-v=1')`.
   Paths with spaces work through `ENDPOINT_ARGS_JSON`; legacy `ENDPOINT_ARGS`
   remains supported for other launchers. Preset switches and profile/path
   switches come last and take precedence. The default high-resolution timer
   is required; `--no-hires-timer` is rejected.

4. The command checks prerequisites and that the port is free, prints firewall
   rules and every usable IPv4 interface URL, then starts control-plane, endpoint,
   and Chrome. Choose the wired LAN address reachable by the device rather than
   a VPN/VM interface. A typical URL is:

   ```text
   http://192.168.1.20:8300/?measure=60&scenario=S
   ```

   The process uses a dedicated browser profile for every run. Keep the terminal
   open; Ctrl+C stops that launched process tree. It never kills processes by
   name. A busy port causes an error; it does not stop the existing server.

## On the viewer device

Open the printed URL in Chrome, use landscape orientation, wait for moving
video, then tap **Start 60 s test**. If autoplay is blocked, tap the video first.
There is a three-second warm-up followed by the requested measurement duration.
No repeated tapping is needed: the viewer sends a key through the normal input
DataChannel every 500 ms and detects the resulting green/magenta source patch
in decoded video. Avoid manual keys/taps during the test because they can also
change the patch. Keep Chrome visible and the screen awake; hiding the tab
invalidates the run and requires a reload. A disconnect discards the partial
run; reconnect and tap Start again.

The field panel shows live **P50/P80/P99** for capture→decoded, input→decoded,
capture→display, and input→display, plus sample counts. At completion the summary
is frozen and the JSON is POSTed to `/field-report`. Wait for **Saved: field-…json**.
If upload fails, tap **Retry upload** or **Download report**. A download is also
available after successful upload. Reload the printed URL for another test.

The launcher prints the timestamped run directory, by default:

```text
C:\src\tmp\m4\field-kit\runs\<timestamp>-<launcher PID>\
  run.json            requested settings and binary directory
  stdout.log          control-plane events, report filenames
  stderr.log          endpoint/Chrome diagnostics and encoder fallback evidence
  viewer-urls.txt     URLs for the server's IPv4 interfaces
  pids.json           launcher and control-plane PIDs
  field-*.json        one saved file per viewer test, including per-frame CSV
  chrome-profile\    dedicated profile (may contain Chrome's debug log)
```

## Settings and interpreting reports

The launcher follows `tools/latency/run.mjs --preset m3-combined`: VPL requested,
60 fps capture, `--cr-fake-vsync-rate=60`, default high-resolution timer, and:

```text
WebRTC-Video-Pacing/factor:2.5/WebRTC-Bwe-LimitPacingFactorByUpperLinkCapacityEstimate/Enabled/
```

The receiver uses a zero delay hint by default, matching the harness. There is
no network shaping in this kit; `netem: null` means an actual direct LAN path.
`run.json`/`serverRun` describe **requested** settings. Check `sender.backend` and
the complete stderr log for VPL→OpenH264 fallback, rather than assuming that a
VPL request proves the actual encoder. Field reports cannot query CDP GPU info.

Reports retain the harness schema: `summary.latDecoded`, `summary.i2decoded`,
`summary.lat`, `summary.i2display`, `summary.i2p`, `summary.rx`, each with
`p50/p80/p95/p99/max/n`. They also include `ua`, receiver/sender stats, `pipeline`
with selected ICE pairs, and `field` with user agent, screen/viewport/DPR,
optional network information, and decoder implementation when readable. A
missing decoder name is reported as null; HTTP LAN pages often cannot obtain
the media permission that exposes it. No permission prompt is required.

The primary M4 metrics are **capture→decoded** and **input→decoded**, ending at
rVFC `presentationTime` (decoded frame submitted for composition). Always compare
the old **capture→display** / **input→display** `expectedDisplayTime` glass
estimates alongside them. These are browser timestamps, not measured panel
scanout. The capture metric starts at the source marker draw timestamp, just as
in the existing harness; it includes draw-to-capture phase. `i2p` is callback-time
colour detection, a separate diagnostic, and must not replace `i2decoded`.
Missing presentation metadata produces no decoded samples, rather than a
callback-time substitute. Chrome's
[rVFC documentation](https://web.dev/articles/requestvideoframecallback-rvfc)
describes these timestamps and the best-effort association with sampled pixels.

Compare identical scenario, capture rate, actual codec/backend, source size,
viewer resolution/DPR, browser version, and receiver delay hint with loopback
and `d25j5-c12` reports. Record Wi-Fi band, signal, distance, and other traffic
alongside each run. The M4 target **P80 < 100 ms and P99 < 200 ms** applies to
both decoded metrics **on d25j5**, not to an unshaped LAN test as a substitute.
Repeat runs; differences below about 20 ms are noise unless repeated because
capture phase can lock differently. Clock offset is estimated using minimum-RTT
probe messages; asymmetric Wi-Fi paths can bias capture latency. Input latency
uses the viewer's own clock. Check sample counts, marker failures, min RTT,
freezes, and input timeouts before interpreting percentiles. A 60 s run has only
about 120 input probes, so input P99 is especially sensitive to a few samples.

## Troubleshooting

- **Page will not open:** use the server LAN IP, not `localhost`; verify the
  selected port, `stdout.log`, and Windows network profile. When elevated, the
  launcher adds missing inbound TCP-port and endpoint-program UDP rules scoped
  to LocalSubnet on Private/Domain profiles. Otherwise it prints the exact
  `netsh` commands to run in an elevated terminal. Existing disabled or blocking
  rules are printed and require review. The launcher does not change Public
  network policy. Check corporate firewall policy as well.
- **Page loads but video does not:** TCP signalling can work while UDP media is
  blocked. Allow inbound UDP for the selected `rb_legacy_endpoint.exe` (not just
  `chrome.exe`). Check `stderr.log`, Wi-Fi AP/guest-network client isolation,
  VLAN routing, and endpoint registration in `/api/health`. There is no TURN
  requirement on a directly reachable subnet; this kit supplies no TURN server.
- **ICE addresses:** the endpoint must advertise a reachable wired LAN host
  candidate, not only `127.0.0.1`, a VPN, or a virtual adapter. The viewer may
  advertise `*.local` mDNS host candidates; mDNS resolution/traffic must work
  between the machines. Selected candidate type/address evidence is in
  `pipeline.pairs` after a successful run. If policy hides addresses, inspect
  `chrome://webrtc-internals` on an accessible PC and endpoint logs. Disabling
  Chrome's mDNS hiding policy is a controlled diagnostic, not a kit default.
- **Start unavailable / no samples:** wait for clock sync and a readable colour
  patch; tap the video if paused, reload after orientation changes, and verify
  scenario agreement. The top-left black/white pixel marker must be in the
  captured image. Automatic marker-row detection handles tab chrome; `&my=N`
  explicitly selects a source-pixel row when diagnosing alignment. Lowering
  device load can help readback/CRC failures. `summary.latDecoded.n=0` cannot establish
  decoded latency; `summary.decoded` is the count of valid marker decodes.
- **No saved report:** the terminal must stay running and the output directory
  must be writable. Wait for the saved filename, or use download/retry. Each
  POST has a 16 MiB limit; avoid large optional traces on long runs. Reports are
  local lab files containing device and LAN details; the kit is intended for a
  trusted subnet rather than an Internet-facing service.

## Glass-to-glass check

Use a **240 fps** phone camera to film the **server screen with a local PRTS
window showing the marker page** and the viewer device side by side. Select
`marker`, enlarge the bottom `frame N` label on both screens in the camera view,
keep both labels at the same camera-image height to reduce rolling-shutter
skew, lock camera focus/exposure, and use a short exposure. Do not open another
controller to create the server reference: that replaces the remote viewer.

The reference must show the **same source frames/counter** that are being
captured and streamed. A separately opened marker page has an independent
counter and is not a valid reference. Important limitation: the current
`endpoint/endpoint_link.cc` unconditionally adds `--headless=new`, so this kit
does not itself expose that local PRTS window. Perform this check only with a
separately prepared visible PRTS source/debug mirror that preserves the same
source and has a measured reference delay. That setup remains unverified here;
do not relabel an ordinary independent browser window or a delayed CDP
screencast as a zero-delay reference. Coordinate this separate experiment when
no other task is measuring.

Step through the recorded camera frames. In each sharp image, read source
counter `S` and viewer counter `V`. With a verified constant 60 Hz source,
`(S - V) * 1000 / 60` estimates relative screen lag (for example, 6 source
frames ≈ 100 ms). With dropped/irregular source updates, instead locate the
**same counter value** appearing on each screen and count camera frames between
its first appearances: `cameraFrames * 1000 / 240`. Repeat across many values
and report a distribution. This measures display lag relative to the visible
server reference; account for that reference's own display/mirror delay when
comparing with capture→display estimates. It cannot directly validate the
capture→decoded endpoint, which excludes client presentation/display.

A 240 fps video has 4.17 ms camera sampling intervals. A first-appearance
difference has roughly ±1 camera-frame quantization under ideal exposure;
source/viewer scanout, rolling shutter, exposure, and any reference mirror add
uncertainty. The simultaneous-counter shortcut is coarser, about one 60 Hz
source frame (16.7 ms), and assumes stable source cadence. Treat practical
accuracy as about 10–20 ms or worse unless those factors are calibrated.

## Verification status

This change was checked with `node --check` and the PowerShell parser only.
No control-plane, endpoint, Chrome, firewall mutation, or two-machine test was
run during development because another task owned the active measurement chain.
Device behavior, ICE/firewall reachability, Ctrl+C cleanup, and the visible
source setup for the camera check require a coordinated field run.
