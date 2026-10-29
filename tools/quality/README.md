# Static quality recovery

From the repository root on Windows:

```powershell
.\tools\quality\test-policy.cmd
node tools/quality/static-recovery.mjs --encoder vpl
node tools/quality/static-recovery.mjs --encoder openh264
```

The standalone C++ test covers moving-content priority, zero-rate suspension,
GCC byte budgets, pacer backlog, encode/drop backoff, stalled captures, trusted
and untrusted QP convergence, bandwidth recovery, new damage and reconnection.
It requires the Visual Studio 18 Community C++ toolchain; output goes to TEMP.

The integration test requires a built `re.exe` and adjacent `chrome.exe` under
`C:/src/chromium/src/out/dev` (override with `--endpoint`), plus installed Edge
(override with `--viewer`). It owns an isolated control-plane, browser profile,
viewer and sockets; it closes its process trees after each run.

A canvas fixture paints text and checkerboard exactly once. The endpoint starts
at 300 kbps with an 8 Mbps ceiling. Over 15 seconds, the receiver must decode
refinement frames with no periodic keyframe requests and reduce pixel error by
at least 20%, or maintain an already clear image (MSE below 30). Error is measured
against a source screenshot, excluding the browser toolbar. Viewer disconnection
must stop source delivery. VPL runs must confirm the hardware backend actually
activated. The test then resumes motion, checks that ordinary frame throughput
returns, closes the viewer and reconnects to the static page.
`--max-kbps 300` also tests refinement under a permanently constrained
allocation; this extreme case may stall stock OpenH264's rate controller.

Logs, first/final/source PNGs and per-second samples are written under
`C:/src/tmp/static-recovery-*` (override with `--out`). These are decoded-pixel
checks, not physical display or subjective quality measurements. Network
loss/jitter and packet prioritization are not emulated by this test; the existing
netem/latency harness covers those scenarios.
