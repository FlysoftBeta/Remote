# Task m3-vpl-harden (timeout 40 min): fix the review findings in the VPL path, then smoke + GPU-restart test
Task dir C:\src\tmp\m3\vpl-harden\. You are the ONLY builder/launcher now. You MAY edit remotekit endpoint/ (no commits). Incremental `be.sh`.
Review (read-only, ranked, with file:line and fixes): C:\src\tmp\m3\review\NOTES.md. Fix findings 1-5 and the cheap ones in 6 (ToI420 null/log spam,
maxBitrate==0 guard, timing_ eviction by age). Key points: rebuild the native frame pool on Reset/Bind after failure (mutex WAIT_ABANDONED after a
GPU crash must not disable zero copy forever); ResetRate keeps a pending target and applies it when the throttle allows (from Encode); Close() clears
the shared encoder device only if it is this instance's; transient conditions drop a frame (and request a keyframe if needed) instead of Fail() -> only
repeated errors or device loss fall back to OpenH264; stale-device native frames go through ToI420 instead of restarting the encoder.
Test: build; smoke both encoders (frames, resize, keyframe); then with VPL kill the Chrome GPU process during a run (taskkill the --type=gpu-process
chrome.exe) twice: zero copy must come back (log native frames delivered after the restart), no fallback to OpenH264, viewer gap < 1.5 s; and a
60 s loopback latency run (`node tools/latency/run.mjs --no-mock --secs 60`) to confirm no regression vs research/m3/vpl-fix.md.
NOTES.md: per-finding fix, test evidence, remaining risks.
