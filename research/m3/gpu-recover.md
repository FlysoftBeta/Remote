# m3-gpu-recover (DRAFT ONLY)
Started 2026-10-26. Scope: diagnose GPU restart capture loss; draft patches only.
No builds, process launches, source-tree edits, or commits authorized or performed.

## Work log
- Read latency incident notes: Graphite ResourceCache.cpp:523 failure, GPU restart, persistent 0 fps.
- Inspected pinned capture/controller, Viz reconnection, endpoint texture ownership and encoder controls; completed isolated drafts and static verification.

## Files changed
- Only task-local draft copies, chromium.diff, remotekit.diff, evidence, verification logs and generation scripts. See deliverables below.

## Evidence / API citations
[V] = read at Chromium prts HEAD (or identified endpoint snapshot); [I] = inference.
- Incident: C:/src/tmp/m2/latency/NOTES.md, Repeat validation / GPU failure.

## Fix design / validation / risks
- Source analysis and draft design follow. Both patches pass apply --check; build and runtime validation belong to the integrating agent.

## Source findings / draft design (15:17 SGT)
- [V] prts_capture.cc: Session uses the raw FrameSinkVideoCapturer remote. Both its disconnect and FrameConsumer receiver disconnect invoke StopIfCurrent -> StopCapture -> sessions_.erase. Session destruction releases VideoCaptureLock. PrtsController only forwards endpoint StartCapture; BrowserSession::WindowChanged selects again only on geometry/DSF changes. GPU restart does not guarantee either, explaining persistent 0 fps [I].
- [V] components/viz/host/client_frame_sink_video_capturer.h explicitly recommends its wrapper to survive Viz crashes. Its .cc retries after 100 ms and replays format, timing, constraints, target, buffer preference, Start. HostFrameSinkManager::OnConnectionLost clears its manager pointer, then invokes the transport callback; content/browser/compositor/viz_process_transport_factory.cc::OnGpuProcessLost synchronously calls ConnectHostFrameSinkManager/BindAndSetManager, so the stock delayed retry uses the replacement pipe rather than polling an unbound manager.
- Draft retains every existing Session/root and capture lock, uses ClientFrameSinkVideoCapturer on UI, and forwards consumer calls over one persistent UI -> frame-runner Mojo pipe. Its establish callback sends OnStopped on that SAME pipe before creating the replacement capturer; FrameConsumer::OnStopped now means clear token map + PrtsFrameSink::Reset, not endpoint/session death. Replacement NewBuffer/OnFrame messages follow the reset in FIFO order. A separate posted Reset task would race queued Mojo frames; the persistent forwarding pipe avoids that.
- [V] endpoint/d3d_frame_source.cc::Reset already clears opened ID3D11Texture2D/IDXGIKeyedMutex COM imports, native_pool_->ClearImports(), pending work, and increments epoch. Browser FrameConsumer buffers_ maps DXGIHandleToken to IDs; both sides must reset together. Kept IDs/frame_seq monotonic within the persistent session.
- Endpoint draft supplies a BindPostTask(main, WeakPtr BrowserSession::Refresh) callback per BindCapture. Reset invokes it after releasing mutex_; Refresh asks both Viz for a fresh frame and Peer for an IDR (existing GenerateKeyFrame path). No Mojo contract/version change and no viewer renegotiation. Setup replay to endpoint is existing Reset + fresh NewBuffer(handle, size), followed by OnFrame.
- No controller change needed: ownership stays alive in PrtsCapture; controller/window input state and endpoint selected root stay intact. Registered roots with active Sessions recover independently; inactive roots are not newly captured.
- Draft copies now created under chromium/{a,b} and remotekit/{a,b}; bases in BASES.json. No source tree edits.

## Deliverables / verification
- chromium.diff: changes ONLY chrome/browser/prts/prts_capture.cc, against prts HEAD c4699fcfdb72a2cba977394f4a89dc942db8cf71.
- remotekit.diff: changes ONLY endpoint/browser_session.cc, endpoint/d3d_frame_source.{cc,h}, against main HEAD 7e88818e9f07f3056192a1ae11ae423a7f7e2a67. Integrate both patches together for IDR requests; Chromium-only patch recovers capture but does not add the endpoint keyframe callback.
- chromium/a, chromium/b, remotekit/a, remotekit/b contain complete before/after copies. a copies are byte-identical to the corresponding HEAD blobs, including the existing CRLF d3d_frame_source.cc. No source or index modifications were made in either live repository. Existing unrelated dirty work was preserved.
- Both `git -C <repo> apply --check <task>/<name>.diff` passed (exit 0), then both passed with `--whitespace=error-all`. Evidence: chromium-apply-check.txt, remotekit-apply-check.txt, verification.txt (SHA256, exact base-copy check, unchanged live-source check).
- Changed line ranges formatted with the pin's buildtools/win-format/clang-format.exe, --style=Chromium. No compilation, GN, browser/endpoint launch, runtime test, or commit was performed.
- draft.mjs creates the isolated copies and changes, finalize.mjs formats only changed ranges / generates patches / checks application, verify.mjs records source invariance and extracts historical evidence. Scripts write only under this task directory.

## Historical evidence (not a runtime test of the draft)
See evidence.txt for numbered excerpts from C:/src/tmp/m2/latency/receiverOffRepeat-{chrome,endpoint}.log.
- Chrome lines 7057 and 7144: 11:06:16.145 and 11:06:18.735, ResourceCache.cpp:523 check(this->isInCache(resource)), Graphite removeFromResourceMap/purgeResource stack.
- Chrome 7229: 11:06:20.059 GPU process exited unexpectedly, exit_code=-2147483645. Chrome 7231: 11:06:20.480 reinitialized GPU; initialization reported 99 ms. ANGLE D3D11 appears in the crash keys. No new StartCapture follows the crash in that log.
- Endpoint 90: 11:06:15.573 fps=29.8886; 91: 11:06:18.745 fps=1.89121; 93-96: 11:06:19.756 through 11:06:22.786 fps=0; 123: 11:06:42.938 still fps=0.
- [I] The persistent stall follows directly from the current teardown-only disconnect path. The old logs do not instrument StopIfCurrent itself, so the exact callback ordering is source-based analysis, not observed tracing. There is no evidence here that an endpoint OpenSharedResource1 failure caused the original stall; clearing those caches prevents stale GPU resources after recovery.

## Verified APIs / source citations
All Chromium paths below are relative to C:/src/chromium/src at c4699fcfdb72a2cba977394f4a89dc942db8cf71. Endpoint paths are relative to remotekit at the recorded HEAD. [V] means read at that source; [I] means inference.
- [V] chrome/browser/prts/prts_capture.cc:61-65,157-182,225-283,328-335,362-367: both disconnect handlers, end-of-stream teardown, raw capturer creation, capture lock, session erase and generation guard.
- [V] chrome/browser/prts/prts_controller.cc::StartCapture/StopCapture/RequestRefreshFrame and OnDisconnect: dispatcher forwards commands; client disconnect stops everything. No GPU restart recovery callback exists there.
- [V] components/viz/host/client_frame_sink_video_capturer.h:30-35,77-111: documented crash resilience, EstablishConnectionCallback signature, configuration/Start consumer pointer and StopAndResetConsumer signatures.
- [V] components/viz/host/client_frame_sink_video_capturer.cc:20-21,211-268: 100 ms retry, remote disconnect handler, complete configuration/target replay, restarting the consumer receiver. StartInternal creates a new service-side consumer pipe while keeping our UI forwarding pipe intact.
- [V] components/viz/host/host_frame_sink_manager.h:248-257 and .cc:361-376: raw creation and preferred reconnecting wrapper overloads. Custom wrapper constructor is used to put an endpoint reset before each re-establishment; the stock creation overload has no reset notification.
- [V] components/viz/host/host_frame_sink_manager.cc:471-504,508 onward: manager pointer cleared on connection loss; compositor frame sink state invalidated; transport callback invoked; hierarchy re-registration after reconnection.
- [V] content/browser/compositor/viz_process_transport_factory.cc:147-149,160-186,325-327: existing sole connection-loss callback, synchronous replacement Mojo pipe binding and GPU service connection. We do not replace that callback or add hooks in Viz/content.
- [V] services/viz/privileged/mojom/compositing/frame_sink_video_capture.mojom: FrameSinkVideoConsumer/FrameCallbacks/FrameSinkVideoCapturer contracts; OnStopped denotes end of that capturer's stream, per-frame Done releases its lease.
- [V] components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.cc:437-511: Start creates a fresh pool and calls RefreshEntireSourceNow; static pages do not require animation to seed a restarted capturer.
- [V] ui/aura/window_tree_host.h:74-86,297-298: VideoCaptureLock retains required capture state/visibility; may be null. Existing ownership remains with the UI Session across reconnection and ends on explicit Stop/window removal/link teardown.
- [V] mojo/public/cpp/bindings/remote.h:101-114 and BindNewPipeAndPassReceiver: stable proxy access on its owning sequence and receiver creation; no Remote moved across sequences. receiver.h:54-73,124-145: receiver binds and dispatches on the supplied sequence. FIFO use of one interface pipe supplies reset ordering [I].
- [V] base/task/bind_post_task.h:20-26,97-114: repeating callback returns same signature and posts its Run to target TaskRunner. base/memory/weak_ptr.h:50-65: WeakPtr may travel across sequences, but is dereferenced/inactivated only on the owner sequence. Reset callback runs on main via BindPostTask before touching BrowserSession.
- [V] endpoint/d3d_frame_source.cc:115-133,172-205,307-319,459-465 (base): receiver/cache binding, OpenSharedResource1 and keyed mutex imports, Reset clears buffers/native imports/pending work and advances epoch, Convert checks epoch before consumer delivery.
- [V] endpoint/d3d_native_frame.h:26: ClearImports API; implementation clears the import map. Outstanding private output leases stay reference counted. This task does not modify the concurrently edited native-pool implementation.
- [V] endpoint/browser_session.cc:116-162,171-177 and endpoint/peer.cc:340-344,634-638: geometry-only reselection, Refresh calls capture refresh and keyframe callback, Peer marshals GenerateKeyFrame to signaling. third_party/webrtc/api/rtp_sender_interface.h:166: GenerateKeyFrame(const vector<string>& rids) returning RTCError.

## Graphite / Ganesh recommendation
- [V] third_party/skia/src/gpu/graphite/ResourceCache.cpp:522-527 asserts cache membership in removeFromResourceMap. The incident hit that host-side Skia invariant; it is not proof of a particular driver defect or proof of the root Graphite bug.
- [I] Graphite on this machine/driver configuration is an observed stability risk for this remote workload. Recommend pinning the M3 remote launch configuration to Ganesh until capture recovery and a longer Graphite soak pass. This is a mitigation, not a claimed fix for the Skia assertion or all GPU failures. No driver model/version was established by this task.
- [V] Exact pin switch: `--disable-skia-graphite` (gpu::switches::kDisableSkiaGraphite), declared gpu/config/gpu_switches.h:56, literal gpu_switches.cc:157. Endpoint/cp.sh passthrough: `--cr-disable-skia-graphite`. content/browser/gpu/gpu_process_host.cc:297 includes it in forwarded GPU switches.
- [V] gpu/config/gpu_finch_features.cc:594-604 checks disable before enable/Finch; gpu/config/gpu_util.cc:352-359 checks the switch before the Windows Intel override. Feature names: SkiaGraphite and SkiaGraphiteWinIntel, declarations gpu_finch_features.h:69-70, definitions .cc:381-395; LateGraphiteFeatureCheck at .cc:424 is enabled by default on Windows. Disabling only SkiaGraphite is insufficient to cover the separate Intel path; the explicit disable switch covers both.
- [V] gpu/ipc/service/gpu_init.cc:710-717 falls back when Graphite is disabled; :1361-1366 maps GL to GaneshGL and Vulkan to GaneshVulkan. Verify chrome://gpu or CDP SystemInfo in the integrating run to confirm Ganesh backend and hardware D3D11 compositing. No launch policy was silently changed by this draft.

## Integrator validation plan (NOT RUN)
1. Apply both checked patches, incremental build using the existing be.sh workflow. Do not gn clean, perform a full build, or run a full suite. First smoke normal 30 fps startup, input, resize and popup switching; save browser/endpoint logs.
2. Start the usual latency animation run for long enough to include 20 s warmup and at least 30 s after injection. Record remote browser PID, its --type=gpu-process child PID, viewer PeerConnection state and decoded-frame count. Restrict selection to the out/dev remote Chromium and its prof-8300 profile; do not target the Edge viewer or unrelated Chrome.
3. Force one GPU-process death with `taskkill /PID <confirmed-remote-gpu-pid> /F` (no /T). Alternative, read at pin: content/browser/renderer_host/debug_urls.cc:261-266 handles chrome://gpucrash by gpu_service()->Crash(). Use an extra remote tab for this URL so the original animated page remains available, then return to it if activation changes. PID injection avoids navigation side effects. Commands here are instructions only; this task did not execute them.
4. Expected evidence: GPU process exited/reinitialized; another `PRTS: establishing Viz capturer`; `PRTS: capture pool reset`; endpoint `capture reset; texture imports cleared, requesting IDR`; new buffer imports and increasing OnFrame counters; D3D source fps and viewer framesDecoded/rVFC resume on the SAME browser/endpoint/WebRTC session. No OpenSharedResource1 or AcquireSync failure loop. Inspect encoded output/decoder recovery to verify an IDR actually occurs; the callback merely requests one.
5. Target recovery within 5 s, fail at a sustained 10 s zero-fps window after GPU reinitialization (test acceptance criteria, not measured guarantees). Check input still works and dimensions/active popup remain correct. Compare no-crash latency/CPU with baseline because the wrapper adds a UI metadata/handle forwarding hop.
6. Repeat on a static page (fresh first frame despite no animation), during resize/DSF changes, while switching popup roots, and while closing the captured popup during the 100 ms retry. Close the viewer/browser while retry is pending; verify no orphan capture/lock, stale callback resurrection or UAF. Old generation StopIfCurrent must not erase a newly selected Session. Use two active Session consumers in a dedicated harness if validating simultaneous root recovery; the production endpoint captures only its selected root.
7. Repeat one or two spaced crashes in a fresh run; excessive rapid crashes can trigger Chromium's GPU fallback policy. Run both current Graphite configuration and --cr-disable-skia-graphite in separate trials; record actual backend. Keep crash windows out of normal latency percentiles. No full suite is required.

## Open risks / limits
- DRAFT ONLY: compile/link success, forced-crash recovery, IDR emission, frame/lease ordering under race, latency and CPU effects remain unverified. apply --check is syntax/context validation of the patch, not compilation.
- Recovery assumes Chromium reestablishes its normal HostFrameSinkManager GPU connection and uses the same hardware adapter. It intentionally reuses that upstream machinery; failed GPU startup or permanent service failure cannot be repaired by the capturer wrapper.
- Browser GPU-process death normally leaves the endpoint's independently created D3D11 device healthy [I]. Reset drops all imported browser textures/tokens/native import state. A physical driver TDR/device removal or adapter/LUID migration requires endpoint device/encoder reinitialization and refreshed HelloInfo; this patch does not implement that broader device-loss path. Software fallback remains unsupported by the existing ARGB/DXGI path; recovery can still stall if Chromium permanently falls back to CPU compositing.
- Existing capture ownership/generation guards remain. Resets are idempotent, including initial/reset/resize notifications; duplicate IDR requests are possible. An already copied private output may be delivered around a reset before the epoch check/consumer call boundary; imported browser handles are never reused across the ordered reset.
- Session keeps its capture lock intentionally during the retry. It is released on explicit StopCapture, root removal or controller link teardown. Wrapper weak retry callbacks die with the wrapper; endpoint reset callbacks are WeakPtr-bound and main-sequence posted.
- New forwarding hop passes only metadata/handles on UI; DXGI duplication and endpoint consumption remain off UI. Quantify scheduling cost before acceptance.
- No upstream hook groups were added. All new browser recovery code is in chrome/browser/prts/prts_capture.cc and reached only through StartCapture's existing ui::IsPrtsRemoteMode() guard. No build file or protocol change is needed (existing //components/viz/host dependency).

Completed draft and static verification at approximately 15:21 SGT, 2026-10-26. No live-chain validation performed.
