# str0m partial evaluation (Stage 6 spike, abandoned)

Short copy of the evidence from the spike notes (2026-10-27, str0m 0.24.1, Rust 1.99.1, OpenH264 sender). The spike was stopped when the
user chose to keep libwebrtc ([decisions](../../decisions.md) D17); no A/B verdict was reached.

- Feature fit: native `Writer::playout_delay`, `Extension::PlayoutDelay`, TWCC + BWE, negotiated SCTP channels. Wire inspection showed
  playout-delay ext exactly 0/0 and TWCC on media. Defaults to check against tuned libwebrtc: pacer factor 1.1 (libwebrtc tuned: 2.5),
  queue limit 2 s.
- Interop: Edge 156 and genuine Chrome 89.0.4389.90 negotiate and decode; the Chrome 89 unordered `maxRetransmits=0` probe channel
  round-tripped (77.7 ms under d25j5-c12 contention). Native SPS profile 66, level 42 (constrained baseline).
- Baseline of the C++ endpoint taken for comparison (scenario S, d25j5-c12, two 60 s runs, ms P50/P80/P99): capture->decoded
  85.4/90.1/98.6 and 69.5/74.0/81.4; input->decoded 126.5/138.0/170.7 and 109.1/121.6/139.2; marker loopback capture->decoded
  33.8/35.3/39.8, input 37.5/49.7/69.6; no freezes >200 ms. Run-to-run variation of 15.9 ms at P50 rules out claiming a few ms
  difference without repeated controlled A/B.
- Invalid for timing: the first Rust runs ran with host CPU at 100% (16 clang-cl plus a rustc); kept only as compatibility smoke.
  Not established: high-rate behaviour of the str0m BWE/pacer against the shaped link (the C++ `--synthetic` stream is only 0.35 Mbps;
  a pre-encoded 6 Mbps replay was being prepared).
- Raw artifacts (not in repo): `C:\src\tmp\m6\spike\` (NOTES.md, baseline-*, lab-server.mjs, summarize.mjs).
