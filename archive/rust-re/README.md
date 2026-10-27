# Rust re (archived, rolled back)

An attempt to rewrite the endpoint "re" in Rust: `re-encode` (oneVPL / NVENC / OpenH264 behind one
Rust API), `re-mojo` and `re-rtc` over a C++ `re_native` shim (Mojo bridge + libwebrtc session behind
a versioned C API, `native/`), plus the `re` binary. `20-re-native-target.patch` is the Chromium
GN hook that built `re_native.dll`; it was dropped from the rb series.
Rolled back by user decision: no clear advantage, and the shim still required the Chromium build tree.
The in-tree C++ endpoint (now `re/`) is the endpoint; its encoder layer reuses the NVENC / parameter ideas.
Last A/B (scenario S, d25j5-c12, 3 runs each, median P50/P80/P99 ms): capture->decoded legacy 70.8/75.0/82.1
vs Rust 60.1/67.5/81.0; input->decoded legacy 114.2/121.9/142.9 vs Rust 121.2/131.0/148.4
(evidence C:\src\tmp\m6\re\matrix.json, not in repo).
