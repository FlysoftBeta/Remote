# re (endpoint)

Built in the Chromium checkout as `re` (libwebrtc plus oneVPL, OpenH264 fallback). Sources live here; the checkout symlink `re` points to this directory. Browser IPC uses `chrome/browser/rb/mojom/rb.mojom`.

Build and patch workflow: [browser/README.md](../browser/README.md). Current architecture: [docs/re.md](../docs/re.md).

## Encoders

`--encoder=auto|vpl|nvenc|openh264` (default `auto`: Intel adapter -> oneVPL, NVIDIA adapter -> NVENC, else
OpenH264; an unavailable choice logs `PRTS <X> unavailable; fallback encoder=openh264`). All backends implement
`VideoEncoderBackend` (`video_encoder_backend.h`) and read one `EncoderParams` set (`encoder_params.h`: resolution,
fps, min/target/max bitrate, CBR/VBR, GOP/intra refresh, slices, VBV, frame cap, low-latency flags, profile/level),
built from the unchanged `--vpl-*`, `--min/start/max-kbps` switches (plus `--nvenc-preset=1..3`).
The NVENC backend (`nvenc_encoder.cc`, header in `third_party/nv-codec-headers`, MIT) loads `nvEncodeAPI64.dll` from
System32 at runtime and is **untested on hardware** (no NVIDIA GPU was available; only the fallback path ran).
