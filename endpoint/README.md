# Legacy C++ endpoint

Built in the Chromium checkout as `rb_legacy_endpoint` (libwebrtc plus oneVPL, OpenH264 fallback). Sources live here; the checkout symlink `rb_legacy_endpoint` points to this directory. Browser IPC uses `chrome/browser/rb/mojom/rb.mojom`.

Build and patch workflow: [browser/README.md](../browser/README.md). Current architecture: [design/01-architecture.md](../design/01-architecture.md). The Rust endpoint is being developed separately in `re/`.
