# M1 debug frames - result

## Outcome and scope

P0 fixed and incrementally built. The one-frame stall was an initial navigation abort after a Network Service crash before IPC, not a missing vsync fallback or an unreleased capture lease. Remote mode now retries the aborted startup GET once when the replacement Network Service launches. The underlying service bootstrap crash still occurs; the fix recovers its effect on startup navigation without changing service process or sandbox settings.

Work started around 04:43 and completed around 05:06 on 2026-10-26 Asia/Singapore. This NOTES file was created before the first baseline run. The incremental investigation record is retained in NOTES-investigation.md.

## File list

Modified in C:/src/chromium/src:
- chrome/browser/prts/prts_controller.cc: controller-owned startup recovery observer.
- chrome/browser/prts/prts_controller.h: forward declaration and owning unique_ptr.
- chrome/browser/prts/BUILD.gn: direct //net and //url dependencies.

No endpoint edits, no upstream hook edits, no remotekit repository edits, no commits. Pre-existing edits to prts_capture.cc and prts_link.h were preserved.

Task output:
- startup-navigation-recovery.patch, patch-checks.txt; before/ and after/ copies of the three changed files.
- NOTES-investigation.md: incremental chronology, including unsuccessful implementation checks.
- baseline-*, native-app-*, fake-vsync-app-*, no-extensions-*, network-only-*, startup-* and recovery-app-* evidence.
- final-app.txt, final-tab.txt, final-healthy-app.txt and corresponding Chrome/endpoint logs.
- cdp-probe.mjs, cdp-state.mjs, cdp-trace.mjs, cdp-histograms.mjs; trace JSON/count files and lease-evidence.txt.
- validate-aligned.sh, run-aligned.mjs, align-marker.mjs, make-aligned-runner.mjs; harness/ contains final per-frame CSV and report JSON.

## Root-cause evidence

1. Baseline normal tab: one endpoint OnFrame, then zero endpoint fps; original 10s harness reports frames=0, latency n=0. CDP shows an initial empty about:blank document, although the target advertises the requested localhost URL. Injected rAF callback gets zero ticks over two seconds.
2. startup-trace.json records browser-initiated initial navigation to the latency URL, followed by WebContentsImpl::DidFinishNavigation with net_error=-3, has_committed=false, is_error_page=false. net/base/net_error_list.h:38 defines -3 as ERR_ABORTED. Chrome logs the Network Service restart at the same startup. startup-histograms.txt records NetworkSandboxEarlyLaunchCrashed count=1, true.
3. Initial native-app trace: no Display::DrawAndSwap and no captures during the three-second sample. A few BeginFrames occur and then the undamaged initial page idles.
4. Manually navigating to the same URL via CDP commits immediately, without changing vsync settings. Three-second native DXGI trace: 160 root DrawAndSwap, 91 captures, 69 ordinary min-period rate limits, zero PipelineLimited events. Injected rAF gets 107 ticks/two seconds. The original 25s harness sees 592 displayed frames after this intervention.
5. lease-evidence.txt: all 91 FrameInUse begins have matching ends; utilization at subsequent capture begins remains 0%. [V] Capturer's NotifyFrameReleased ends FrameInUse, so this is evidence that Done reaches Viz and releases leases. No pool exhaustion evidence.
6. --fake-vsync-rate=60 does not repair initial navigation. [V] ui/gl/vsync_thread_win.cc already falls back to timed Sleep on failed/early DXGI wait and notifies observers afterward. The monitor error spam is therefore not the stall's cause here.
7. Disabling extensions alone still stalls. NetworkServiceInProcess2 alone restores startup and sustained capture. This corroborates the service/bootstrap path rather than an extension, animation lock-in, or oracle setting.
8. Instrumented recovery check: initial error=-3, initial-document=1, renderer-initiated=0, method=GET, post=0, committed=0; after abort IsLoading=0 and IsInitialNavigation=1, but the aborted same-URL pending entry remains. Refusing every pending entry incorrectly prevented recovery. Final code permits only this same-URL entry.

[V] = read at this pinned checkout. [I] = inference, explicitly distinguished below.

## Fix and exact integration

The existing remote-only PostBrowserStart hook constructs PrtsController. Its constructor now creates PrtsStartupNavigationRecovery only under ui::IsPrtsRemoteMode(). All new code lives in PRTS-owned files, so no new upstream hook is necessary.

The observer attaches to the first initial WebContents, including asynchronously created --app windows through BrowserCollectionObserver. It records only browser-initiated initial HTTP(S) GETs that abort without committing. A retry requires a witnessed pre-IPC Network Service crash and replacement launch. A posted UI task rechecks that the page is still initial, is not loading, and has no different pending entry. New primary navigation clears the candidate; POSTs and renderer navigations cannot become candidates. Exactly one attempt is allowed. Observations and posted weak callbacks retire on commit, WebContents destruction, retry, controller teardown, or endpoint disconnect.

BUILD.gn direct dependencies added to source_set("prts"): //net for net/base/net_errors.h, //url for url/gurl.h. Existing //content/public/browser, browser_window, //base and UI deps supply the remaining APIs. No Mojo contract changes.

Build from the existing checkout with:

    bash /c/src/tmp/m1/be.sh chrome

Several incremental builds passed, including the final cleanup build used for all three final runs. No endpoint rebuild was needed. No gn clean or full build was run.

Patch validation (all status=0; exact commands in patch-checks.txt):
- git diff --check for the three changed files.
- git -C C:/src/chromium/src apply --check --cached <patch>: verifies against the unchanged index/baseline without changing it.
- git -C C:/src/chromium/src apply --reverse --check <patch>: verifies the installed working-tree change. Plain forward apply against the working tree would duplicate the already-installed fix.

## Validation and measurements

All final runs use native DXGI scheduling, hardware capture, real endpoint/libwebrtc/OpenH264, and a headless Edge viewer. No fake-vsync or NetworkServiceInProcess2 flag is used in the default app/tab runs.

| Run | Duration | Displayed frames | Marker decoded / failed | fps | Capture->display P50 | P95 | P99 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Final default app | 15s | 446 | 446 / 0 | 30.0 | 75.8 ms | 87.6 ms | 90.8 ms |
| Final default tab | 15s | 441 | 441 / 0 | 30.0 | 58.4 ms | 68.5 ms | 74.2 ms |
| Healthy-service app control | 10s | 298 | 298 / 0 | 30.1 | 73.3 ms | 82.8 ms | 86.8 ms |

App/tab each log one bootstrap crash and exactly one automatic retry; neither requires CDP navigation. Their output video is 1898x1068. Healthy-service control uses NetworkServiceInProcess2 and logs neither crash nor retry. Before marker alignment, the unmodified original 12s app harness already reports 352 displayed frames at ~30 fps.

Measurement adaptation is explicit: the existing page draws its marker at page origin, while capture includes browser chrome. The original viewer samples root origin. Task-local run-aligned.mjs is copied from the existing harness; it enables viewer CDP, finds the CRC-valid marker row, and changes only RKMeasure.onFrame's source-y sampling (45 px for app; 87 px for tab). Its statistics/report calculation is unchanged. It also fits the source canvas CSS to the viewport; runtime devicePixelRatio=1 and canvas dimensions match the viewport in these runs. No repository fixture/viewer is edited. Earlier DPI-based speculation from the renderer command line was not established by runtime data; the measured origin offset is proven.

Reproduce final measurements after building:

    bash /c/src/tmp/m1/debug-frames/validate-aligned.sh app 15 --url=http://localhost:8300/tools/latency/remote-page.html --app --cr-enable-logging --cr-remote-debugging-port=9333
    bash /c/src/tmp/m1/debug-frames/validate-aligned.sh tab 15 --url=http://localhost:8300/tools/latency/remote-page.html --cr-enable-logging --cr-remote-debugging-port=9333

The wrapper uses the supplied kill.sh/cp.sh setup, then runs the copied original harness and stores logs/CSV under this task directory. CDP is used for fixture/marker alignment only, not to reload or navigate the source in final runs.

## API citations [V]

Paths below are relative to C:/src/chromium/src. Every new nonstandard API called was read at the pin; successful compilation also validates its use.

| API used | Header / location read |
| --- | --- |
| ui::IsPrtsRemoteMode | ui/base/ui_base_switches.h:128 |
| GlobalBrowserCollection::GetInstance | chrome/browser/ui/browser_window/public/global_browser_collection.h:34 |
| BrowserCollection::ForEach; return false stops | chrome/browser/ui/browser_window/public/browser_collection.h:54 |
| BrowserCollectionObserver::OnBrowserCreated | chrome/browser/ui/browser_window/public/browser_collection_observer.h:16 |
| BrowserWindowInterface::GetTabStripModel | chrome/browser/ui/browser_window/public/browser_window_interface.h:242 |
| TabStripModel::GetActiveWebContents | chrome/browser/ui/tabs/tab_strip_model.h:441 |
| WebContentsObserver default ctor, Observe, web_contents | content/public/browser/web_contents_observer.h:1074 and :1082 |
| DidStartNavigation, DidFinishNavigation, DidStopLoading, WebContentsDestroyed | content/public/browser/web_contents_observer.h:340, :391, :421, :777 |
| NavigationHandle URL, primary frame, renderer origin | content/public/browser/navigation_handle.h:175, :200, :263 |
| NavigationHandle IsPost / original request method | content/public/browser/navigation_handle.h:329-341; initial/current methods distinguished |
| NavigationHandle initial document, net error, committed | content/public/browser/navigation_handle.h:400, :416, :505 |
| WebContents GetController / IsLoading | content/public/browser/web_contents.h:492, :808 |
| Controller IsInitialNavigation / GetPendingEntry | content/public/browser/navigation_controller.h:743, :547 |
| Controller LoadURLParams(GURL) / LoadURLWithParams | content/public/browser/navigation_controller.h:209, :574 |
| NavigationEntry::GetURL | content/public/browser/navigation_entry.h:72 |
| NetworkServiceProcessObserver / Add / Remove | content/public/browser/network_service_instance.h:34-44 |
| Service process launched / crashed observer methods | content/public/browser/service_process_observer_hub.h:27-32 |
| ServiceProcessInfo::crashed_pre_ipc optional bool | content/public/browser/service_process_info.h:66 and :83-86 |
| GURL::SchemeIsHTTPOrHTTPS / is_empty / equality | url/gurl.h:252, :99, :508 |
| ScopedObservation Observe / Reset | base/scoped_observation.h:114, :122 |
| WeakPtrFactory ctor / GetWeakPtr / InvalidateWeakPtrs | base/memory/weak_ptr.h:423, :436, :464 |
| SequencedTaskRunner::GetCurrentDefault / inherited PostTask | base/task/sequenced_task_runner.h:338; base/task/task_runner.h:68 |
| base::BindOnce | base/functional/bind.h:58 |
| net::ERR_ABORTED value | net/base/net_error_list.h:38 |

Additional implementation evidence read [V]: ui/gl/vsync_thread_win.cc (WaitForVSync fallback and observer notification); ui/gl/vsync_thread_win_dxgi.cc (missing monitor output path); components/viz/service/frame_sinks/external_begin_frame_source_win.cc (OnVSync->OnBeginFrame); components/viz/service/frame_sinks/root_compositor_frame_sink_impl.cc:209-235 (Windows source selection); components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.cc:1678 (NotifyFrameReleased); chrome/browser/headless/chrome_browser_main_extra_parts_headless.cc:32-39 (existing remote-only constructor hook).

## Open risks and limits

- This is a bounded startup-navigation P0 recovery, not a fix for the underlying early Network Service process crash. The precise bootstrap crash reason remains unknown; the Chromium histogram proves pre-IPC failure but not its internal cause.
- The process observer hub documents independently ordered service/child disconnect callbacks. Here both app/tab exercised crash then replacement launch and recovered. The hub suppresses stale crash notifications after it has moved to a new service instance.
- Recovery deliberately covers the first startup WebContents / one GET attempt. It is not a general navigation retry policy and does not replay forms or recover later service/renderer failures.
- Existing idle-source capture behavior remains damage driven. The observed one captured frame was correct for the uncommitted empty document.
- An exploratory --force-device-scale-factor=1 plus --start-fullscreen run produced continuous Chrome capture logs but no endpoint OnFrame; a browser-side geometry/metadata rejection is an [I] based on where logging occurs. Its exact condition was not instrumented. This alternate geometry was not needed or used for final default-window measurements; retain as a separate follow-up.
- The original marker fixture does not account for browser chrome. Use the explicitly aligned task runner to reproduce these numeric latency measurements.
