# Task m3-vpl-fix (timeout 45 min): fix the oneVPL path (IDR storm, low bitrate caps, slow VPP/mutex hold) and measure
Task dir C:\src\tmp\m3\vpl-fix\. You are the ONLY builder/launcher now. You MAY edit remotekit endpoint/ (no commits). Incremental `be.sh`.
State: C:\src\tmp\m3\vpl-integrate\NOTES.md (just integrated; UHD 630, legacy MSDK 1.35). Measured problems (loopback, 60 s):
- 120 rate rebuilds and ~115 IDRs per minute: every SetRates from GCC rebuilds the encoder + IDR -> P95 133-137 ms vs OpenH264 67-73.
- Bitrate start/min/max 1885/30/3608 kbps on the VPL path vs 2000/2000/12000 for OpenH264 (the encodings max_bitrate / codec settings differ).
- Native VPP BGRA->NV12 9.7 ms P50 (16.6 P95) and keyed mutex held up to 16.6 ms.
Fix (keep both encoders working; OpenH264 behaviour unchanged):
1. Rate changes without IDR: keep BufferSizeInKB/InitialDelayInKB fixed (sized for the max bitrate) and change only TargetKbps/MaxKbps via
   MFXVideoENCODE_Reset (try with and without mfxExtEncoderResetOption StartNewSequence=OFF; check the returned status and GetVideoParam);
   if the runtime still forces an IDR, apply hysteresis (ignore changes < 20 %, at most one reset per 2 s, never on the first frames) and prefer
   leaving the encoder at the max allowed bitrate with VBR LowDelayBRC. Count resets/IDRs in the 5 s stats.
2. Same bitrate config as the OpenH264 path (start/min/max from the same RtpEncodingParameters / VideoCodec settings).
3. Mutex/VPP: inside the keyed mutex do only a CopyResource into a private pool texture, release the mutex, then VPP from the private copy; submit VPP
   and ENCODE back to back without SyncOperation between them (sync only on the bitstream); measure VPP submit vs completion separately.
4. Measure: `node tools/latency/run.mjs --no-mock --secs 60` loopback headless, vpl vs openh264, 2 runs each, plus the scrolling and game scenarios from
   tools/latency/scenarios.js once each (vpl and openh264). Table with P50/P80/P95/P99 (capture->display, input->flip), encode stage stats, resets/IDRs,
   send bitrate, pipeline block. If VPL is still worse than OpenH264 on any scenario, explain why with data.
NOTES.md: changes, results, remaining risks.
