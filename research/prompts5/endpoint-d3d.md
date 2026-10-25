# Task endpoint-d3d (timeout 45 min): endpoint D3D11 frame source + browser-link glue + input forwarding
Output dir: C:\src\tmp\m1\endpoint-d3d\  (files that will be added to //prts_endpoint next to the drafts in C:\src\tmp\m1\endpoint\)
Inputs (read first): C:\src\tmp\m1\endpoint\ (NOTES.md, frame_source.h, peer.*, input_parser.*, main.cc, BUILD.gn),
C:\src\tmp\m1\s10-link\ (endpoint_link.*, NOTES.md, mojom), C:\src\tmp\m1\contract\prts.mojom, design/03 section 3.3.

Deliverables:
1. d3d_frame_source.{h,cc}: implements prts.mojom.PrtsFrameSink (bound on its own sequence) and the endpoint's FrameSource interface.
   D3D11 device on the adapter whose LUID == HelloInfo.gpu_luid (fallback: default hardware adapter), VIDEO_SUPPORT + multithread protected.
   NewBuffer: OpenSharedResource1 the NT handle, cache by buffer_id (Reset clears). OnFrame: IDXGIKeyedMutex AcquireSync(0, 20 ms) (only S_OK
   counts), CopyResource into an endpoint-private BGRA texture, ReleaseSync(0), reply immediately (browser then calls Done), then on the
   source's own thread: copy to a STAGING texture, Map, libyuv::ARGBToI420 (check BGRA vs ARGB byte order: DXGI B8G8R8A8 = libyuv "ARGB"
   in memory) into a webrtc::I420Buffer honoring visible_rect, deliver as webrtc::VideoFrame with timestamp from capture_end mapped to
   rtc::TimeMicros (same machine monotonic clock: base::TimeTicks vs rtc::TimeMicros - verify both are QPC-based at the pin or convert).
   Latest-frame-wins: if the previous frame is still being converted, drop the older one. Log per-second fps and convert time.
   (OpenH264 at 1080p30 is the M1 encoder; oneVPL comes in M3 - keep the import code reusable.)
2. browser_session.{h,cc}: implements PrtsBrowserClient: tracks windows; on first WindowAdded (or WindowActivated) calls
   StartCapture(id, {fps, nv12=false}, sink remote of the D3D11 source) and switches capture when the active window changes; sends the
   viewer `ctl` `frame` message on size change (hook point into peer: a callback interface); implements the endpoint's InputSink by forwarding
   to PrtsBrowser::Pointer/Wheel/Key/ResetInput/Ime*/Focus on the active window id (converting the 06 binary units: normalized u16 -> float
   0..1, wheel lines -> 40 px, frame px -> DIP using the window's DSF, viewer timestamps -> local TimeTicks::Now() for M1). Keyframe request
   -> RequestRefreshFrame + encoder key frame.
3. main_integration.md: exact edits to main.cc / BUILD.gn / peer.* from the endpoint task to wire: flags --chrome=<path>
   --user-data-dir=<dir> --url=<start url> (launch chrome through endpoint_link with `--headless=new --prts-remote --screen-info={1920x1080}
   --no-first-run --no-default-browser-check --disable-legacy-window --user-data-dir=...`), --synthetic (keep the test source). Provide the
   full replacement main.cc and BUILD.gn as main.cc / BUILD.gn in your dir rather than prose where possible.
Cite every API at the pin (file:line) in NOTES.md. Scope: no browser-side code. Max ~1000 lines.
