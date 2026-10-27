# remotekit

A cloud browser: a lightly patched Chromium (**rb**) runs headless on a Windows host, its windows are captured on the GPU, encoded to H.264
by an in-tree C++ endpoint (**re**, libwebrtc + oneVPL) and streamed over WebRTC to a Chrome 89+ **viewer**; keyboard, mouse, touch and IME
travel back. A Node **control-plane** handles instances and signalling. Product name PRTS; web pages see "Google Chrome".
Goal: 1080p, capture->decoded P80 < 100 ms / P99 < 200 ms at RTT 50 ms and 10-16 Mbps ([brief](docs/brief.md)).

| Part | Path | Doc |
|---|---|---|
| rb, the browser (Chromium 157 + patch series) | `browser/` | [docs/browser.md](docs/browser.md) |
| re, the endpoint (C++, in-tree, libwebrtc, oneVPL / OpenH264 / NVENC) | `re/` | [docs/re.md](docs/re.md) |
| control-plane (Node sample) | `control-plane/` | [docs/control-plane.md](docs/control-plane.md) |
| viewer (Chrome 89+) | `viewer/` | [docs/viewer.md](docs/viewer.md) |
| tools (latency, netem, soak, field-test, encoder-tuning, release) | `tools/` | [docs/tools.md](docs/tools.md) |

## Quickstart from a release
Windows 10/11 x64 with an Intel GPU (oneVPL; otherwise the endpoint falls back to OpenH264). Release packages are built by
[tools/release](tools/release/README.md) (portable folder with rb, re, control-plane, bundled Node, viewer).
1. Extract the archive to a writable directory.
2. Run `start.cmd` (or `powershell -ExecutionPolicy Bypass -File .\start.ps1`; `-Port` changes the port).
3. Open <http://localhost:8080/> in Chrome 89+ or Edge. Ctrl+C in the launcher stops everything.

## Build from source
Windows with Git, depot_tools, Python 3 and Visual Studio. Fetch, patch and build Chromium with `browser\cr.cmd` (`init`, `apply`, `gen`,
`build dev chrome`, `build dev re`); `re` is built inside the Chromium tree. Details: [browser/README.md](browser/README.md),
[docs/browser.md](docs/browser.md). Run the chain for measurements with the [latency harness](docs/tools.md#latency-harness).

## Status and limitations
- Working end to end: capture, input (mouse, keyboard, touch, IME and soft keyboard), WebRTC video, tuned oneVPL encoding, GPU-restart recovery.
  Capture->decoded meets the target under the d25j5 profile; input->decoded does not yet (146-160 ms P80 on a shaped 12 Mbps link;
  [overview](docs/overview.md#latency-budget-and-measured-numbers)).
- NVENC backend: implemented, **untested on hardware**. Only Intel UHD 630 was verified.
- Isolation between tenants ([docs/isolation.md](docs/isolation.md)): planned, not implemented. One viewer controls an instance at a time.
- No audio. Widevine is bundled without a Google VMP signature (L3). The control-plane is a debugging sample (token placeholder auth, no respawn policy).
- Windows only.

## Documentation
[docs/overview.md](docs/overview.md) (architecture, frame and input paths, latency), [docs/decisions.md](docs/decisions.md) (decision log),
[docs/protocol.md](docs/protocol.md), [docs/video-pipeline.md](docs/video-pipeline.md), [docs/research/](docs/research) (evidence notes),
[archive/](archive/README.md) (history).
