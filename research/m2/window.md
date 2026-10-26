# m2-window

Task: remote windows maximized without caption controls/resize border; viewer-sized live viewport.
Pin: Chromium 157.0.8103.15, branch prts. No commits, clean/full builds or suites.

## Result

Implemented end to end. Default before viewer connects is 1920x1080 @1 and maximized (default-before-viewer.json). First viewer ctl resize updates it. Tabs and toolbar remain visible; caption buttons are absent. Ctrl+N and a window.open browser popup also start maximized. Every registered root follows subsequent viewport changes.

Endpoint applies even DIP dimensions, minimum 640x480, and DSF=min(dpr,sqrt(1920*1080/DIP-area)), floor 1. For viewers below the minimum, scale both DIP dimensions uniformly to preserve the viewer aspect; thus 390x844 becomes 640x1386 instead of independently clamping width and changing aspect. Above the pixel budget, DIP dimensions scale down uniformly at DSF 1. Monitor/root pixel dimensions round down to even values. [I] This aspect-preserving minimum interprets the requested minimum in a way consistent with the no-letterbox requirement.

## Changed files

Remotekit:
- endpoint/browser_session.cc: validate ctl resize and apply sizing/budget policy over Mojo.
- endpoint/peer.cc: one InitEncode size diagnostic proving encoder reinitialization.
- viewer/viewer.js: first resize on ctl open, video-area CSS dimensions, 200 ms debounce, media query listener for DPR changes.

Chromium:
- chrome/browser/prts/mojom/prts.mojom: append SetViewport(Size dip,float dsf), preserving existing method ordinals.
- chrome/browser/prts/prts_controller.{cc,h}: guarded viewport dispatcher.
- chrome/browser/prts/prts_window_registry.{cc,h}: maximize added windows and remaximize all roots, setting exact monitor pixels afterward.
- chrome/browser/prts/BUILD.gn: direct ui/display dependency.
- chrome/browser/ui/views/frame/browser_desktop_window_tree_host_win.cc: guarded maximized Show and zero native client insets.
- chrome/browser/ui/views/frame/browser_frame_view_win.cc: guarded caption-control hiding and zero reserved width; no drag/resize nonclient hit targets.
- ui/display/win/screen_win_headless.{cc,h}: non-test SetPrimaryDisplayGeometry API preserving display ID, updating monitor/work area/DSF and notifying observers.

Focused review diffs: chromium.diff and remotekit.diff. Existing unrelated work was left untouched.

## Validation and evidence

Incremental be.sh chrome and be.sh builds PASS, including final rebuild after preserving Mojo ordinals. node --check viewer/viewer.js and git diff --check PASS. No full build or broad test suite. clang-format binary is absent from checkout/VS paths; formatter attempts made no edits and additions were manually reviewed.

Headless Edge viewer driven through CDP, real control-plane/endpoint/PRTS Chrome:

| Viewer CSS / DPR | Capture pixels | Remote page inner size / DPR | Result |
| --- | --- | --- | --- |
| 1280x720 / 1 | 1280x720 | 1280x633 / 1 | PASS |
| 1920x1080 / 1 | 1920x1080 | 1920x993 / 1 | PASS |
| 1600x900 / 1 | 1600x900 | 1600x813 / 1 | PASS |
| 390x844 / 3 | 978x2118 | 640x1299 / 1.5289415121 | PASS |
| 1280x720 / 2 | 1920x1080 | 1280x633 / 1.5 | PASS |
| 1280x720 / 1 (DSF reset) | 1280x720 | 1280x633 / 1 | PASS |

Page height excludes browser UI, which is intentionally included in video. All tested roots report Browser.getWindowForTarget windowState=maximized. No desktop letterboxing; phone rounding leaves only 0.139 CSS px per side (no visible black bar). No caption buttons/border in inspected screenshots. A link was clicked through viewer input and navigated successfully after each of the four required resizes. Ctrl+N and a requested 300x250 popup both inherit the full maximized viewport.

Artifacts: resize-results.json, validate.log, 01-*.png through 08-popup.png; each requested size has before/after-click screenshots. The fixture prints innerWidth, innerHeight, devicePixelRatio, outer and screen sizes. Test code: fixture-server.mjs, cdp.mjs, validate.mjs.

Extra validation: 2560x1440 viewer caps to 1920x1080 @1. With two browser roots open, both resize to 640x1386 phone and 1600x900 desktop and remain maximized. extra-results.json / extra.log / screenshots09-11; decoder keyFramesDecoded incremented 16->17->18 after resizing. InitEncode logs show 1280x720,1920x1080,1600x900,978x2118 on the same running session. D3D source/capture already rebuild safely on WindowChanged, so no unnecessary capturer/encoder implementation changes were needed.

## API verification at pin

[V] ui/views/widget/widget.h: Widget::Restore/Maximize; ui/aura/window_tree_host.h:228 SetBoundsInPixels; :249 device_scale_factor. Headless Maximize skips already-maximized windows (ui/views/win/hwnd_message_handler_headless.cc:298), hence restore/remaximize.
[V] ui/display/win/screen_win_headless.h existing UpdateDisplay private implementation routes through UpdateFromDisplays and ScreenWin::UpdateFromDisplayInfos, whose implementation notifies change_notifier_.NotifyDisplaysChanged (screen_win.cc). New setter uses the same non-test notification path directly with exact pixel DisplayInfo.
[V] ui/display/win/display_info.h: synthesized-ID DisplayInfo constructor; ui/display/win/screen_win_display.h:36 pixel_bounds accessor; screen_win_headless.h:106 GetPrimaryScreenWinDisplay. The new setter is a general screen API: its PRTS caller is guarded by ui::IsPrtsRemoteMode(). ui/display cannot depend back on ui/base, which already depends on display.
[V] ui/views/view.h:580/666 SetBounds/SetVisible; Windows browser host Show override and ui/base/mojom/window_show_state.mojom.h kMaximized read before editing.
[V] base/values.h:323 FindDouble; base/containers/flat_map.h:232 at. gfx Size/Rect headers and generated Mojo contract checked.
[V] third_party/webrtc/video/video_stream_encoder.cc:1888 detects input size changes, :1354 ReleaseEncoder/InitEncode, :1380 queues a keyframe. H264EncoderImpl::InitEncode/Release in modules/video_coding/codecs/h264/h264_encoder_impl.cc recreates OpenH264. Runtime InitEncode sizes and decoded keyframes confirm this path.
[V] third_party/blink/renderer/core/css/media_query_list.idl declares addListener/removeListener aliases compatible with older viewers.

## Open risks / limits

- Physical display-DPR transitions and Chrome89 were not exercised. Edge CDP changes DPR/matchMedia.matches without emitting resize or media-query change events when CSS dimensions stay unchanged (probe-dpr.mjs); extra DSF-only protocol validation explicitly dispatches resize. The four required dimension-changing cases use actual CDP resize events.
- Freshly linked component DLL loading was slow on this host. Initial browser launches hit the pre-existing15-second Hello timeout; startup-stack.log shows NtCreateSection/LoadLibraryExW before browser initialization. A standalone warm launch succeeded and real-chain tests then passed; startup timeout policy was not changed.
- Even DIP/pixel rounding can differ from the requested aspect by subpixel amounts; phone content-fit gaps are quantified above.

## Work log

NOTES.md created in the first tool call and updated incrementally. Initial inspection found resize forwarded by peer but ignored by BrowserSession, and capture had an existing resize path. Implemented viewport/frame hooks, verified headers, built incrementally, fixed fractional-DSF odd-pixel round trips, ran CDP sizes/clicks/new-window checks, added DPR media-query handling, ran multiwindow/budget checks, then preserved Mojo ordinals and rebuilt. Evidence and focused diffs are entirely in this task directory.

Final binary validation PASS after Mojo ordinal cleanup: validate.log and extra.log. Fresh-launch default check PASS at 1920x1080 @1 (default-before-viewer.json). Refreshed screenshots after dismissing the prior forced-shutdown restore bubble through its close button using viewer input. Test processes are stopped at handoff; ports used were CP8300, fixture8306, source CDP9335 and viewer CDP9446.

