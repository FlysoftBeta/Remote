# PRTS prior-art survey

Survey date: 2026-10-25. Scope: read-only inspection of `C:/Users/Flysoft/Desktop/chromium`; no builds or changes to the prior project. Paths below are relative to that repository unless explicitly marked Chromium source paths.

## Evidence and scope

The new project's requirements come from `remotekit/design/00-brief.md`. Evidence labels used throughout:

- **Code:** implementation read in Rust/JS/PowerShell or Chromium format patches; establishes behavior/intention, not runtime success.
- **Documented:** measurements or acceptance results recorded by the prior project; not rerun during this survey.
- **Assessment:** portability, quality, and redesign judgments inferred from that evidence.

Entry points read: `README.md`, `docs/prts/STATUS.md`, `PLAN.md`, `chromium-hooks.md`, `protocol.md`, milestone/handoff notes, identity/MF/flyout/touch notes, and relevant journals. STATUS/PLAN contain superseded sections: newest dated summaries and actual branch/code take precedence over older “next action” text.

Read-only Git inspection found branch `wip/touch`, HEAD `26fb18cbb897642981086969d063f834b0fbf168` ("Qualify source-timed touch gestures and channel-loss recovery"). README, STATUS, journal-mvp, two screenshots, release-v0.1.1 notes and test runner have local modifications; `public-test/` is untracked. Findings describe this filesystem snapshot, not a clean release commit. Build outputs, dependency trees and large binaries were skipped.

## 1. What exists and which Chromium it uses

**Code:** `PIN = caef9db47e2cf3fe8e9f07ddceeff6ddf6957385`. Direct Gitiles reading of [`chrome/VERSION` at PIN](https://chromium.googlesource.com/chromium/src/+/caef9db47e2cf3fe8e9f07ddceeff6ddf6957385/chrome/VERSION) confirms **158.0.8111.0 (M158)**. This was upstream main, not a stable-channel pin: `cr pin latest` resolves `refs/heads/main`.

For the requested recent-stable reference, the [Windows Stable release endpoint](https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Windows&num=1) returned **157.0.8103.15**, Chromium commit **`1aa07b638799e59656559ae6c6649d4b197f555e`**. Its `chrome/VERSION` was independently read through Gitiles. This survey also read [`ui/views/win/hwnd_message_handler_headless.cc` at that exact stable commit](https://chromium.googlesource.com/chromium/src/+/1aa07b638799e59656559ae6c6649d4b197f555e/ui/views/win/hwnd_message_handler_headless.cc): `HWNDMessageHandlerHeadless::Init` retains the HWND and sets `DWMWA_CLOAK`; `Activate` changes logical activation through the delegate. No full compatibility/rebase claim follows from this limited stable-source check. `C:/src/cr-partial` existed but had no valid HEAD; it was not changed.

Architecture, verified in code and consistent with README:
```text
patched unified Chrome --headless=new (hidden/cloaked Windows HWNDs, Aura/Views)
  chrome/browser/prts: lifecycle + registries + root/popup capture + input
          | three anonymous pipes: command, event, frame descriptors
          | CPU read-only sections OR D3D11 NT shared keyed textures
          v
  prtsd (Rust): private frame copies -> scene composition -> H.264 encoder
          | WebRTC H.264 track + reliable control / unreliable feedback channels
          +-------------------- directly --------------------> web viewer
          | framed JSON stdin/stdout
          v
  server/ (Node HTTP + WebSocket signalling + vanilla JS test viewer)
```
The **three one-way browser pipes** are command (daemon -> browser: RPC/input/releases), event (browser -> daemon: replies/lifecycle/resource registration), and frame (browser -> daemon: descriptors only). Pixel data is out-of-band in shared resources. Node is outside the video/input fast path after WebRTC setup.

**Documented working:** browser lifecycle, tabs/navigation/resize, CPU and GPU capture, Intel MF hardware H.264 and software fallback, WebRTC, OOPIF input, real browser toolbar/omnibox/accelerators, common popup composition, PRTS branding and Chrome UA/CH identity. GPU qualification names UHD 630, vendor `0x8086`, device `0x9bc5`, LUID `0xe369`, SwiftShader=0.

Branch/release boundaries matter:

- Published `v0.1.0-preview`: 34 Chromium patches, documented Chromium tip `c14260952f`; clean-extract full40 result **38 pass, 2 deferred GPU omnibox geometry failures**. Earlier component full40 passed after an unchanged synthetic-loss rerun. This is a preview with explicit known issues.
- Current `wip/touch`: 36 patches, documented Chromium tip `5cc896e9d0`; component build6 and incremental non-component Release pass. All six final touch timing cases pass across GPU/QSV and CPU/software at DSF 1/1.25/2; quick2 passes. Touch is **not merged to main or published** in the inspected status.
- Touch full1: 40/46 pass initially; two scale2 touch failures fixed and targeted requalified; unchanged M2 rerun passes; known omnibox failures remain; strict CPU2 metadata-timeout rerun and clean-extract release qualification remain pending.
- `wip/e1-onevpl`, checkpoint `7c57d40`: parked unfinished work. STATUS records libvpl 2.17 dispatcher, API1.35 Intel runtime, native CPU/GPU and forced-WebRTC passes; MF comparison/full regression/final build receipt remain incomplete. Current working-tree encoder contains **MF + OpenH264 only**.
- `wip/i1-isolation`: parked; Job Object lifetime works in normal code, but AppContainer/LPAC/private desktop qualification is incomplete. Do not infer production tenant isolation from `isolation_profiles:["job"]`.

Known gaps: independent video tracks for extra browser windows; unqualified flyout families beyond app/context/select/omnibox; mobile automatic keyboard/editable-focus signalling; connected-desktop foreground/cursor qualification; Windows 10 runtime qualification; real device-removal recovery; direct oneVPL release readiness; TURN/remote-network/Chrome 89 qualification; measured 1080p tail-latency under the new network target. No audio, consistent with the new brief.

## 2. Patch inventory and reuse assessment

All 36 patches were inventoried by subject, touched files and hunks; capture/input/transport/branding changes were examined in detail. Added PRTS files were reconstructed **in memory** by sequentially applying their hunks to inspect their final form. No source checkout or patch application was performed on disk. Counts are added/deleted source lines excluding diff headers, not file bytes or net final diff. `+A/-D` counts include tests/string churn. WIP in a filename is historical and is not sufficient to determine release status.

Layer shorthand: C=`chrome`; H=`chrome/browser/headless` unified-headless integration (not old `headless/lib`); A=`ui/aura`; V=`ui/views`; Z=`components/viz`; T=`content`; D=`ui/display`; E=`components/embedder_support`. PRTS file abbreviations below expand under **`chrome/browser/prts/`**; all other paths are Chromium source paths. “As-is” means a sensible candidate on the **same PIN and opt-in mode**, not a promise that it applies unchanged to M157/stable. Later fixups should be folded into their originating feature.

### Registry and initial browser integration

| Patch file | Purpose | Touched files/layers | Size | Assessment |
| --- | --- | --- | ---: | --- |
| `0001-Add-PRTS-target-registry-and-per-window-viewport-con.patch` | Browser/tab discovery, stable page IDs, navigation/close/viewport and observation | C: `prts_target_registry.{cc,h}` | +650/-0 | **Reusable idea**: retain lifecycle semantics; start with window identity rather than page-target-first capture. |
| `0002-Tighten-PRTS-target-creation-and-bounded-string-pars.patch` | Creation and bounded string validation corrections | C: `prts_target_registry.cc` | +6/-4 | **Reusable as-is**, folded into registry. |
| `0003-Use-public-browser-creation-API-for-PRTS-targets.patch` | Replace version-sensitive browser construction with public creation/navigation API | C: `prts_target_registry.cc` | +28/-24 | **Reusable idea**; use target stable milestone's API and fold into 0001. |
| `0004-Preserve-PRTS-target-identity-when-Chrome-replaces-t.patch` | Preserve logical identity across WebContents replacement | C: `prts_target_registry.cc` | +21/-1 | **Reusable as-is**, folded; important observer/lifetime behavior. |
| `0005-Observe-ordinary-PRTS-tab-selection-changes.patch` | Follow native tab-strip selection, not only RPC activation | C: `prts_target_registry.cc` | +19/-3 | **Reusable as-is**, folded. |
| `0020-Add-isolated-PRTS-v2-negotiation-and-stable-window-a.patch` | Separate v2 negotiation, window/tab identifiers and registry | C: `prts_controller.{cc,h}`, `prts_pipe_client_win.{cc,h}`, `prts_protocol.h`, `prts_sources.gni`, `prts_target_registry.{cc,h}`, `prts_window_registry.{cc,h}` | +544/-9 | **Reusable idea**: v2 contract is the baseline; carrying parallel v1/v2 machinery is avoidable in a new product. |

### Pipe transport and lifecycle

| Patch file | Purpose | Touched files/layers | Size | Assessment |
| --- | --- | --- | ---: | --- |
| `0006-Add-bounded-Windows-PRTS-pipe-transport-and-protocol.patch` | Framing, handle adoption, bounded reader/writer threads, protocol/switches | C: `BUILD.gn`, `prts_controller.h`, `prts_pipe_client_win.{cc,h}`, `prts_protocol.h`, `prts_switches.h` | +512/-0 | **Reusable idea/code**; keep bounds and cancellation, replace ad-hoc numeric dispatch with named protocol definitions. |
| `0007-Bootstrap-PRTS-headless-controller-and-wire-browser-.patch` | Controller startup/shutdown, inherited-handle cleanup and keepalives | C/H: `chrome/BUILD.gn`, `chrome/app/chrome_main.cc`, `chrome/browser/BUILD.gn`, headless `BUILD.gn`, `chrome_browser_main_extra_parts_headless.{cc,h}`, `prts_controller.cc`, `prts_sources.gni` | +324/-5 | **Reusable idea**: correct unified-headless lifecycle hooks; simplify GN wiring on the new base. |
| `0016-Transfer-PRTS-pipe-thread-ownership-before-asynchron.patch` | Move thread ownership before asynchronous join | C: `prts_pipe_client_win.cc` | +3/-1 | **Reusable as-is**, folded into 0006. |
| `0033-WIP-retain-PRTS-pipe-cancellation-through-thread-poo.patch` | `BLOCK_SHUTDOWN` keeps cancellation/join alive through ThreadPool teardown | C: `prts_pipe_client_win.cc` | +3/-1 | **Reusable as-is**, mandatory lifetime fix folded into transport. |

### CPU capture and static-frame backpressure

| Patch file | Purpose | Touched files/layers | Size | Assessment |
| --- | --- | --- | ---: | --- |
| `0010-Add-bounded-Viz-CPU-readback-fallback-adapter-for-PR.patch` | ARGB Viz capture, read-only shared-section export and leases | C: `prts_capture.{cc,h}`, `prts_sources.gni` | +407/-0 | **Reusable idea/code** as software fallback; integrate root source from the outset. |
| `0011-Wire-PRTS-frame-subscriptions-and-shared-section-cre.patch` | Frame subscribe/release/controller wiring and resource credits | C: `prts_controller.cc`, `prts_pipe_client_win.cc`, `prts_target_registry.{cc,h}` | +85/-5 | **Reusable idea**, merge into common capture service. |
| `0012-Honor-PRTS-per-source-credits-and-connection-relativ.patch` | Per-source credits, HELLO-relative timestamps | C: `prts_capture.{cc,h}`, `prts_target_registry.cc` | +26/-13 | **Reusable as-is**, folded; retain clock/credit semantics. |
| `0013-viz-complete-failed-video-captures-before-retrying-r.patch` | Complete oracle capture accounting on failed/invalidated captures; passive-refresh regression | Z: `components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl{,_unittest}.cc` | +50/-9 | **Reusable idea**: inspect stable code first; upstream may have changed. Keep this as a separate upstream-facing fix if still required. |
| `0014-PRTS-resume-blocked-static-capture-when-frame-credit.patch` | Remember lost static updates and refresh on credit/resource transitions | C: `prts_capture.cc` | +29/-7 | **Reusable as-is**, folded; fixes permanent static-page stalls. |

### GPU/GMB and keyed-texture export

| Patch file | Purpose | Touched files/layers | Size | Assessment |
| --- | --- | --- | ---: | --- |
| `0015-Add-native-GMB-capture-and-keyed-D3D11-texture-expor.patch` | Native DXGI GMB capture, independent keyed export pool, CPU recovery | C: `chrome/browser/BUILD.gn`, `prts_capture.{cc,h}`, `prts_controller.cc`, `prts_d3d.{cc,h}`, `prts_sources.gni`, `prts_target_registry.{cc,h}` | +311/-33 | **Reusable idea/code**: conservative correct starting path; measure GPU-copy/wait cost. |
| `0017-Gate-PRTS-texture-negotiation-on-the-active-Chromium.patch` | Verify active GPU matches daemon LUID | C: `prts_controller.cc` | +14/-1 | **Reusable as-is**, folded. |
| `0018-Handle-pending-GPU-information-during-PRTS-handshake.patch` | Account for GPU information not ready at HELLO | C: `prts_capture.cc`, `prts_controller.cc` | +18/-1 | **Reusable idea**, preserve asynchronous startup behavior. |
| `0019-Recover-PRTS-native-capture-startup-and-probe-keyed-.patch` | Probe shared allocation, first-frame deadline, actual GPU checks, wait for copy completion | C: `prts_capture.{cc,h}`, `prts_controller.cc`, `prts_d3d.{cc,h}`, `prts_target_registry.cc` | +103/-58 | **Reusable idea/code**: retain safety; replace UI-thread GPU polling only with proven asynchronous lifetime control. |
| `0021-Capture-PRTS-browser-roots-with-independent-virtual-.patch` | Whole-root capture and per-window virtual monitor/DSF sizing | C/A: `prts_capture.{cc,h}`, `prts_controller.cc`, `prts_window_registry.{cc,h}`; `ui/aura/BUILD.gn`, `headless_root_size.h`, `window_tree_host.cc` | +387/-37 | **Reusable idea** and core baseline: captures browser chrome. Revisit synthetic-display placement and minimize generic Aura additions. |
| `0027-WIP-reject-stale-root-surfaces-during-window-resize.patch` | Reject resized output made from an old root surface/letterboxing | C: `prts_capture.cc` | +14/-1 | **Reusable as-is**, folded into capture. |

### Content input, Aura window input and touch

| Patch file | Purpose | Touched files/layers | Size | Assessment |
| --- | --- | --- | ---: | --- |
| `0008-Add-generic-synthetic-widget-input-and-selected-curs.patch` | Page-level RWH synthetic input/hit testing and cursor observer API | T: `content/browser/BUILD.gn`, `renderer_host/render_widget_host_view_aura.cc`, `synthetic_input.cc`; `content/public/browser/BUILD.gn`, `render_widget_host_cursor_observer.h`, `synthetic_input.h` | +315/-0 | **Drop page-only path** for new v2 product; retain OOPIF/ACK/cursor findings. It coexists with later Aura input and increases public API surface. |
| `0009-Implement-PRTS-input-notifications-and-root-cursor-e.patch` | Legacy page-input adapter, notifications/cursor and controller wiring | C: browser `BUILD.gn`, `prts_controller.cc`, `prts_input.{cc,h}`, `prts_sources.gni`, `prts_target_registry.{cc,h}` | +532/-5 | **Drop legacy adapter**, reuse validation/held-state cleanup and cursor semantics. |
| `0023-Route-PRTS-window-input-through-Aura-with-renderer-c.patch` | Window-level mouse/wheel/key/text, renderer completion barrier, focus/cursor side-effect guards | C/T/V: `prts_controller.cc`, `prts_sources.gni`, `prts_target_registry.cc`, `prts_window_input.{cc,h}`, `prts_window_registry.{cc,h}`; content browser/public `BUILD.gn`, `aura_input_barrier.{cc,h}`, `render_widget_host_view_event_handler.cc`; Views `desktop_window_tree_host_win.cc`, `hwnd_message_handler.cc` | +1071/-59 | **Reusable idea**: correct injection layer; barrier/public-content instrumentation and serialized waits need major simplification/measurement. |
| `0035-Add-genuine-multi-touch-to-the-remote-Aura-window-in.patch` | TOUCH wire/capability, ten contacts, genuine Aura touch and cancellation | C: `prts_controller.cc`, `prts_window_input.cc` | +95/-3 | **Reusable idea/code**, WIP runtime evidence only; carry along matching Rust/JS changes. |
| `0036-Preserve-remote-finger-timing-through-queued-Aura-to.patch` | Source event timing mapped to monotonic browser clock for fling velocity | C: `prts_window_input.cc` | +26/-5 | **Reusable as-is**, folded into touch; preserve validation and clock mapping. |

### Popups, virtual desktop behavior and diagnostic residue

| Patch file | Purpose | Touched files/layers | Size | Assessment |
| --- | --- | --- | ---: | --- |
| `0022-Suppress-native-desktop-side-effects-for-opt-in-head.patch` | Opt-in cursor/input virtualization, avoid OLE drag/native external protocols, printing off | C/V: `external_protocol_handler.cc`, `prts_controller.cc`, `prts_target_registry.cc`, `prts_window_registry.cc`; `ui/base/ui_base_features.cc`, Views `BUILD.gn`, `desktop_drag_drop_client_win.cc`, `desktop_headless_input.h`, `desktop_window_tree_host_win.cc`, `hwnd_message_handler.cc` | +163/-12 | **Reusable idea**: essential desktop-side-effect suppression. Generic `disable-cursor-event-hook` switch is appended by PRTS; remove unrelated formatting churn in external-protocol code. |
| `0024-WIP-track-owned-headless-popup-hosts-and-expose-life.patch` | Host readiness/lifetime/stack observers and owner tracker | C/V: `prts_host_tracker.{cc,h}`, `prts_sources.gni`, `prts_window_registry.cc`; `desktop_headless_input.h`, `desktop_window_tree_host_win.cc` | +360/-1 | **Reusable idea/code**; unify browser/popup sources and avoid process-global callback ownership where possible. |
| `0025-WIP-correct-virtual-flyout-geometry-and-route-owned-.patch` | Popup geometry, synthetic-monitor lookup/rounding, owned-host input | C/D/V: `prts_window_input.{cc,h}`, `prts_window_registry.cc`; display/win `scaling_util{,_unittest}.cc`, `screen_win.cc`, `screen_win_headless.{cc,h}`, `screen_win_headless_unittest.cc`; Views `menu_controller.cc`, `hwnd_message_handler_headless.cc` | +305/-212 | **Reusable idea**; keep verified geometry fixes, audit test hooks and broad display changes against stable. |
| `0026-WIP-capture-and-compose-owned-popup-scenes-with-work.patch` | Scene/layer export, alpha, fair two-resource scheduling and correct mouse release | C: browser `BUILD.gn`, `prts_capture.{cc,h}`, `prts_controller.{cc,h}`, `prts_host_tracker.{cc,h}`, `prts_window_input.cc`, `prts_window_registry.cc` | +492/-118 | **Reusable idea/code**: daemon composition is proven for four families; clean registry/scene coupling. |
| `0028-WIP-add-read-only-omnibox-geometry-and-align-DOM-out.patch` | Omnibox diagnostic geometry; root-consistent DOM outer size; post first barrier poll | C/T: browser `BUILD.gn`, `prts_window_registry.cc`; content `aura_input_barrier.cc`, `render_widget_host_view_aura.cc` | +127/-3 | **Split**: reuse outer-size fix; **drop** omnibox internals/probe protocol from production; reassess barrier scheduling. |
| `0034-WIP-disable-Aura-window-animations-for-immediate-PRT.patch` | Existing animation-disable switch prevents non-component select-close UAF | C: `prts_controller.cc` | +5/-0 | **Reusable as-is**, folded into opt-in policy; keep Release regression. |

### Product branding and web identity

| Patch file | Purpose | Touched files/layers | Size | Assessment |
| --- | --- | --- | ---: | --- |
| `0029-WIP-brand-PRTS-resources-and-preserve-Chrome-web-ide.patch` | Standard PRTS branding/strings plus Windows UA/CH identity | C/E: `chrome/app/chromium_strings.grd`, `chrome/app/theme/chromium/BRANDING`; `components/embedder_support/user_agent_utils.cc` | +620/-615 | **Reusable idea**: tiny identity hook is good; large blanket string churn should be scoped and regenerated carefully. |
| `0030-Preserve-Chromium-copyright-attribution-in-PRTS-bran.patch` | Restore original Chromium copyright attribution | C: `chrome/app/chromium_strings.grd` | +2/-2 | **Reusable as-is**, fold into branding. |
| `0031-Use-a-build-guard-for-the-Windows-headless-UA-produc.patch` | Compile-time Windows guard for headless UA product behavior | E: `components/embedder_support/user_agent_utils.cc` | +3/-2 | **Reusable as-is**, fold into identity; add appropriate new-product scope. |
| `0032-Brand-the-standard-Settings-string-part-as-PRTS.patch` | Settings/About PRTS strings omitted by first branding pass | C: `chrome/app/settings_chromium_strings.grdp` | +121/-121 | **Reusable idea**, fold into branding and retain attribution. |

Assessment in this table is task-specific: source-preserving fixes can be reused, but the new project's Aura/minimal-patch requirement argues against importing legacy v1 and test-only Chrome internals wholesale.

## 3. Frame-export design actually implemented

**Code anchors:** Chromium `PrtsCapture::Impl::Source` in `prts_capture.cc`; `PrtsD3D::{Initialize,Create,Copy}` in `prts_d3d.cc`; daemon `browser_frames::Resources::process`, `d3d::Device::{open,finish,copy_shared,readback,nv12}`, `scene_frames.rs`, `d3d_composition.rs`.

Capture source and pacing:

- Legacy v1 obtains the active page's capturer from its RenderWidgetHostView and uses `WebContents::IncrementCapturerCount` to keep hidden/background content rendering.
- V2 captures **Aura root `FrameSinkId`**, not a page subtree: `root->GetHost()->compositor()->context_factory()->GetHostFrameSinkManager()->CreateVideoCapturer()`, then `ChangeTarget(viz::VideoCaptureTarget(current), 0)`.
- It holds `WindowTreeHost::CreateVideoCaptureLock()`, observes root destruction, and rebuilds capture if the sink changes. Root readiness is checked by a 16 ms timer; legacy page source uses 100 ms.
- Capturer type is `viz::ClientFrameSinkVideoCapturer`, backed by Viz `FrameSinkVideoCapturerImpl`; format `media::PIXEL_FORMAT_ARGB`, fixed resolution, `SetMinCapturePeriod(1/fps)`, no auto-throttling, explicit initial/static refresh.
- GPU chooses **`kPreferMappableSharedImage`**; CPU chooses `kDefault`. This is native GMB export through Viz, not a custom display output device, desktop duplication, DWM screenshot, or external BeginFrame driver.

GPU handoff sequence (verified):

1. Viz delivers `VideoBufferHandle::gpu_memory_buffer_handle`, type `gfx::DXGI_SHARED_HANDLE`, and a `FrameSinkVideoConsumerFrameCallbacks` lease. Active GPU/LUID and dimensions are checked.
2. `PrtsD3D::Copy` opens Viz's BGRA allocation using `ID3D11Device1::OpenSharedResource1`; acquires the **source key 0**. The dedicated browser export texture is BGRA8, `SHARED_NTHANDLE | SHARED_KEYEDMUTEX`; acquire its **key 0**.
3. Browser GPU-copies the visible rect into the export allocation, waits on `D3D11_QUERY_EVENT` (poll + 1 ms sleep, bounded), then returns Viz's **key 0** and publishes export **key 1**. This isolates Viz's key0 convention from the wire's key0/1 protocol. Viz lease is returned after the source read completes.
4. Resource registration sends the browser's NT handle value, size/format/LUID. **The daemon duplicates the browser handle into itself** using its browser process handle; pixel bytes never enter the pipe.
5. Daemon validates allocation/adapter, acquires export **key 1**, GPU-copies into a private texture, waits for its GPU event, releases **key 0**, then sends `FRAME_RELEASE` (`0x320`). Encoder latency therefore does not retain Chromium pool credit.
6. Daemon privately composes popup sources, converts BGRA to NV12 using D3D11 video processing, wraps NV12 as an MF DXGI-surface sample, and waits for the corresponding H.264 output. Hardware path has no CPU Map/readback of pixels.

“Zero-copy” stats (`d3d11-zero-copy`) mean **no CPU pixel transfer into the hardware encoder**. Viz capture, browser export, daemon private copy, scene render and NV12 conversion are real GPU work. Browser copy completion is synchronous on the capture/UI-side path; daemon `Device::finish` polls with 1 ms sleeps and a one-second deadline. This is a correctness-first implementation, not an optimal latency floor.

CPU fallback:

- Viz read-only shared-memory frames are mapped, visible rect/stride checked, ARGB/ABGR normalized into a browser-owned read-only section; CPU popup alpha is retained in the later scene path.
- Daemon `DuplicateHandle` + `MapViewOfFile(FILE_MAP_READ)` verifies layout, size and committed readable pages with `VirtualQuery`; row-copies into private BGRA, then releases the browser credit.
- CPU -> MF uses BT.601 limited-range conversion and NV12 memory samples. CPU -> OpenH264 uses I420. GPU capture -> software uses an explicitly reported **staging readback**; it must not be called the hardware fast path.

Bounds/backpressure and recovery:

- Global export pool remains **two resources**, negotiated one/two descriptor credits; per-source fairness prevents a changing base from starving a static popup. Resource registration/unregistration have ACKs and five-second deadlines.
- Pending frames coalesce **per source/target**, not globally; renderer updates blocked by resources/credits are remembered and trigger `RequestRefreshFrame` when capacity returns.
- Root resize changes generations and rejects captures whose `metadata.capture.source_size` or full `content_rect` does not match the requested root, preventing old surfaces letterboxed into new metadata.
- GPU capability requires matching adapter LUID, hardware/non-SwiftShader checks and a real shared-keyed-allocation probe. Missing native first frame after five seconds or safe pre-acquisition import rejection switches the source to CPU.
- After unsafe acquired-mutex/device-loss failure, the protocol requires teardown/restart rather than fabricating releases. GPU “force” configuration still permits CPU recovery; actual stats must be checked.

Why this design: reuse Viz's capture scaling/pooling/pacing/Mojo path, capture Chrome UI at the root, separate source lease lifetime from an encoder/network consumer, and avoid a Windows Ozone port. Earlier direct capture-to-encoder import was not demonstrated; the two private-copy boundaries intentionally simplify ownership.

Documented cost/latency (not rerun):
| Measurement | GPU path | Comparison / limitation |
| --- | --- | --- |
| M4 fixed 4 Mbps, 1080p QSV harness | 29.87 fps; 13.87 ms conversion/encode; 18.36% pipeline CPU | CPU capture/QSV: 29.85 fps; 22.74 ms conversion/encode; 53.08% pipeline CPU. CPU percentages use one-core equivalents; excludes WebRTC/control plane. Historical before later MF tuning. |
| Later MF request latency, 720p immediate submission, 180/180 immediately decoded | mean 9.281 ms; p95 10.737; p99 11.769; max 13.964 | CPU BGRA: mean 10.656; p95 11.862; p99 38.424. Includes conversion + MTA/MFT dispatch/output, excludes capture/network/display. |
| Build47 popup decoded appearance, 24 samples | 86.25–248.13 ms | CPU/software 69.55–262.33 ms, report-only. This is input-to-popup appearance, not a complete E2E distribution. |
| Final preview artifact popup samples | 72.55–239.49 ms (GPU, STATUS) | CPU 50.38–278.02 ms; geometry issues explicitly deferred. |

The fixed harness and adaptive real-daemon measurements are not interchangeable. `phase2-m4.md` records real-daemon GPU/QSV at 1080p: 23.25 encoded fps, 14.496 ms encode, 16.90% daemon CPU and 46.05% browser CPU. Its CPU-capture comparison fell to software when REMB fell to 332110 bps (20.17 fps, 25.478 ms), so that is not a clean GPU-vs-CPU QSV comparison. Earlier M3 static-page PLI -> fresh decoded IDR was documented at 93.70 ms (`phase2-m3.md`/STATUS), which measures refresh/recovery, not normal input-to-display latency. No documented 1080p/10–16 Mbps/50 ms RTT P80<100/P99<200 result was found.

## 4. Input, focus, touch and editable fields

**Code:** two generations coexist. `content/public/browser/synthetic_input.h` + `content/browser/renderer_host/synthetic_input.cc` and `PrtsInput` are the legacy page/RWH path. V2 is `PrtsWindowInput`, dispatched into **`aura::WindowTreeHost::SendEventToSink`**; ordinary Aura/Views routing reaches browser controls, renderer widgets and OOPIFs. No `SendInput` or OS click synthesis is required.

V2 mechanics:

- Input addresses window/source generation, carries root DIP coordinates, and is translated to the chosen host; `ConvertDIPToPixels` occurs once before Aura's inverse dispatch transform.
- Pointer routing honors `DesktopCaptureClient::GetCaptureWindowGlobal`, then owned hosts in virtual z order. Hover enter/exit, held buttons, changed-button release flags, click count and wheel/modifier state are tracked.
- Keyboard uses synthesized `ui::KeyEvent` (`EF_IS_SYNTHESIZED`) with DOM code/key mapping, preserving Views accelerators such as Ctrl+T/L/W while bypassing the server's native IME queue.
- `KeyRoot()` prefers owned global capture, then active input-accepting owned widget, then browser root. `Focus(true)` activates the Views Widget; blur resets held input and deactivates logically. The headless HWND handler implements logical activation rather than OS foreground changes.
- Text resolves host `InputMethod::GetTextInputClient`; rejects `TEXT_INPUT_TYPE_NONE`; single character goes through `InsertChar`, multi-character committed Unicode through `InsertText`. Content Enter handling is special-cased to produce exactly one commit.
- `content::DispatchWithAuraInputBarrier`/`FinishAuraTextCommit` serialize input against renderer routing/ACK and visual-state completion. There is a five-second action deadline and reset cancellation. This adds content-layer code and can increase head-of-line latency; it deserves review for production necessity versus acceptance-test ordering.
- Reset releases held keys/buttons, cancels contacts, exits hover, cancels pending completion work. Control loss/channel closure, generation changes, shutdown and root destruction all matter.

**Touch code in 0035/0036 and matching daemon/viewer:** wire TOUCH `0x208`, v2 touch capability bit64; ten bounded contacts with stable IDs, down/move/up/cancel and pressure. Each contact chooses its host on down and retains a weak reference to that root for subsequent events. Builds `ui::TouchEvent` with `ui::PointerDetails(kTouch, ...)`, not mouse emulation; Chromium gesture recognition supplies tap/long press/scroll/fling/pinch. Reset dispatches `kTouchCancelled` to each surviving original host.

Frontend `server/public/input.js` uses touch PointerEvents/pointer capture and prevents local video gestures; validates capabilities, tracks contacts, and cancels them on ownership loss. Source `event.timeStamp` survives JS -> daemon -> queued native dispatch. Browser maps the first source time to `base::TimeTicks`, preserves relative timing, bounds/clamps future time, and enforces monotonically increasing stamps. Dispatch-time stamping previously suppressed high-DSF flings.

**Editable-focus answer:** there is an internal **lookup/check** of current `TextInputClient` during commit, but **no end-to-end editable-focus notification contract** was found. Remote `focus` means viewer ownership/activation, not “focused control is editable.” No published field type/inputmode/selection/composition state drives a client soft keyboard. Viewer uses a local textarea for desktop key/IME commit; touch-down does not establish an automatic remote-editable keyboard policy. The new brief's automatic keyboard requirement remains work.

IME is committed-text forwarding, not full remote preedit/candidate/caret mirroring. Native file picker is existing headless cancellation; system drag/drop, native move loops, external protocol UI and unsupported OS surfaces need explicit policy, not synthetic input.

## 5. Exactly what branding and UA patches do

**Product changes, verified from 0029/0030/0032:**

- `chrome/app/theme/chromium/BRANDING`: `PRODUCT_FULLNAME` and `PRODUCT_SHORTNAME` become `PRTS`; installer full/short names become `PRTS Installer`. Company names remain `The Chromium Authors`, copyright remains original, Mac bundle ID/creator are unchanged.
- `chrome/app/chromium_strings.grd`: hundreds of application-facing occurrences become PRTS: product/application/window titles, About/version product references, menus, prompts and other Chromium-branded UI. This is much broader than a title-only edit: includes platform-specific/mobile promotional copy irrelevant to the Windows cloud product.
- `0030` restores `IDS_ABOUT_VERSION_COMPANY_NAME` and `IDS_ABOUT_VERSION_COPYRIGHT` to **The Chromium Authors** after 0029's overly broad replacement changed them to “The PRTS Authors.”
- `chrome/app/settings_chromium_strings.grdp`: 121 replaced lines cover “About PRTS”, update/help/default-browser/settings/sign-in/product references. The included GRDP was missed by the initial top-level GRD pass.
- No Google-branded icons/artwork, Google build flag, executable rename, custom installer implementation or change to Chromium copyright is provided. Standard Windows version-resource generation consumes BRANDING; tests inspect the generated executable resources. Other locales/platforms are not independently qualified here.

**Web changes, exactly two behavioral hooks in `components/embedder_support/user_agent_utils.cc`:**
```cpp
// GetUserAgentBrandList(): Windows independent of product branding.
#if BUILDFLAG(IS_WIN)
  brand = "Google Chrome";
#elif !BUILDFLAG(CHROMIUM_BRANDING)
  brand = version_info::GetProductName();
#endif
// GetUserAgentInternal(): preserve Chrome/<version> product on Windows.
#if !BUILDFLAG(IS_WIN)
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(kHeadless))
    product.insert(0, "Headless");
#endif
```
0031 changes the initial constant-false runtime branch into the build guard above to satisfy Chromium unreachable-code diagnostics; semantics are unchanged. These hooks apply to **all Windows builds using these source changes**, not just a `--prts-pipe` process. Scope this deliberately when porting.

The existing UA-reduction/platform/GREASE/permutation pipeline does the rest. It does not remove the Chromium member from UA-CH: expected brands are **Google Chrome + Chromium + GREASE**, just like Chrome. At M158 the documented order is `Chromium/158`, `Not.A/Brand/24`, `Google Chrome/158`; full Chrome/Chromium versions are `158.0.8111.0`, GREASE `24.0.0.0`.

Documented reduced UA: `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/158.0.0.0 Safari/537.36`. Platform Windows, mobile=false, architecture=x86, bitness=64, wow64=false remain upstream values; platform-version comes from UniversalApiContract (19.0.0 on the test host), not a new hardcoded OS string.

There are no per-site headers or JS `navigator` overrides. `e2e-identity.js` checks page/dedicated/shared/service-worker UA and low/high-entropy JS metadata; page request UA-CH; webdriver=false/window.chrome/PDF plugins; consistent nonzero sizes. Worker HTTP requests naturally omit page-style UA-CH on this PIN, corroborated with stock Edge; the gate requires that absence rather than inventing headers. `e2e-branding.js` checks Windows resources and internal About/version pages separately. The identity patch does not itself hide webdriver or implement a general anti-detection layer.

Patch0028 additionally makes DOM outer bounds respect the logical root canvas. That geometry fix supports consistent web identity but is separate from the two UA hooks. No Chromium automation/debugging switches are needed in the production identity launch.

## 6. Daemon, media, control plane and viewer

### Module map (current working tree)

| Module / path | Responsibility verified in code |
| --- | --- |
| `prtsd/src/main.rs`, `lib.rs` | Tokio daemon entry/module wiring; Windows-specific modules guarded with cfg. |
| `transport.rs` | Length-prefixed JSON stdin/stdout with blocking I/O workers, bounded serialized output, seq/time metadata and EOF handling. |
| `daemon.rs`, `model.rs` | Central state actor; options/IDs/ownership/lifecycle; input validation; bitrate/network/stats policy; async browser start/stop and viewer state. |
| `browser.rs` | Three inherited pipes, `STARTUPINFOEX`, browser Job/process lifetime, handshake/RPC/heartbeats, event/frame readers, v1/v2 projection, resource/scene routing. |
| `browser_wire.rs`, `window_wire.rs` | Binary codecs and validation; v1 pages; v2 windows/tabs/layers/scenes/capabilities, including touch. |
| `browser_frames.rs` | Read-only section or keyed texture import/validation; private copy; per-source coalescing; browser credit release. `CpuFrame` can contain a GPU texture despite its name. |
| `frame_source.rs` | `FrameSource` trait, `Synthetic`, `BrowserSource`; subscription, forced refresh, epoch/size validation and idle unsubscribe. |
| `scene_frames.rs`, `composition.rs` | Private scene caches; revisions/epochs, latest base plus popup blending, CPU reference and bounded cache accounting. |
| `d3d.rs`, `d3d_composition.rs`, `composition.hlsl` | Matching hardware adapter, shared import/copy/completion, staging fallback, padded NV12 processing and shader scene composition. |
| `encoder.rs`, `mf_encoder.rs` | `VideoEncoder`, `Selected`, software OpenH264 and MF worker/low-latency hardware implementation; sticky fallback. |
| `video.rs` | One encoder worker per instance, wall-clock pacing, latest encoded watch slot, epochs, stats and synthetic fixture rendering. |
| `viewer.rs`, `shaping.rs` | Per-viewer PeerConnection/data channels/H.264 track/RTCP telemetry; real encrypted UDP delay/loss shaper. `shaping.rs` is included from viewer with `#[path]`. |
| `src/bin/browser_m1..m5.rs`, `bin/support/*`, `tests/*` | Native acceptance drivers, UI fixtures, desktop probes, encoder/lifetime/odd-size/rate-control checks. Useful evidence, not the daemon's production loop. |

Dependency versions are repository facts from `Cargo.toml`, not recommendations to install today's latest: Rust edition 2024, **webrtc 0.21.1 + rtc 0.21.1**, Tokio 1.53.3, **windows 0.62.2**, **openh264/openh264-sys2 0.9.9**. `webrtc` is the async wrapper over its Sans-I/O `rtc` core; this is not libwebrtc C++, str0m, or old webrtc-rs0.14 APIs. Cargo.lock exists; no lockfile re-resolution was run.

Browser lifetime and transport:

- `RawBrowser::launch_configuration` creates three synchronous anonymous pipes; explicit `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` limits inheritance. Browser bootstrap clears inherited flags before spawning children. No inherited daemon-process handle; daemon retains CreateProcess's browser handle with duplication/lifecycle rights.
- `PROC_THREAD_ATTRIBUTE_JOB_LIST` assigns a kill-on-close Job Object **atomically at creation**, with `CREATE_SUSPENDED` followed by resume. This closes the parent-death race of assigning the job after launch. It is process lifetime containment, not AppContainer tenant isolation.
- Launcher uses `--headless=new --disable-features=UiGpuRasterization --no-first-run --no-default-browser-check --prts-pipe=...`. UI tiles are CPU-rasterized to avoid cold shader stalls; page GPU/compositing/WebGL/texture capture remain enabled. Trusted environment overrides select executable/backend; viewer requests do not accept arbitrary paths or switches.
- Wire envelope: LE u32 length, magic `0x53545250`, major/minor, type/flags, u64 request ID; 20-byte minimum, 1 MiB maximum. Odd daemon/even browser request IDs; bad records/EOF/stalls terminate the connection.
- Chromium transport has dedicated reader and writer threads and 1024-message/8 MiB incoming/outgoing bounds; frame/event pipes share the serialized writer queue, so separate pipes do not imply independent writer scheduling. Cancellation uses `CancelSynchronousIo` before join; final shutdown work is `BLOCK_SHUTDOWN`.
- Startup has a separate configurable 300-second limit (5..600), `WaitForInputIdle` before HELLO; normal HELLO/RPC remains five seconds. Cold component DLL loading is not mistaken for an accepted slow protocol request.

### Encoders and actual low-latency settings

**Current QSV = Intel Media Foundation MFT**, not direct oneVPL. `Selected::new` tries MF whenever preference is not `software`; errors warn and fall back. Even `qsv` is a preference with fallback, not a hardware guarantee. `video.rs` records actual backend and `encoder.input_path`, preserving software fallback across resize.

MF code runs COM/MF on a dedicated MTA; hardware-only `MFTEnumEx` selection prefers Intel, discovers `MFT_TRANSFORM_CLSID_Attribute`, then **`CoCreateInstance`** creates the transform. It deliberately avoids `IMFActivate::ActivateObject` due to the measured device-catalog watcher leak. Async MFT is unlocked; NeedInput/HaveOutput/stream-change are serviced with bounded polling and deadlines.

Mandatory controls: H.264 Baseline, CBR, low latency=true, B pictures=0, GOP=`fps*2`, mean/max bitrate, startup/requested IDR. Best-effort maxQP51 and DisplayRemoting hint; no enabled intra-refresh mode in the current MF path. HRD buffer is **`max(ceil(bps/80),32768)` bytes**, read back after negotiation alongside GOP/B/RC/low-latency settings. At 4 Mbps this is 100 ms; at 150 kbps the floor is 1.75 s, not 100 ms. That is capacity/configuration, not measured residency.

One request is outstanding on the MF worker: output sample time must equal its input time; waits up to two seconds for the matching picture rather than admitting a hidden startup frame queue. CPU BGRA becomes padded NV12 memory; GPU BGRA becomes D3D11 NV12 with `MFCreateDXGISurfaceBuffer` and device manager. Intel GPU dimensions are padded to multiples of4, even aperture plus viewer crop preserves odd visible dimensions; CPU/software edge replication differs and has dedicated pixel checks.

Bitrate update recreates/reconfigures MF and forces SPS/PPS+IDR because the driver's successful live property write did not reliably change real output. After one second of media warmup, a subsequent one-second output window rejects measured bitrate >150% of target; failing hardware switches to software. At150kbps documented native output461344bps fails, software155057bps passes. Rate cap is not a strict packet token bucket.

OpenH264 settings in current code: bundled-source 0.9.9, `CameraVideoRealTime`, bitrate RC, Baseline, low complexity, two-second intra period, up to 4 workers (`min(available_parallelism,4)`), frame skipping only below 150 kbps. Even BGRA uses packed SIMD converter; odd dimensions use BT.601 limited-range edge replication. Live `ENCODER_OPTION_BITRATE` and `ENCODER_OPTION_RC_FRAME_SKIP` preserve prediction/RC history; repeated REMB changes do not recreate it or force every frame to IDR. Explicit keyframe requests still force intra.

**Parked oneVPL candidate, verified without branch checkout:** read `git show wip/e1-onevpl:prtsd/{build.rs,src/vpl_encoder.rs,native/vpl.cpp}`. `build.rs` compiles the C++ bridge and official vendored dispatcher statically, linked to Windows D3D/system libraries; current touch branch has none of these files. Rust wraps an owned native encoder and shared D3D device; bridge uses external D3D11 surfaces/allocator, NV12, `MFX_IOPATTERN_IN_VIDEO_MEMORY`, AsyncDepth=1, Baseline, best-speed target usage, GopRefDist=1/NumRefFrame=1, fps*2 GOP and LowDelayBRC.

Its RC is **capped VBR with target=max**, not CBR: source comment states LowDelayBRC supports VBR/QVBR/VCM. Readback requires VBR/LowDelayBRC/AsyncDepth=1/refdist1/video-memory input. HRD is `max(2,ceil(bps/80000))` KB (100ms nominal), half-buffer initial delay. Optional vertical intra refresh uses cycle=max(2,fps) and long GOP. EncodeFrameAsync is immediately synchronized (`SyncOperation`,1000ms), rejects buffered/no-sync/B/reordered output; bitrate Reset can close/reinit in the same session. This is a promising **separate candidate**, not evidence the released daemon fulfills oneVPL requirements. Documented E1 tests include900 immediate CPU and GPU frames each and20 stable239-handle cycles; comparative latency work remains open.

### Pacing, WebRTC and network behavior

`Video::start` creates **one worker/encoder per instance**, shared by viewers of the currently selected target/window. It captures only with viewers, unsubscribes while idle, sleeps to `1/fps` deadlines, and skips missed periods rather than accumulating catch-up work. Resize/activation epochs reject stale encoded work at publication. This is independent Viz+daemon pacing; it does **not** drive Chromium external BeginFrames or synchronize presentation to encoder availability.

Encoded output is a Tokio watch latest-frame slot. Each viewer has its own PeerConnection, RTP state and task; skipped **encoded** access units request a new IDR, while deliberate capture/encode skips do not manufacture prediction loss. RTP media time advances by actual elapsed frame capture/ingress time across skips; an empty sample advances StaticSample's timestamp before the access unit. Submission timeout is100ms.

H.264 SDP only: `video/H264`,90kHz, `packetization-mode=1;profile-level-id=42e01f;level-asymmetry-allowed=1`, initial payload102. PLI/FIR force IDR; REMB becomes an encoder cap combined with instance/network caps by min(). No full TWCC/GCC controller or explicit RTP playout-delay extension was found in application code. The viewer best-effort sets `playoutDelayHint=0`/`jitterBufferTarget=0` when present. No audio track. No application-level packet pacer/token bucket was found; RTP transmission is through the crate, with optional test delay/loss shaping.

Default PeerConnection binds UDP `0.0.0.0:0`, host ICE; server offers, viewer answers and trickles ICE, with pre-answer candidate queues and ufrag filtering. No qualified TURN deployment; remote-network/firewall behavior and Chrome 89 decoder/SDP compatibility remain unproven. Advertising level3.1 does not itself establish 1080p interoperability; resolve actual SPS/receiver limits in the new project.

Shaper wraps `Runtime::wrap_udp_socket` after encryption: selects SRTP/SRTCP datagrams by RFC7983 first-byte classification, including retransmissions; delay + seeded packet loss,4MiB/session queue, oldest-drop overflow, flush/reseed on replacement, worker abort on close. ICE/STUN/DTLS/SCTP/input/inbound traffic bypass it. Thus this test facility is **not a symmetric 50 ms RTT+jitter emulator**; ICE RTT need not change with media-delay settings.

Stats limitation verified in code: `browser_frames.rs` stamps `received = Instant::now()` around daemon ingress; `BrowserSource::capture` passes `frame.received` as `capture`, and scene output uses the newest cached ingress time. Browser `timestamp_us` is retained but not used here to establish browser-capture-to-display latency. `capture_encode_ms`/send age are therefore ingress-to-stage metrics on browser sources, not full capture age. Viewer reports decode/jitter-buffer/getStats and explicitly says capture-to-display unavailable. This prevents interpreting attractive encoder stats as the requested E2E result.

### Node and viewer protocol

`server/package.json`: Node>=22, ESM, runtime dependency `ws^8.18.0`; dev `puppeteer-core^25.14.0`. `server/index.js` serves an explicit static allowlist, defaults `PRTS_BROWSER_PROTOCOL=2`, launches `Daemon` from `daemon.js`, validates loopback Host/same-origin WS upgrade, and bounds clients/messages/requests. This is a local-operator sample, not a deployed multi-tenant/authenticated service.

Node <-> daemon: LE u32 byte length + UTF-8 JSON,4MiB record and8MiB queue bounds. Request `{v:1,id,method,params}` -> matching result/error; events carry sequence/time/data. `hello` advertises implemented sources/encoders/limits. JSON represents browser u64 identity/generation as decimal strings. Current capabilities:8 instances,32 sessions,4096x4096 axes with8,294,400-pixel total cap, fps1..60(default30).

WS relays lifecycle/RPC/signalling (`instance.*`, `target.*`, `session.*`, `network.set`, `stats.configure`); each WS owns its viewer session and disconnect closes it. A client-supplied viewer ID is replaced with a server UUID. Single control owner per instance; view-only peers share video. Node's `target.*` compatibility API projects v2 **windows**; real tab metadata exists inside the window contract. Additional windows are selectable through the same stream, not concurrently independent tracks.

WebRTC data channels use protocol `prts-json-v1`: reliable ordered **`prts.control.v1`** for ready/target/cursor/input/errors, unordered `maxRetransmits=0` **`prts.feedback.v1`** for telemetry. Input includes monotonically increasing seq, target/generation and event fields; stale/unauthorized control is rejected and held input released on loss. Discrete keys/touch do not use unreliable feedback.

Viewer files: `public/app.js` lifecycle/signalling/track setup, `input.js` remote input + local textarea/IME commits, `stats.js` interval getStats/feedback, `video-crop.js` odd padded video crop. `mapPoint` accounts for contain letterboxing, displayed visible size and logical root DIP; size/generation changes reset input, same-epoch title/load updates retain it. Cursor shapes/visibility are metadata/CSS; custom cursor is not advertised as supported. One visible video/viewer session, manual reconnect; auto recovery/independent multiwindow UI remains work.

## 7. Patch-maintenance and build workflow

**Code:** `cr.cmd` is a minimal `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/cr.ps1 %*` shim. Infra repo holds PIN/patches/daemon/server; actual full checkout is expected under `C:/src/chromium/src` (`CR_ROOT` changes root), depot_tools `C:/src/depot_tools`. No Chromium checkout was required for this survey.

| Command / step | Actual implementation and caveat |
| --- | --- |
| `cr init` | Write `.gclient` with unmanaged src solution if absent, then sync pinned source/deps and apply. Full dependency/build workflow, not the partial research clone. |
| `cr pin [rev\|latest]` | Resolve commit/ref or upstream **main**, write PIN only. `latest` means trunk, not latest stable. |
| `cr apply` | `git checkout -q -f -B work main`; sorted patches via `git am -q --3way --keep-cr`. Destructive to work branch/uncommitted edits; apply itself lacks sync's cleanliness guard. |
| `cr export [branch]` | Delete existing `patches/*.patch`, `git format-patch --zero-commit --no-signature --no-stat -o ... main..<branch>`; default work. Allows explicit WIP branch export; numbering/filenames churn when history changes. |
| `cr sync` | Read PIN; if work exists, reject tracked uncommitted changes, export work; detach; `gclient sync --revision src@PIN --no-history --shallow -D --force --reset`; set main=PIN; recreate/apply work. Untracked files excluded by `--untracked-files=no`; WIP branch choice requires care. |
| `cr update` | Pin latest main then sync; conflict recovery: fix in src, `git am --continue`, export and commit infra patches/PIN. Not a tested stable-channel rebase pipeline. |
| `cr gen/build/run` | GN gen if needed, copy repo args only if output args absent, `autoninja -C <out> chrome` default; run chosen `chrome.exe`. Existing args/builds persist. |

Default `args.gn`: `is_debug=false`, `is_component_build=true`, `dcheck_always_on=true`, `symbol_level=1`, blink/v8 symbols0. It does not encode branding through Google flags; Chromium branding files supply PRTS.

`scripts/build-release.ps1`: hardcoded existing checkout/out/Release, refuses concurrent ninja/siso using that directory; noncomponent, nonofficial, PGOphase0, no dchecks, all symbols0; GN gen then incremental chrome build with bounded hidden worker/log/exit files. First build documented6–8h; PLAN records earlier initial component build~6.5h/16 threads. M1 notes measured incremental builds61.3s, diagnostic relink25.7s, final controller/registry38.8s; these are historical small edits, not a universal port/build estimate. Branding caused wider rebuilds.

`scripts/test.ps1` supersedes older ad-hoc `qualify-m5a.ps1`: sequential quick/MF/GPU/flyout/touch/full suites, per-process timeout, stdout/stderr/`.exit`, recorded journal entries; explicit environment reset prevents test cross-contamination. Preview known-issue handling is narrow, raw failures remain; disconnected-desktop override explicitly leaves physical foreground/cursor unverified. Do not automatically import this large acceptance matrix into every development edit: retain targeted checks for changed behavior.

Packaging tools worth reusing:

- `package-preview.ps1`: browser/runtime+server bundles, source/build metadata and hashes, node_modules omitted; package an already built noncomponent output.
- `verify-preview.ps1`: clean extraction, install dependencies, real full acceptance against extracted executable; receipts bind qualified archives/BUILD metadata/SHA manifests.
- `split-preview.ps1`, `join-preview.ps1`: bounded-size archive pieces, per-part/full hash verification and exact reassembly.
- `publish-preview.ps1`: requires qualification receipts/hashes, deadline per upload, resumes/skips matching assets, checks remote digests before publishing prerelease. Useful packaging discipline, separate from research scope.
- `verify-gpu.ps1`: actual active Chromium vendor/device/LUID/non-SwiftShader plus decoded WebGL/video and real encoder input stats; important acceptance artifact.
- `prts-autopilot.ps1`: stale-journal/process/rate-reset-driven agent-resume helper. **Drop for the new framework**: development orchestration coupled to this machine/project, not browser infrastructure.

Workflow quality assessment: tiny readable Git wrapper, reproducible PIN, external build tree, bounded gates and clean-extract receipts are strengths. Weaknesses are hardcoded paths, forceful apply/reset/export, WIP/default-branch assumptions, no demonstrated multi-milestone rebase, long append-only journals and mutable status/release notes. Add explicit source+patch manifest and safe clean checks in new scripts; choose stable intentionally; do not run `cr update` merely to obtain a recent stable.

## 8. Lessons and documented dead ends

| Lesson / dead end | Evidence and implication |
| --- | --- |
| Whole-page capture is insufficient | `chromium-hooks.md` marks M1–M4 page hooks superseded; RWH capture/input misses tabstrip, omnibox and separate desktop popup roots. Begin with v2/window sources. |
| “Headless” does not mean no HWND | Unified Windows headless has cloaked stand-ins; old headless_shell lacks Chrome UI; Ozone headless is not a Windows backend. Keep HWND/Aura/new-headless path. |
| Source-only investigation patches were not ready code | `phase2-m5-design.md` maps all four `chromium-output-investigation` patches/P1–P8. Null GPU widget, custom OutputPresenter/shared-image ring and external BeginFrames deferred; raw token export/named-pipe/PID authority/UI-thread writes rejected. |
| Reparenting all popups into one root is risky | Desktop popup factories intentionally permit overhang/effects; renderer select menus use another creation path. Partial conversion changes focus/capture/modal/clip behavior; private separate-root composition proved four families. |
| Early host creation callback is too early | Widget/focus/capture/compositor are not all ready at Env notification. `PrtsHostTracker` uses explicit ready/destroying observations and owner/context/transient relationships; creation order alone is not z order. |
| Static capture can deadlock without a queue growing | Failed/invalidated Viz readbacks failed to call `VideoCaptureOracle::CompleteCapture`; global daemon latest slot also overwrote another source. Fix oracle accounting, per-source coalescing and remembered credit-blocked refresh; unchanged M3 passed10/10 afterward. |
| HRESULT success macros can be misleading | WAIT_TIMEOUT/WAIT_ABANDONED from keyed acquire are nonnegative; require exactly S_OK. windows-rs GetData Result discards S_FALSE; inspect raw HRESULT. A published descriptor is not GPU-read completion. |
| Mapping succeeds before memory is safe to read | SEC_RESERVE/SEC_IMAGE could satisfy mapping but not readable declared layout. Validate committed read-only mapped coverage with VirtualQuery before slice construction. |
| Cold UI shaders dominated popup delay | `lessons-flyouts.md`: copy waited106ms behind Graphite/Dawn raster/compilation; readback~4ms. Ganesh-only/eager refresh were unreliable; disabling Views UI GPU raster fixed the diagnosed stall while page GPU stayed. |
| DSF/outer bounds/synthetic monitors are separate | Getter reports client bounds while setter forwards outer bounds: requested960x640 initially938x629. Correct measured insets, preserve logical root property; large48192px monitor offset exposed overflow; selected rows at fractional DSF need full-rectangle containment. |
| Odd-size hardware support needs real driver tests | GPU1202x802 failed later `ProcessInput` while CPU succeeded; matching4-aligned GPU/media dimensions fixed it. Odd aperture truncation required enclosing even aperture plus viewer crop, not scaling content. |
| Event receipt is not successful UI action | Views rejected releases lacking changed-button flags. Tests must select actual New tab/Reload/select/omnibox commands and verify decoded outcome/restoration, not count events. |
| Touch velocity and focus can be destroyed by bookkeeping | Dispatch-time timestamps suppressed flings; same-epoch title/load metadata was sending blur and cancelling contacts. Preserve source timing and only reset on actual control/epoch change. |
| MF configuration success is not output behavior | Wrong HRD units yielded2s buffer; tiny accepted/readback100ms HRD at150kbps broke restart decoding. Driver ignored live bitrate writes. Require matching immediate output, property readback and measured rate/decoding. |
| MFT lifetime leak was upstream activation infrastructure | `phase2-m4.md`: mfplat->combase device-catalog registry watcher retained3 handles/activation even with balanced shutdown/revocation. Reusing IMFActivate, extra COM shutdown/flush and manager detach failed. Discovered-CLSID CoCreateInstance avoids that path;20-cycle handle counts became flat. |
| Component pass is insufficient for Release | Noncomponent select close hit `DesktopWindowTreeHostWin::Close` UAF after hide destroyed animator. Existing Aura animations-disabled switch fixed it without changing upstream teardown; clean-extract qualification caught it. |
| Shutdown ACK is not completed process shutdown | CONTINUE_ON_SHUTDOWN allowed cancellation worker to outlive teardown progress. BLOCK_SHUTDOWN + owned asynchronous joins fixed hang; temporary daemon grace/exit-status workarounds removed,227ms worker fixture closure documented. |
| Test fixtures can manufacture browser failures | Accepted Winsock stream inherited nonblocking mode, returning10035 despite timeout; set accepted streams blocking. OOPIF script-ready is not painted; require actual pixels. Read-only WebUI `web_contents()` avoids probe-created empty contents. |
| Network experiments contaminate later experiments | After400ms delay test REMB could stay5kbps; subsequent loss observation saw1–5fps and repaired packets. Fresh instance and>=15fps witness isolate loss. Current shaper does not emulate input/RTT/jitter. |
| Concurrent qualification corrupts evidence | UI tests changed Notepad/taskbar counts; concurrent MFT activation muddied handle counts; loaded chrome DLLs caused linker sharing failures; stale logs falsely suggested build success. Serialize native tests/builds, check fresh `.exit`, avoid racing shared out dirs. |
| Isolation cannot be layered on by toggling job UI bits | `journal-i1.md`: each tested outer-job UI bit caused nested sandbox SBOX18 launch failures; AC later hit Mojo writable-file execute guard; LPAC startup unresolved. GPU success in one AC/private-desktop probe is not full isolation qualification. |

## Recommendation

**Reuse the proven architecture as the starting point, not the historical 36-commit series unchanged.** It already satisfies the hardest structural constraints: unified Windows headless with stand-in HWNDs, real Chrome UI via root capture, Aura input, GPU export without desktop capture, private daemon composition and practical software fallback.

Concrete reuse priorities:

1. Reuse the **resource protocol/lifetime implementation**, LUID checks, CPU recovery, per-source backpressure/static-refresh fixes, native three-pipe launcher/Job lifetime, private copies and teardown cancellation. Preserve the regression fixtures that caught real ownership/desktop/decoder bugs.
2. Adapt **root capture + host readiness/owner/virtual-order tracking**, CPU/GPU premultiplied scene composition and touch source-clock/cancellation logic. Keep all desktop cursor/drag/external-UI suppression explicitly opt-in. Preserve generic boundary direction: Views/Aura hooks must not depend on chrome/browser.
3. Reuse the daemon's `FrameSource`, encoder selection/fallback seam, MF fallback (including CLSID activation and HRD fixes), software converter, viewer signalling and packet-shaping fixture. Review the parked **oneVPL bridge** on its own branch and qualify it as the new primary encoder; do not restart Intel ABI/runtime investigation from zero.
4. Reuse the **two small UA behavior hooks**, standard BRANDING mechanism and page/worker identity tests. Fold attribution restoration/Settings fix into one scoped branding change; keep product PRTS and Chrome web identity independent.
5. Adapt PIN/export/build and GPU/clean-extract packaging scripts. Replace hardcoded paths and forceful apply defaults, distinguish stable from trunk, record exact upstream/source/patch/build identities, and document one current status page rather than chronological plans with contradictory headings.

Redo/simplify deliberately:

- Start **window/tab/source/scene identities once**. Drop v1 page-only capture/input and the temporary window-to-target projection; design one encoder/track per selected browser window from the outset if independent windows are required. Existing single-instance worker cannot provide simultaneous window streams by a capability flag alone.
- Split the 1,337-line final window registry, 621-line input adapter and 695-line capture adapter into lifecycle/window geometry, source capture, scene policy and protocol dispatch responsibilities. Move diagnostic Chrome internals/test geometry RPCs out of production. Avoid exposing new generic public content APIs just to enforce test ordering.
- Revisit serialized renderer/visual-state barriers and synchronous UI-side GPU query polling with traces. Keep ownership safety; optimize by asynchronous completion/fences/pooling only after equivalent release/generation failure behavior is proven. Latest-frame dropping, not deeper queues, should absorb encoder/network slowness.
- Add **editable TextInputClient state observation -> typed viewer notification -> soft keyboard policy** early. Include Views omnibox and content/OOPIF editors; preserve local IME commit behavior. Full preedit/caret/selection mirroring is separate functionality.
- Build instrumentation around source capture, input receipt, encoder submit/output, RTP send and actual displayed-frame witnesses. Measure 1080p at 10–16 Mbps and 50 ms RTT plus jitter, reporting P80/P99; current ingress/encoder/getStats counters cannot certify it. Measure popup cold-start and same-epoch input tail separately.

Suggested logical series: **(1)** headless lifecycle+transport+registry, **(2)** opt-in desktop/input boundary including touch, **(3)** root/source capture+resources+CPU/GPU recovery, **(4)** popup ownership/scene export, **(5)** root geometry/DSF integration, **(6)** product/web identity; keep any still-required generic Viz/display fixes separate with upstream-style regression tests. Collapse 0001–0005; 0006/0007/0016/0033; 0010–0012/0014/0015/0017–0019/0027; 0023/0035/0036; 0024–0026; 0029–0032. Split 0028 behavior from diagnostics, fold 0034 into opt-in lifecycle policy. The goal is fewer coherent maintenance units and a smaller final diff, not one giant patch hiding coupled changes.

Candid quality assessment: this is capable prototype/preview engineering with unusually concrete failure reproduction, bounded lifetimes and decoded-pixel acceptance. It is also a rapidly accumulated migration: duplicate protocol generations, magic numeric message IDs, global Views observer callbacks, large registries, test-specific Chrome dependencies and sprawling mutable journals. Keep the demonstrated behavior and careful resource checks; spend the rewrite budget on a stable Aura/Views adapter boundary and current-window model, not reproducing every historical acceptance mechanism.

## Open questions / spikes to verify

- Apply/rederive the final behavior against the **chosen recent stable**, especially Viz CompleteCapture, root size, synthetic-screen scaling and normal Aura input/IME APIs. This survey read stable headless HWND behavior, not every stable hook.
- Trace each GPU stage and UI-thread wait: can export completion become asynchronous, can private allocations be pooled, and which copy can be removed without coupling Viz leases to encoding? Retain the current safe path as a comparison.
- Benchmark/qualify parked oneVPL vs corrected MF on UHD 630 at 1080p, 10/16 Mbps, 30/60 fps, scrolling/video/games, IDR/bitrate changes, device loss and long handle cycles. Verify current runtime licensing/deployment and actual low-delay readback/output behavior.
- Implement editable-focus subscription and client keyboard/IME policy; verify focus across omnibox, OOPIF, popup, navigation, touch and control handoff without server OS foreground input.
- Qualify bubbles/tooltips/permission/profile/extension/modal dialogs and popup overhang/clipping, eight-popup scene limit, native-only surface policy and mixed CPU/GPU fallback. Do not extrapolate four popup-family passes to all Chrome UI.
- Decide independent per-window worker/track negotiation and input ownership; remove compatibility projection if the new API can start with native windows/tabs.
- Test actual Chrome 89+ receivers, including Baseline SPS level/1080p negotiation, optional jitter hints absent, odd-size crop and touch APIs. Existing modern Edge qualification does not establish that floor.
- Run symmetric 50 ms RTT with specified jitter/bandwidth/loss and input traffic included; define capture-to-display/input-to-photon witnesses and report P80/P99 with cold/warm UI cases. Current shaper and encoder means do not answer this.
- Repeat physical foreground/cursor checks in a connected Windows session and target Windows 10 APIs/runtime. Document hardware acceleration separately from software fallback and desktop invisibility separately from tenant isolation.
- Keep AppContainer/LPAC/private window station work deferred per the new brief; when resumed, use parked failure evidence and verify nested Chromium sandbox, GPU, executable mappings, network/loopback and cross-tenant UI behavior.

