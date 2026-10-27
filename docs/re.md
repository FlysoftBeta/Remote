# re (endpoint)

`re` is the C++ endpoint: source in `re/`, GN target `re` (`out\<cfg>\re.exe`), built **inside the Chromium tree** (`src\re` is a symlink to
`remotekit\re`; `//BUILD.gn` pulls it into `gn_all` via patch 14). Build and patch workflow: [browser](browser.md#build-configurations-browserargs).
A Rust rewrite was tried and rolled back (kept in `archive/rust-re/`, [decisions](decisions.md) D17-D19); the earlier shim variant
(`re_native`) is therefore not part of the current design. Component README: [re/README.md](../re/README.md).

## Process structure
One `re.exe` per instance, started by the [control-plane](control-plane.md) (`ENDPOINT_PATH`, `--instance=<id>`) and speaking the framed JSON
signalling set on stdin/stdout (stderr = log; stdout carries only frames, sends are bounded to 8 MiB). The
control-plane supervises endpoints independently, persists their profiles/configuration and restarts crashes with backoff
([management API and data layout](control-plane.md#instances-and-profiles)); `re` owns its rb browser (it launches chrome.exe with the inherited Mojo end, `--rb-remote
--headless=new --screen-info --window-size --disable-legacy-window`, and forwards every `--cr-<switch>[=value]` as `--<switch>[=value]]`).
A fault in one encoder, driver or browser takes down only that instance.

| Source | Role |
|---|---|
| `main.cc` | switches, process setup (message loop, ThreadPool, Mojo as broker), hires 1 ms timer, browser launch |
| `endpoint_link.*`, `browser_session.*` | Mojo link to rb ([protocol](protocol.md#browser-to-re-mojo)); main-sequence owner of the browser; marshals input/IME/window calls |
| `signaling.*` | stdio JSON transport, QPC-derived wall clock shared by probe replies and synthetic markers |
| `peer.*` | libwebrtc PeerConnection (offerer), H.264/VP8 track, `input`/`ctl`/`probe` channels, playout-delay, stats; all session state on the signaling thread |
| `d3d_frame_source.*`, `d3d_native_frame.*` | frame import (open-once cache, fence/keyed-mutex wait), VPP BGRA->NV12 pool, native `VideoFrameBuffer`; import and CPU conversion on separate threads |
| `input_parser.*` | validates and decodes the binary `input` messages into Mojo calls |
| `encoder_params.*`, `video_encoder_backend.h`, `encoder_factory.*`, `vpl_encoder.*`, `nvenc_encoder.cc` | encoder layer |
| `experiment_options.*`, `latency_stats.h`, `synthetic_source.*` | switches/field trials, trace samples, browserless synthetic source (`--synthetic`) |

Threads: main sequence (browser/Mojo), signaling thread (all session state), import queue (Mojo frame import, GPU), encoder thread
(`SyncOperation`), network thread (libwebrtc). Dependencies: `//base`, Mojo, `//third_party/webrtc`, bundled libvpl dispatcher (static),
OpenH264 via `rtc_use_h264`. A non-component rel build needs only the MSVC runtime.

## libwebrtc and Mojo
Details and rationale: [video-pipeline](video-pipeline.md#libwebrtc-integration-re) and D12/D18/D19. Mojo and libwebrtc are linked directly into
`re` (no shim); the `.mojom` is Chromium's `chrome/browser/rb/mojom/rb.mojom`.

## Encoders
`--encoder=auto|vpl|nvenc|openh264` (default `auto`: Intel adapter -> oneVPL, NVIDIA adapter -> NVENC, otherwise OpenH264; an unavailable
choice logs `PRTS <X> unavailable; fallback encoder=openh264`; a bogus value is rejected). On a hybrid Intel+NVIDIA machine `auto` picks
oneVPL (first vendor) even if the browser GPU is NVIDIA. All backends implement `VideoEncoderBackend { name, label, Probe, Create(env,
format, EncoderParams) }` and read one `EncoderParams` set built from the switches; low latency is always on (no B-frames, lookahead,
reorder).

| `EncoderParams` | Meaning |
|---|---|
| width, height, fps | stream geometry |
| min / target / max kbps | rate range (`--min-kbps/--start-kbps/--max-kbps`, defaults 2000/8000/8000) |
| `RateControl` | cbr or vbr (`--vpl-rc`) |
| gop_frames | 0 = infinite, IDR on demand |
| intra_refresh_cycle, slices | `--vpl-intra-refresh`, `--vpl-slices` |
| vbv_frames / vbv_ms | `--vpl-vbv-frames` / `--vpl-vbv-ms` |
| max_frame_factor | frame-size cap as a multiple of the average frame (`--vpl-max-frame-factor`, Init-only) |
| low_power, target_usage | `--vpl-lowpower`, `--vpl-tu` |
| nvenc_preset | `--nvenc-preset=1..3` |
| profile, level | constrained baseline only (`--vpl-profile=cb`; High is gated); level 0 = auto from resolution/fps |

- **oneVPL** (default, tuned): [video-pipeline](video-pipeline.md#encoder-tuned-defaults). Verified on Intel UHD 630.
- **OpenH264**: stock WebRTC encoder, fallback of last resort.
- **NVENC** (`nvenc_encoder.cc`): implemented against SDK 12.1 headers (`re/third_party/nv-codec-headers`, MIT); loads `nvEncodeAPI64.dll`
  from System32 at runtime; D3D11 NV12 input registered from the native frame pool texture (after fence wait + VPP, same device) or an
  I420->NV12 upload when capture is on another adapter; P1-P3 + ULTRA_LOW_LATENCY, CBR/VBR, infinite GOP, intra refresh, slices, VBV, IPPP, sync
  mode, seamless rate reconfigure, device adoption/resize like VPL. **UNTESTED ON HARDWARE**: no NVIDIA GPU was available; only the compile
  and the no-GPU fallback ran (`PRTS NVENC probe failed: no NVIDIA adapter present` -> `fallback encoder=openh264`). Not validated:
  zero-copy ordering against VPP, reconfigure acceptance, SPS level/profile, performance.
- The offer prefers H.264 constrained baseline (`42e01f`, packetization-mode=1), followed by VP8 for viewers without usable H.264.
  VP8 uses libwebrtc's libvpx software encoder regardless of `--encoder`; both codecs retain the configured bitrate ceiling (8 Mbps by
  default), `LowDelayOutput` and playout-delay metadata. `PRTS codec selected=...` logs encoder creation; sender/viewer stats confirm the
  codec in use. `--v=1` logs the exact offer SDP to stderr for negotiation diagnostics.
- Software fallback wrapper, `LowDelayOutput` and `SetPlayoutDelay` live in `encoder_factory.*`. The actual backend and input path are
  reported per run (`PRTS encoder selected=...`, report `pipeline.encoder`); always verify them instead of trusting the request.

## Configuration and switches
| Group | Switches |
|---|---|
| Launch | `--instance`, `--chrome=<exe>`, `--user-data-dir`, `--url`, `--window-size`, `--cr-<switch>` (forwarded to chrome, e.g. `--cr-enable-logging`, `--cr-fake-vsync-rate=60`, `--cr-remote-debugging-port`), `--synthetic` (no browser) |
| Isolation / limits | `--isolation=off\|job\|lpac\|appcontainer` (default off), `--job-ui-limits=none` (see [isolation](isolation.md)); `--job-memory-mb=1..1048576` (whole browser job committed memory in MiB), `--job-cpu-rate=1..10000` (Windows hard cap in 0.01% units of all CPUs), `--job-process-count=1..65535` (whole browser job process count). Resource limits are opt-in and require isolation other than off |
| Video | `--fps` (30/60, default 60), `--min-kbps --start-kbps --max-kbps`, `--capture-sync=fence` (default) or `mutex` |
| Encoder | `--encoder`, `--vpl-rc --vpl-max-frame-factor --vpl-vbv-frames --vpl-vbv-ms --vpl-tu --vpl-lowpower --vpl-intra-refresh --vpl-slices --vpl-profile`, `--nvenc-preset` |
| WebRTC | `--playout-delay=0,0` or `none`, `--fec=off`, `ulpfec`, `flexfec` (ULPFEC disables generic NACK), `--webrtc-field-trials=...` (e.g. `WebRTC-Video-Pacing/factor:2.5/`) |
| Timing | hires 1 ms timer on by default, `--no-hires-timer` for A/B |

Switch values and the old `--vpl-*` names are unchanged by the encoder refactor. Frame-size factor is applied at Init and retained on Reset.
Job limits apply to the browser tree, not the endpoint/control-plane. They preserve `KILL_ON_JOB_CLOSE`; invalid limits fail launch.
Too-small memory/process limits can prevent Chromium startup and cause CP backoff. An omitted limit leaves it uncapped.
EOF on stdin requests clean endpoint shutdown; CP falls back to killing the tree when shutdown exceeds its grace period.
Each managed instance receives its own profile under `<CP_DATA_DIR>/profiles/<profile-id>`; CP prevents concurrent use within that manager.

## Trace
`RB_TRACE=1` in the environment makes `re` log per-frame and per-input `PRTS TR ...` lines (TimeTicks in microseconds, bounded to the last
five seconds; Windows TimeTicks is consistent between the browser and `re`): capture/Mojo/copy/convert, encode start, encoded, DataChannel
receive/parse, Mojo key post. The latency harness correlates them with relay (`RELAY_TRACE=1`) and viewer traces (`--trace`); stage results:
[stage-table](research/m3/stage-table.md), [trace](research/m3/trace.md).

## Known issues
First-run `Browser Mojo disconnected / Hello timed out` was seen twice after a chrome.dll relink and not reproduced; GPU restart handling is in
patch 18; release `re.exe` dependency check (dumpbin) is still to do ([tools](tools.md#release)).
