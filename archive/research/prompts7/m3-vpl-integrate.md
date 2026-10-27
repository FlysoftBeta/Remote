# Task m3-vpl-integrate (timeout 45 min): land the oneVPL GPU encoder path in prts_endpoint and measure it
Task dir C:\src\tmp\m3\vpl-integrate\. You are the ONLY builder/launcher now. You MAY edit remotekit endpoint/ (incl. vendoring endpoint/third_party/libvpl)
and tools/latency/ if needed. Do not edit C:\src\chromium\src. Incremental builds only (`be.sh`). No commits.
Input: the reviewed draft C:\src\tmp\m2\vpl\{remotekit.diff,NOTES.md,vendor-libvpl.ps1,vendor\} (written against an older HEAD; latency instrumentation
(endpoint/latency_stats.h, d3d_frame_source.cc, peer.cc), netem `--turn=` and window/resize changes landed since -> apply with `git apply -3` and merge
keeping all features). Follow its "Integration order" (vendor libvpl 2.17 dispatcher sources with SHA256 check; no driver DLLs).
STATE (continue from here): the diff is ALREADY applied and merged and libvpl is vendored in endpoint/third_party/libvpl (staged in git index);
the dispatcher compile errors (-Wunused-function etc. in vendor code) were being fixed by another agent with object-only builds: check
C:\src\tmp\m3\vpl-integrate\NOTES.md and continue.
1. Build prts_endpoint; fix compile/link errors (Chromium toolchain, libc++, exceptions only in the dispatcher target).
2. Smoke on the chain: `--encoder=vpl` (default when VPL loads) and `--encoder=openh264`: frames reach the viewer, no corruption (viewer screenshot),
   resize (viewer resize to 1280x720 and back) works with reinit + IDR, keyframe request works, the endpoint log names the VPL implementation/runtime
   (legacy MSDK on UHD 630) and the selected settings (GetVideoParam readback: rate control, LowPower, AsyncDepth, level, profile).
3. Measure with the existing per-stage breakdown: `node tools/latency/run.mjs --no-mock --secs 60` (via test.sh) for vpl and openh264, 2 runs each,
   loopback, headless viewer: capture->display and input->flip P50/P80/P95/P99 and the endpoint stage table (copy, convert, encode). Every report already
   records the pipeline configuration; make sure the encoder field shows VPL and its settings.
NOTES.md: merge decisions, build fixes, smoke evidence, the numbers table with configuration, remaining risks (keyed mutex hold time, adapter LUID match).
