# Task m2-window (timeout 45 min): remote window = maximized, frameless, sized to the viewer, live resize
Task dir C:\src\tmp\m2\window\. You are the ONLY builder/launcher now. You MAY edit: chrome/browser/prts/* (incl. mojom), the minimal upstream files needed
(e.g. ui/display/win/screen_win_headless.*, the browser frame view for caption buttons), remotekit endpoint/ and viewer/. Incremental builds only
(`be.sh chrome`, `be.sh`). No commits. Other agents are drafting diffs against the same files; keep your changes focused.

Today: endpoint launches chrome with --screen-info={1920x1080} --window-size=1920,1080 (endpoint/main.cc ~L119); the window comes out 1898x1069 with a
caption bar (min/max/close) and the viewer letterboxes. Design (decided):
1. Remote mode browser windows are MAXIMIZED to the virtual screen work area and show NO caption buttons and no resize border (keep tab strip + toolbar;
   not fullscreen). New windows (Ctrl+N, window.open popups that are browser windows) behave the same. Find the smallest hook (e.g. caption button
   visibility in the Windows browser frame view, a ShouldShowWindowControls-like predicate, initial show state = maximized in remote mode).
2. Viewport from the viewer: viewer sends ctl `resize {w,h,dpr}` (CSS px of the video area; on load and on window resize, debounced 200 ms) per design/06.
   Policy (endpoint): DIP size = w x h (even, min 640x480); DSF = min(dpr, sqrt(1920*1080/(w*h))) clamped to >= 1.0, so pixel area <= 1080p (if w*h alone
   exceeds 1080p, scale DIP down keeping aspect, DSF 1). Endpoint -> browser over Mojo (new method on the existing controller interface, e.g.
   SetViewport(gfx.mojom.Size dip, float dsf)). Browser: update the headless screen (display bounds + work area + DSF; notify display observers with a
   non-test setter in screen_win_headless, like the existing cursor-point setter from patch 06), then re-maximize every registered browser window so
   the root matches; the root capturer must follow the new pixel size (check prts_capture resolution constraints) and the endpoint frame source /
   encoder must handle the size change (reinit OpenH264 on size change; no crash, keyframe after resize).
3. Initial size: if the viewer connects after chrome started, apply its first resize; keep 1920x1080 @1 as default.
Test (headless Edge viewer via CDP, Emulation.setDeviceMetricsOverride or window size): 1280x720@1, 1920x1080@1, then live resize to 1600x900 and
390x844@3 (phone-like): screenshot the viewer each time: no letterbox, no caption buttons, page layout matches (report window.innerWidth/devicePixelRatio
of a remote page via a test page that prints them). Input coordinates must stay correct after resize (click a link). Record results in NOTES.md.
