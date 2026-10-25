> **SUPERSEDED / INCOMPLETE** (run killed in a cancelled attempt). Use research/b1-frame-path.md, b2-capture.md, b3-pacing.md (frames) and c1-input-layer.md, c2-ime-osk.md (input) instead.

# Windows unified headless: frame export and pacing

Research date: 2026-10-25. Scope: task b, source investigation only; no Chromium build or runtime measurements.
Stable source pin: **157.0.8103.15**, commit **`1aa07b638799e59656559ae6c6649d4b197f555e`** (M157).
This report is being developed incrementally within the bounded research session.

Evidence labels: **Verified** = implementation read at the stable pin; **Prior art** = read-only patches/docs from the earlier project; **Inference** = design or expected behavior requiring measurement.

## Scope and evidence

The project brief is `design/00-brief.md`; prior-art baseline is `research/a-prior-art.md`.
The earlier project's `docs/prts/` and patches are read-only. Its M158 base is not the stable source pin.
Only this report is written by this task. Retain the native HWND, Windows Aura, and normal Chrome Views UI.

## Recommendation

Provisional: capture each browser/popup Aura root through Viz video capture, keep the existing cloaked HWND output path, and compose exported root textures in the endpoint before NV12 conversion/encoding.
Prefer the existing shared-image capture path before considering a custom output device.

## Open questions / spikes to verify

- Confirm actual stable source APIs, GPU output device selection, capture formats and pacing control.
- Measure capture-to-export latency and popup/resize correctness on UHD 630.
