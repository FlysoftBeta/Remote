# re-rtc

Safe Rust wrapper over the pinned official libwebrtc in `re_native.dll`. The
checked-in C bindings live in `src/ffi.rs`; Cargo never invokes GN or bindgen.
Rust owns signaling transport, encoder parameters, input/IME routing, and process
supervision. C++ owns peer connections, the external encoder adapter, RTP
extensions, DataChannels, field trials, and WebRTC stats.

## Build

Create `C:/src/chromium/src/re_native` as a **directory symbolic link** to
`remotekit/re/native` (siso rejects a junction). Apply the browser patch series,
including `20-re-native-target.patch`. An elevated PowerShell can create the link:

```powershell
New-Item -ItemType SymbolicLink -Path C:/src/chromium/src/re_native -Target C:/Users/Flysoft/Desktop/remotekit/re/native
./browser/cr.ps1 build dev re_native
cargo build --manifest-path re/Cargo.toml -p re-rtc --example re-rtc-probe --release
```

`out/dev` is a component build for development. Configure the absolute DLL path;
the wrapper registers its directory for Chromium's dependent component loads:

```powershell
$env:RE_NATIVE_DLL = 'C:/src/chromium/src/out/dev/re_native.dll'
$env:ENDPOINT_PATH = "$PWD/re/target/release/examples/re-rtc-probe.exe"
$env:ENDPOINT_ARGS = '--width=1920 --height=1080 --fps=30'
node control-plane --port 8360
```

Open `http://localhost:8360/`. The probe uses `re-encode`'s OpenH264 backend to
generate moving I420 bars, answers ctl hello, echoes binary input and probe
`{t:"echo",nonce}`, and implements the normal QPC-calibrated ping/pong. It is a
test endpoint. Real input/IME routing belongs to the Rust supervisor.

For distribution, use the existing **noncomponent** release configuration:

```powershell
# After the orchestrator's current release job has finished:
./browser/cr.ps1 gen rel
./browser/cr.ps1 build rel re_native
$env:RE_NATIVE_DLL = 'C:/src/chromium/src/out/rel/re_native.dll'
```

With `is_component_build=false`, WebRTC and Chromium libc++ link into this DLL.
The Rust process uses only the C ABI. `out/dev/re_native.dll` is not the standalone
distribution artifact. The release build and its final import table have not
been exercised by this task. Build/ship a matching DLL when the mojom or WebRTC
pin changes; keep ordinary system/VC runtime prerequisites available.

## Contract

- `Session::from_env(Config::default())` loads ABI 1 from `RE_NATIVE_DLL`.
  One canonical DLL path is supported per process, cached and retained until
  exit. Native C APIs are declared in `re/native/include/re_native*.h`.
- `Session` is thread-affine (`!Send`, `!Sync`). All Rust calls and drop run on
  its creating thread. Native callbacks copy data into owned `Event`s; they do
  not execute application code. Drain `poll` promptly. The 2048-event queue is
  bounded; overflow returns an error requiring session teardown.
- Offer/answer/ICE events map directly to design/06 signaling messages. Keep
  session IDs and stale-message rejection in Rust. C++ queues up to 512 ICE
  candidates until the remote description is set. Async failures arrive as
  `Event::Error`; an accepted API call does not mean negotiation has finished.
- Defaults match the legacy peer: constrained-baseline `42e01f` SDP with
  level-asymmetry, H.264 packetization mode 1, min/start/max 2000/8000/8000 kbps,
  30 fps, fluid content hint, maintain resolution, no FEC, TWCC/send-side BWE,
  pacing factor 2.5, negotiated playout-delay and per-frame `(0,0)` metadata.
  Rust encoders must put the honest level in SPS (4.2 at 1080p60).
- Submit a complete Annex-B AU, IDR with SPS/PPS, original strictly increasing
  Windows TimeTicks microseconds, dimensions and trace times. Bytes are copied
  before return and never converted to raw pixels in C++. `FrameTiming` zero
  fields mean unavailable. `now_us` uses the same clock as browser capture.
- `Event::Rates` reports bps and WebRTC's FPS value; zero bitrate means pause
  raw production. Update encoder parameters and coalesce `Event::Keyframe`
  requests until an IDR is produced. Keep the raw capture cadence independent
  of WebRTC's observed input FPS to avoid a feedback loop.
- Drop raw frames **before** encoding. The adapter forces
  `WebRTC-FrameDropper/Disabled/` even when custom trials are supplied. It keeps
  the legacy untrusted-rate-controller flag and detects gaps caused by other
  WebRTC queues, requests IDR, and suppresses dependent pictures until recovery.
  Never discard a successful reference AU between the Rust encoder and submit.
- Channels 0/1/2 are negotiated automatically: input/ctl ordered reliable,
  probe unordered with `maxRetransmits=0`. Extra negotiated channels can be
  created before the offer. `send` copies bytes, caps messages at 64 KiB, and
  reports backpressure when not open or buffered above 64 KiB. C++ passes raw
  messages to Rust, including receive wall-clock and TimeTicks timestamps.
- `request_stats` asynchronously returns JSON
  `{"rtc":[...RTCStatsReport...],"native":{"submitted":N,"handed":N,"suppressed":N}}`.
  `Event::Frame` gives RTP/capture/handoff correlation. `RB_TRACE=1` retains
  legacy `PRTS TR E/X` trace lines and rolling latency stage logs.
- The shared native runtime initializes base/thread pool, logging, and the
  legacy high-resolution timer settings once. RTC owns its Winsock/SSL setup.
  Mojo and RTC use this same runtime; destroy sessions before releasing callback
  storage, and never unload the module while the process is running.

## Focused verification

```powershell
cargo test --manifest-path re/Cargo.toml -p re-rtc --lib --release
cargo test --manifest-path re/Cargo.toml -p re-rtc --test native --release -- --ignored
cargo clippy --manifest-path re/Cargo.toml -p re-rtc --all-targets --release -- -D warnings
node re/crates/re-rtc/examples/verify.mjs --width=1920 --height=1080 --fps=60 --drop-frame --out=C:/src/tmp/m6/rtc-native/edge-1080
node re/crates/re-rtc/examples/verify.mjs --chrome89 --width=1920 --height=1080 --fps=30 --out=C:/src/tmp/m6/rtc-native/chrome89-1080
```

The fixture uses the existing viewer and framing module with local test
signaling and an IPv4 UDP bridge. It counts rVFC callbacks, decoder stats, echo,
BWE callbacks, RTP playout-delay bytes and TWCC. `--drop-frame` discards one
steady-state frame including RTX to exercise PLI recovery; `--no-playout-delay`
checks the off path. It writes SDP, endpoint/browser logs, `result.json`, and the
first encoded AU. Chrome 89 uses its existing compatibility flags and a fresh
owned profile. Only fixture-owned processes are terminated. These are decode
and ABI checks, not a release latency or sustained-throughput comparison.
