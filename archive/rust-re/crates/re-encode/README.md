# re-encode

Windows x64 encoder layer for `re`, independent of Chromium GN and WebRTC. Rust
owns the API, D3D11 imports/conversion and selector. Small C++17 bridges use the
oneVPL and NVENC headers without duplicating their ABI layouts in Rust.

Build from `re/` with Rust MSVC and the Visual Studio C++/Windows SDK tools:

```powershell
cargo build --release
cargo clippy --release --all-targets -- -D warnings
cargo run --release --bin re-encode-probe -- --out=C:/src/tmp/m6/encoders/probe --upload-complete --check-controls
```

The build compiles the existing `endpoint/third_party/libvpl` 2.17 dispatcher
statically and OpenH264 from the pinned `openh264`/`openh264-sys2` 0.9.9 crates.
No Intel or NVIDIA runtime DLL is bundled. The NVENC header and MIT license are
vendored at `re/third_party/nv-codec-headers` (fixed tag `n12.1.14.0`).

## Integration

```rust,no_run
use re_encode::{create_encoder, d3d::D3dDevice, EncoderChoice, EncoderParams};

let device = D3dDevice::create(None)?;
let encoder = create_encoder(EncoderChoice::Auto, EncoderParams::default(), Some(device))?;
# Ok::<(), re_encode::Error>(())
```

Construct and call an `Encoder` on the encoding queue. `Box<dyn Encoder>` is
intentionally not `Send`: construct it on its destination thread. Serialize
immediate-context access with capture submission and never hold an explicit D3D
multithread lock while encoding. `D3dDevice::from_device` enables D3D multithread
protection and requires `D3D11_CREATE_DEVICE_VIDEO_SUPPORT`.

- `EncoderParams` uses visible dimensions, integer FPS and bits/second. Defaults:
  1080p60, min/target/max 2/8/8 Mbps, VBR, one-frame requested VBV, max-rate
  one-frame cap, constrained baseline, honest automatic level, no periodic IDR,
  refresh off, TU7, LowPower, one reference/slice, no B frames/lookahead.
- `FrameData::Gpu(&GpuFrame)` accepts single-subresource NV12 or BGRA on the exact
  encoder device. Use `import_texture(&OwnedHandle)` and `import_fence` once per
  token and cache them at the capture layer. A fence wait is queued on the GPU
  before source reads. Fence values must be signaled by a live producer.
- `GpuFrame::lease` must prevent producer reuse/overwrite while held. Merely
  retaining the texture's COM object is insufficient. VPL retains the lease until
  its surface `Data.Locked` is zero or the session closes, including across encode
  returns. Deliver the capture consumed/reuse acknowledgement from lease release.
- Padded NV12 (`align16(width/height)`, DEFAULT, RENDER_TARGET) passes to hardware
  without a copy. Other NV12 uses a GPU copy into a padded slot; BGRA uses cached
  BT.709-full to BT.709-limited video processing. These paths retain source leases
  conservatively until the output slot is released. Pools are bounded.
- `FrameData::Cpu(CpuFrame)` is borrowed limited-range I420 with validated plane
  lengths/strides. Hardware repacks/uploads it; OpenH264 consumes it directly.
  Software GPU input includes staging readback and NV12/BGRA conversion.
- `encode` returns `Option<AccessUnit>`: Annex-B bytes, original microsecond
  timestamp and IDR/I/P frame type. Timestamps must increase. `None` is a software
  rate-control skip. Valid encoded references are never dropped for exceeding caps.
- `force_idr` coalesces requests until an actual IDR. Annex-B SPS profile/level,
  IDR SPS/PPS, backend timestamps, and absence of reordered hardware output are
  checked. `params()` describes requested active parameters; `diagnostics()`
  reports driver-effective values and fallback reasons.
- Reconfiguration returns `Seamless`, `NewSequence` or `Unchanged(reason)`.
  Bitrate-only reset preserves effective VBV/frame cap. VPL starts BRC allocation
  at max bitrate; a smaller initial target is then applied by reset. Unsupported
  resets retain the previous configuration. Resize/configuration changes prepare
  a replacement and start IDR. VPL FPS resets are conservatively sequence
  boundaries: UHD630 inserted IDR even with `StartNewSequence=OFF`. NVENC requests
  FPS/bitrate reconfiguration with `forceIDR=0` (hardware verification pending).
- `EncoderChoice::from_str` implements `auto|vpl|nvenc|openh264`. Auto selects
  Intel VPL or NVIDIA NVENC according to the supplied adapter, then OpenH264.
  Explicit choices return errors. Auto retries a failed hardware raw frame as a
  software IDR. Invalid input/pool pressure does not trigger fallback.

## Backend limits

VPL uses Query/Init/GetVideoParam validation, matching adapter LUID, AsyncDepth 1,
external texture allocation and the existing bounded rounding policy. Nonzero
rate values tolerate max(16 kbps, 10%); Init VBV/initial delay allow max(64/32 KB,
10%), reset VBV allows max(2 KB, 10%), and cap allows max(1024 bytes, 10%). Zero
must remain zero. CBR disables LowDelayBRC and frame cap; VBR enables LowDelayBRC.
`FrameCap::TargetFactor` has the current target-rate, Init-only option semantics.

NVENC uses dynamically loaded System32 `NvEncodeAPICreateInstance`, SDK API 12.1,
D3D11 registered/mapped resources, P1/P2/P3 + ULTRA_LOW_LATENCY, synchronous output,
no B frames/lookahead/AQ/multipass, one reference, fixed slice count, repeat
SPS/PPS, infinite or finite GOP, and optional intra refresh. SDK 12.1 has no global
AVC MaxFrameSize: the frame cap tightens the VBV hint, with no byte guarantee.
**Untested on NVIDIA hardware**; initialization failures are recoverable.

OpenH264 uses the current camera-real-time, RC_BITRATE_MODE, four-thread/four-fixed-
slice, one-layer/reference, skip-enabled, low-complexity preset. It supports only
constrained baseline here and rejects intra refresh. Set
`software_screen_content` for the explicit screen override. `software_slices`
defaults to four to preserve the fallback; set it to `None` to use shared `slices`.
Software has no equivalent hardware VBV/global cap and exposes that in diagnostics.
The CPU build's performance can differ from Chromium's optimized OpenH264 build.
Install NASM on PATH before building for the crate's optimized x86 assembly;
without it the crate silently uses substantially slower C++ implementations.
Alternatively set `RE_OPENH264_DLL` to a Cisco binary path. The loader verifies
its SHA-256 against the crate's known Cisco binary list before loading. An
explicit DLL load/verification failure returns an error. No DLL is downloaded or
redistributed by this crate.

## Probe

Default: 300 moving-pattern frames at 1080p60 with each available backend, shared
NT texture/fence imports between two devices on one adapter, `.h264` + per-frame
CSV output, OpenH264 decoding/count/dimension checks and optional ffprobe checking.
Set `RE_FFPROBE` to an executable path when ffprobe is absent from PATH.
Unavailable backends print a clear reason and are skipped by `--encoder=all`.

`--input=nv12|bgra|cpu` selects input. `--upload-complete` excludes upload GPU
completion from encode timing; otherwise its remaining work may be included.
Generation, file writes and decoding are always outside the timer. Software GPU
timing includes staging/conversion. Bitrate uses nominal input duration, and
nearest-rank P50/P99 include startup and skips. The probe runs without pacing.
`--check-controls` separately checks explicit IDR, rate/FPS changes, retained
settings on rejected resets, and resize with an independently decoded IDR.
This is an encoder probe, not a browser/WebRTC end-to-end latency measurement.
