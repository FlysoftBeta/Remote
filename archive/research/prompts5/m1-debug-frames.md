# Task m1-debug-frames (timeout 45 min): real chain delivers only ONE captured frame - find and fix
You MAY edit and build in this task (you are the only one building now): files under C:\src\chromium\src\chrome\browser\prts\ and
C:\Users\Flysoft\Desktop\remotekit\endpoint\ (= C:\src\chromium\src\prts_endpoint, a symlink), plus upstream files touched by our patch
series if the root cause is there (keep `// PRTS:` + ui::IsPrtsRemoteMode() guards). Do NOT git commit, do NOT run gn clean/full builds,
do NOT touch other repos. Record everything in C:\src\tmp\m1\debug-frames\NOTES.md (create it in the first 5 minutes).

Setup (all working): patched Chromium out\dev (chrome.exe, prts_endpoint.exe). Scripts:
- `bash /c/src/tmp/m1/be.sh` builds prts_endpoint; `bash /c/src/tmp/m1/be.sh chrome` builds chrome (incremental, 30-150 s; kills test processes first).
- `TAILN=30 bash /c/src/tmp/m1/test.sh 10 --url=http://localhost:8300/tools/latency/remote-page.html --cr-enable-logging`
  restarts control-plane (port 8300) + endpoint (which launches chrome --headless=new --prts-remote with the Mojo link), runs a headless
  Edge viewer for 10 s and prints the harness summary + endpoint log. Endpoint log: C:\src\tmp\m1\cp1.log. Chrome log (with
  --cr-enable-logging): C:\src\tmp\m1\prof-8300\chrome_debug.log. `--cr-<switch>` endpoint args are forwarded to chrome as `--<switch>`.
  `--app` opens the URL as an app window.
Symptom: Mojo link OK (Hello), StartCapture OK (chrome log "PRTS: StartCapture window=1 size=1898x1069"), viz delivers exactly one frame
("PRTS: captured frame #1 gmb"), the endpoint imports/converts it (D3D11 keyed mutex OK, OpenH264 encodes, viewer decodes 1898x1068), then
no more frames although the page animates with rAF (tools/latency/remote-page.html). Also seen: "Network service crashed or was terminated,
restarting service" once at startup (also without --prts-remote; probably unrelated), and GPU-process spam
`vsync_thread_win_dxgi.cc: Failed to find DXGI output with matching monitor` (no monitor attached to the UHD 630 output?).
Code: chrome/browser/prts/prts_capture.cc (Session: CreateVideoCapturer on the root compositor frame sink, ARGB, mappable shared image,
consumer on a ThreadPool sequence, Done() after the endpoint replies), endpoint/d3d_frame_source.cc (OnFrame replies after CopyResource),
viz fix patch 04 in components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.cc (CompleteCapture on failed delivery).

Find the root cause with evidence (check: is the root ui::Compositor drawing at all - BeginFrames/DrawAndSwap on the root sink in headless
with a cloaked HWND and no matching DXGI output; does the renderer rAF tick (CDP: add --cr-remote-debugging-port=9333); is Done() really
reaching viz (frame pool exhausted -> "frame dropped" in capturer logs; you can raise capturer log verbosity via OnLog); does the oracle
reject frames (min period / animation lock-in)). Fix it in the smallest correct way (a P0 fix is fine: e.g. if vsync is broken in this
environment, make the root BeginFrame source fall back to a timer in remote mode, or use an existing switch; justify). Success = the harness
prints frames > 100 and a capture->display latency line; report the numbers. If the --app mode or the tab-strip mode differ, test both.
Final message: root cause, fix (files), measured result.
