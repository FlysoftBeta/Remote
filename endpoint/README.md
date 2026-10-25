# endpoint (placeholder)

The browser-endpoint is C++ built inside the Chromium tree as the `prts_endpoint` target
(libwebrtc at the pin + a custom oneVPL `VideoEncoder`); see `design/REVIEW.md` and `design/01-architecture.md`.
Its sources live in the patch series under `chromium/patches/` and are created in Stage 3 (M1).
The Rust skeleton from M0 was removed after the clean-room review.
