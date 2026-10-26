# Task marker-align (timeout 30 min): latency harness must find the marker anywhere near the top-left of the frame
Edit ONLY: viewer/ (measurement code), tools/latency/ (marker.js, measure.js, run.mjs, remote-page.html). No commits. Notes:
C:\src\tmp\m1\marker-align\NOTES.md. Do not run C:\src\chromium\src\out\dev\chrome.exe (a build is running); test against the mock
(`node tools/latency/run.mjs --secs 10 --port 8310`) and with a synthetic offset (e.g. add `?my=87` to the mock page so it draws the marker
87 px lower, or similar test hook).

Problem: in the real chain the page marker (tools/latency/remote-page.html draws it at page origin) sits below the browser chrome
(about 45 px in --app windows, 87 px with the tab strip), but the viewer decoder samples only frame row 0. A Codex debug run proved it
works when the sampling y is offset (C:\src\tmp\m1\debug-frames\run-aligned.mjs / align-marker.mjs: read them).
Implement in the viewer measurement code: auto-detect the marker row by scanning the left part of the frame (x < 2 cells, y from 0 to
min(200 px, H/4) in source pixels) for the sync pattern + CRC-valid decode, lock onto the found y (re-scan after N consecutive CRC
failures or on frame size change), and use the found y for all later frames. Keep Chrome 89 compatibility (no new APIs) and cost low
(scan only when unlocked). Also accept an explicit `my=<px>` URL parameter override. Make remote-page.html optionally draw a second copy
of the marker (not needed if detection works - skip). Verify: mock at y=0 still gives the same latency numbers, and offset y=87 is detected.
Final message: 5 lines.
