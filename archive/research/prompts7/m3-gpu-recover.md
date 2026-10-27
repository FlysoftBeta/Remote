# Task m3-gpu-recover (timeout 35 min): DRAFT capture recovery after a GPU process crash
Task dir C:\src\tmp\m3\gpu-recover\. DRAFT ONLY: another agent builds and runs the chain. Do NOT launch chrome/endpoint, do NOT build, do NOT edit
C:\src\chromium\src or remotekit in place. Produce chromium.diff (against src `prts` HEAD; a\ and b\ copies, verified with
`git -C C:\src\chromium\src apply --check`) and remotekit.diff if the endpoint needs changes (verified with apply --check).
Observed (C:\src\tmp\m2\latency\NOTES.md, "Repeat validation / GPU failure"): the GPU process crashed (Skia Graphite ResourceCache.cpp:523
check(isInCache)), restarted, and capture stayed at 0 fps for the rest of the run. Find why (chrome/browser/prts/prts_capture.* and the controller:
FrameSinkVideoCapturer remote disconnect on viz/GPU restart, HostFrameSinkManager connection loss, capture lock, the endpoint's opened shared
texture handles / cached DXGI tokens becoming invalid) and draft a fix: detect the capturer disconnect (or the frame sink manager reconnection),
recreate the capturer for each registered root, re-send the capture setup to the endpoint, and make the endpoint drop its texture cache and request a
keyframe. Also state whether Graphite on this driver is a stability risk and whether to pin Ganesh in remote mode (feature/switch name at the pin).
NOTES.md: cause analysis with citations, the fix design, how to test it (e.g. kill the GPU process via chrome://gpucrash or taskkill the --type=gpu-process
PID during a run), risks.
