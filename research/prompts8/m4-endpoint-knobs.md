# Task m4-endpoint-knobs (timeout 40 min): endpoint experiment switches + VPL rate-control hardening (draft only, no build, no launch)
Task dir C:\src\tmp\m4\endpoint-knobs\. Another task is building and launching the chain from the SAME tree right now, so you must NOT edit
remotekit/endpoint in place and must NOT build or launch anything. Work on a copy: copy remotekit/endpoint/{vpl_encoder.cc,vpl_encoder.h,peer.cc,peer.h,
main.cc,BUILD.gn} (plus any other endpoint file you need) to C:\src\tmp\m4\endpoint-knobs\work\, edit there, and produce `endpoint.diff`
(unified, paths relative to remotekit, must apply with `git apply` on current HEAD; verify with `git apply --check`). Check every libwebrtc/oneVPL API
you use against headers at the pin (C:\src\chromium\src\third_party\webrtc, endpoint/third_party/libvpl).
Goal: the orchestrator runs the design/03 section 6 experiments with switches only (defaults = today's behaviour exactly):
1. VPL hardening (research/m3/vpl-review.md, vpl-harden.md, STATUS M3 known issue 1): readback after Init/Reset must tolerate driver rounding on
   TargetKbps/MaxKbps/BufferSizeInKB/InitialDelayInKB/MaxFrameSize (accept small differences, log them, never fall back to OpenH264 for that);
   a readback that differs only by rounding updates parameters_ instead of Fail().
2. VPL switches (parsed once, e.g. a struct filled from base::CommandLine in vpl_encoder.cc): `--vpl-rc=vbr|cbr` (E2; VBR keeps LowDelayBRC+MaxFrameSize),
   `--vpl-max-frame-factor=<float>` (MaxFrameSize = factor x average frame size at the current target rate, or at max rate when unset; if Reset cannot
   change MaxFrameSize, apply at Init and document), `--vpl-vbv-frames=<n>` / `--vpl-vbv-ms=<n>` (E3), `--vpl-tu=<1..7>` and `--vpl-lowpower=on|off` (E4),
   `--vpl-intra-refresh=<frames>` (E6: mfxExtCodingOption2 IntRefType=1 vertical, IntRefCycleSize; keep IDR on PLI), `--vpl-slices=<n>`,
   `--vpl-profile=cb|high` (E14; High needs the SDP profile-level-id/H264Format to match: implement if small, otherwise describe and gate it).
   Each switch: validated, echoed in the "PRTS VPL Init" log line and in whatever feeds the report `pipeline` block (find how the endpoint reports
   encoder state to tools/latency/pipeline.mjs; extend it, and include tools/latency/pipeline.mjs changes in the diff if needed).
3. peer.cc switches: `--min-kbps --start-kbps --max-kbps` (E5; today 2000/8000/12000 hard-coded), `--playout-delay=min,max` in ms (E10; default 0,0;
   `none` = do not negotiate the extension), `--fec=off|ulpfec|flexfec` (E13: codec preferences incl. red/ulpfec, or the FlexFEC field trials;
   state which the Chrome 157 and Chrome 89 receivers accept). `--fps` already exists (E1).
4. NOTES.md: every switch, default, where it is applied, which readbacks you could not verify without running. Keep the diff minimal and compile-clean by
   inspection (includes, types, mfxU16 overflow for kbps > 65535 / BRCParamMultiplier).
