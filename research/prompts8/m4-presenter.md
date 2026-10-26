# Task m4-presenter (timeout 75 min; LOW PRIORITY best effort, the acceptance metric now excludes client presentation): viewer presentation latency levers on real Chrome 89 (headed, real 60 Hz display)
Task dir C:\src\tmp\m4\presenter\. Results file: research/m4/presenter.md. You are the ONLY process that launches the chain now. You MAY edit viewer/ (behind
URL parameters; defaults unchanged) and tools/latency/ (no commits). Chrome 89: C:\src\tmp\chrome89-google\app\chrome.exe, launch flags in
C:\src\tmp\chrome89-google\NOTES.md and research/m4/chrome89.md (harness option --viewer-exe from the m4-chrome89 task). Headed window on the real display.
Background: stage "presentationTime -> expectedDisplayTime" is 17-24 ms (research/m3/stage-table.md row 19), an rVFC estimate.
Options, each measured loopback marker x3 (60 s) and d25j5-c12 S x2 against the current viewer as the control in the same session:
1. Fullscreen <video> (Fullscreen API or a kiosk window), 1:1 size, hardware decode: check chrome://gpu "Direct Composition"/"Overlays" and whether the
   video quad is promoted to an overlay (chrome://tracing or --enable-logging=stderr --vmodule=*overlay*=2 / DirectCompositionChildSurface logs).
2. Remove overlay blockers in viewer/ (CSS transforms, opacity, overlaid DOM such as the cursor/IME layers, non-integer size) behind `?presenter=clean`.
3. `?presenter=canvas`: desynchronized low-latency canvas (getContext('2d', {desynchronized: true})) drawing the WebRTC <video> on each
   requestVideoFrameCallback; the measurement must then read the marker from the canvas path (keep the input->photon method comparable; explain).
4. Measurement truth: state what rVFC/presentation feedback can and cannot prove about real scanout here; if a DXGI/PresentMon trace (PresentMon is
   not installed: only use it if already present; do not download) or `chrome://tracing` "Display::DrawAndSwap"/"Present" events give a better
   present->scanout estimate, use them. Write a short procedure for a high-speed phone video glass-to-glass check (for the user to perform).
Report: gain per option (P50/P80/P99 capture->display and input->flip, overlay promoted yes/no), and a recommendation for the viewer default.
