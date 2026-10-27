# Decisions log

Each entry: ID, date, decision, why, evidence. Status is **valid**, **amended** (still valid, changed in detail) or **superseded**.
All Chromium paths are relative to `src/`. Baseline: Chromium Stable **157.0.8103.15** / `1aa07b638799e59656559ae6c6649d4b197f555e`
("the pin"). Decisions rest on Chromium source and the [brief](brief.md) only; the user's earlier project was prior art, never a reason.
Component docs: [browser](browser.md), [re](re.md), [protocol](protocol.md), [video-pipeline](video-pipeline.md).

## Architecture (clean-room review, 2026-10-25)

### D1 Baseline pinned to Windows Stable 157.0.8103.15 (valid)
Web identity must match a really released Chrome (UA major, UA-CH GREASE are derived from the major version); a stable branch keeps
patches from drifting; libwebrtc at the same pin matches Chrome 157's own sender. Rejected: tracking main. Upgrade policy: optional rebase
per new Stable milestone. Evidence: [r-chromium-map](research/r-chromium-map.md).

### D2 Run form: Chrome `--headless=new` + Aura/Views + the cloaked HWND kept (valid)
`HWNDMessageHandlerHeadless` keeps a real, cloaked HWND; activation and capture are in-process logical state; `--headless` disables native
occlusion; cloaking does not affect Present or the BeginFrame source. The brief requires this path. All behaviour is guarded by the
runtime switch `--rb-remote`; without it the patched chrome.exe equals upstream. Headless `Activate()` does not deactivate other windows
of the process (see D8). Rejected: custom `ui::PlatformWindow` / headless-ozone shim on Windows (`DesktopWindowTreeHostPlatform` is
linux/fuchsia only; large patch, leaves the new-headless path), Ozone headless (unavailable), headless_shell (no browser UI), hidden
desktop + desktop duplication + SendInput (multi-tenant interference).

### D3 Browser <-> endpoint IPC: Mojo invitation (valid; confirmed D19)
The endpoint is in-tree C++ (D12), so both ends share one `.mojom` (`chrome/browser/rb/mojom/rb.mojom`), handle passing (D3D11 NT handle
as platform handle) and version. The endpoint creates a `mojo::PlatformChannel`, inherits one end into chrome.exe (`--rb-mojo=<handle>`,
`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`); the browser (broker) sends the `OutgoingInvitation`. The browser creates the capturer and hands the
frame sink to the endpoint, so frames leave the GPU process without the browser UI thread. Fallback (never needed): three inherited
anonymous pipes with a versioned schema. Rejected: DevTools/CDP (cannot reach browser UI, automation fingerprint), named pipes (ACL and
squatting). Evidence: [s10-link](research/m1/s10-link.md), [protocol](protocol.md#browser-to-re-mojo).

### D4 Frame export: one `FrameSinkVideoCapturer` per browser window root (valid)
`VideoCaptureTarget(compositor->frame_sink_id())` captures the root render pass (tab strip, omnibox, page, OOPIF, in-root popups) in the
same `DrawAndSwap` that found damage; on Windows it yields D3D11 shared textures. The mappable blit does not block the GPU main thread; it
only defers delivery until the GPU is done. Software fallback: `kDefault` shared-memory ARGB. A viz fix is carried (patch 04,
`CompleteCapture` on the failed/version-changed path). Rejected/backup: custom `SkiaOutputDevice` writing into the shared texture (10-16
viz files; only if capture cost proved unacceptable), per-frame `RequestCopyOfOutput`, per-WebContents capture plus separate UI path,
DComp/swap-chain interception, DWM/desktop capture. Evidence: [b2-capture](research/b2-capture.md), [b1-frame-path](research/b1-frame-path.md).

### D4b Popups stay inside the browser window root in remote mode (valid)
By default Windows menus, bubbles, tooltips and `<select>` popups become separate top-level `DesktopWindowTreeHostWin`s. In remote mode
`ChromeViewsDelegate::CreateNativeWidget` picks `kNativeWidgetAura` for non-top-level windows with a parent and `GetDefaultParent`
returns `root_window_` (patch 11, ~10 lines). One window = one root = one capturer = one input sink. Popups cannot leave the window,
which is the whole viewport anyway. Fallback (unused): capturer per popup root plus endpoint compositing.

### D5 GPU hand-off: use the Viz pool textures directly, zero extra copy (amended by D14)
The endpoint opens each pool buffer once (`OpenSharedResource1`, cached), syncs, runs `VideoProcessorBlt` BGRA->NV12 straight into the
encoder input surface, then returns the frame (`Done()`). Sync was keyed mutex 0; the default is now the shared D3D11 fence (D14).

### D6 Frame pacing: P0 vsync + capture throttle; external BeginFrame rejected (amended by D15)
P0: Viz follows `VSyncThreadWin`, capturer `SetMinCapturePeriod(1/fps)`, auto-throttling and animation lock-in off, endpoint overwrites
with the latest frame. P1 (external BeginFrame, patch 10) was built and rejected in M4 (D15); the retained pacing helper is the precise
60 Hz fake vsync (patch 17). Rejected from the start: `--fake-vsync-rate` as pacing (process-global, startup-fixed) alone,
`--disable-frame-rate-limit`, `SetDisplayVSyncParameters` (no effect on DComp roots).

### D7 Input: `aura::EventInjector::Inject` of `ui::Event` into the target `WindowTreeHost` (valid)
The sink reaches Views (tab strip, omnibox, menus) and content (`RenderWidgetHostInputEventRouter`, real `WebTouchEvent`). Required remote
mode patches (06): no `::GetKeyState`, logical capture in headless, cursor/keyboard-hook/`DoDragDrop`/touch-drag Win32 calls become
reports or no-ops. Rejected: `SendInput` (global foreground/focus, multi-tenant), `PostMessage` to the cloaked HWND, `RenderWidgetHost::Forward*`
or DevTools Input (cannot reach browser UI), split routing. Evidence: [c1-input-layer](research/c1-input-layer.md).

### D8 Focus and activation: logical per instance, one active root (valid)
No `SetForegroundWindow`. Because headless does not deactivate other roots, the controller deactivates the others explicitly; when the
viewer loses control all held keys/buttons are released, touches cancelled and the root deactivated.

### D9 Editable-focus detection and remote soft keyboard (valid)
Remote mode returns `ui::RemoteInputMethod` (derived from `MockInputMethod`, adds detach/empty-focus notifications and a forwarding
virtual-keyboard controller). Per-root observers emit `IME_STATE` (see [protocol](protocol.md)). Text goes back via `InsertText` /
`SetCompositionText`. Evidence: [c2-ime-osk](research/c2-ime-osk.md).

### D10 Touch capability exposure through WebPreferences (valid)
`OverrideWebPreferences` in a `ChromeContentBrowserClientParts` sets `maxTouchPoints`, touch feature detection, pointer/hover types per
instance (patch 09). Rejected: `pointer_device_win.cc` (process-global, affects Views), `--touch-events`.

### D11 Branding: product PRTS, web identity Google Chrome (valid)
Two guarded edits in `user_agent_utils.cc` (brand list, no `Headless` prefix; patch 13); everything else flows through the upstream UA/UA-CH
pipeline. Build args `proprietary_codecs=true`, `ffmpeg_branding="Chrome"`. Rejected: `--user-agent` (UA-CH early return),
`is_chrome_branded=true`, faking JS and HTTP separately. Evidence: [d-branding](research/d-branding.md). Generation of UI branding: D16.

### D12 Endpoint: in-tree C++ + libwebrtc (pinned) + oneVPL (valid; reaffirmed D17, D18)
libwebrtc is the counterpart of the Chrome 89 receiver (GCC/TWCC, pacer, NACK/RTX, FEC, playout-delay, frame dropper), the lowest P99
risk; oneVPL is a C API and D3D11 is native C++; in-tree reuses the checkout and toolchain; upstream `remoting/` is the same pattern
(native host + libwebrtc + HW H.264 + `VideoPlayoutDelay::Minimal()`). Rejected: Rust + webrtc-rs (unverified GCC/pacer maturity, still
needs a C++ bridge), standalone libwebrtc checkout (second gclient, ABI drift), endpoint inside the browser process (bigger patch, encoder
and network in the process that will be sandboxed). Table of options: [video-pipeline](video-pipeline.md#endpoint-options).

### D13 control-plane and viewer (valid)
Node sample (instance lifecycle, signalling relay, debug pages); the viewer must run on Chrome 89 (feature-detect newer APIs).

## Tuning outcomes (M3/M4, 2026-10-26/27)

### D14 Tuned defaults (valid)
| Setting | Default | Evidence |
|---|---|---|
| Capture hand-off | shared D3D11 fence (patch 19, `--capture-sync=fence`; keyed mutex fallback) | Viz capture segment P50 14.8 -> 1.3 ms ([fence-impl](research/m4/fence-impl.md), [shared-fence](research/m4/shared-fence.md)) |
| Bitrate | min/start/max = 2000/8000/8000 kbps | max = link rate fills the bottleneck queue; 8 Mbps removed c12 loss, S capture P99 123 -> 88 ms ([experiments](research/m4/experiments.md)) |
| Frame rate | 60 fps | 30 fps made G ~28 ms slower; 60 vs 30 in [levers-measurements](research/m3/levers-measurements.md) |
| Encoder | oneVPL VBR + LowDelayBRC, LowPower on, TU7, AsyncDepth 1, CB profile, 1 slice, frame cap 1.0x, VBV 1 frame requested (driver rounds ~34 KB), no periodic IDR | [s7-onevpl-probe](research/s7-onevpl-probe.md), [t1-encoder-tuning](research/t1-encoder-tuning.md), [vpl-harden](research/m3/vpl-harden.md) |
| Receiver | RTP playout-delay (0,0) sent by the endpoint | Chrome 89 treats `playoutDelayHint` as a floor: [s8-chrome89](research/s8-chrome89.md), [jitter](research/m3/jitter.md) |
| Pacing | `WebRTC-Video-Pacing/factor:2.5`, precise 60 Hz fake vsync (patch 17), 1 ms hires timer | M3 combined vs baseline loopback capture->display P50 ~44 vs 64-84 ms ([levers-measurements](research/m3/levers-measurements.md)); S d25j5-c12 capture P80/P99 158.7/181.4 -> 108.6/121.3 ([matrix2-before-after](research/m3/matrix2-before-after.md)) |
| GPU restart | capturer recovery (patch 18) | [gpu-recover](research/m3/gpu-recover.md) |
| FEC | off | E13: no gain; ULPFEC also disables NACK ([experiments](research/m4/experiments.md)) |

Acceptance state (end-to-end target P80 < 100 / P99 < 200 ms, ending at decoded frame): capture->decoded passes under d25j5 with genuine
Chrome 89 (S P80/P99 73-82 / 101-119 ms); input->decoded (which carries the uplink too) fails at 146-160 ms P80 ([chrome89](research/m4/chrome89.md)).
Loopback marker: capture->decoded ~17-35 ms, input->decoded ~37-60 ms.

### D15 Rejected levers (valid)
- 30 fps (slower), CBR + LowDelayBRC (driver reads back OFF), RGB4 direct input (no gain, P99 28.9 ms), VBV below the driver minimum (~2 frames
  honoured in S7, 1 requested and rounded in M4), intra refresh (no latency gain on c10, recovery unverified; opt-in `--vpl-intra-refresh`), FEC.
- S5 external BeginFrame (fixed/demand; patch 10 and pacer): marker-page phase stabilised to 1.4 ms but real content sinks lagged 1-2 frames
  after scrolling/GPU recovery; removed. Evidence: [s5-pacing](research/m4/s5-pacing.md), [capture-phase](research/m4/capture-phase.md), diffs in `research/m4/s5-rejected/`.
- Viewer presenter modes (kiosk, clean, desynchronized canvas): no measurable gain, viewer defaults unchanged ([presenter](research/m4/presenter.md)).

## Stage 6 (2026-10-27)

### D16 Codename rb/re; PRTS branding by generation (valid)
Code, switches and the checkout branch use **rb** (browser) and **re** (endpoint); **PRTS** stays the product brand. UI strings and
translations are generated by `browser/branding/apply_branding.py` (allowlisted message bodies, config in `branding/config.json`), not
carried as patches; patch 12 holds only the `BRANDING` fields and patch 13 keeps the web identity Google Chrome. Old notes still say
prts/prts_endpoint.

### D17 Rust endpoint attempted, then rolled back (user decision)
A Rust `re` was built (`re-encode`, `re-mojo`, `re-rtc` over a C++ `re_native` shim, patch 20 GN hook). Rolled back: no clear advantage and the
shim still required the Chromium build tree. Last A/B (scenario S, d25j5-c12, 3 runs each, median P50/P80/P99 ms): capture->decoded legacy
70.8/75.0/82.1 vs Rust 60.1/67.5/81.0; input->decoded legacy 114.2/121.9/142.9 vs Rust 121.2/131.0/148.4, i.e. within run-to-run noise
(15.9 ms at P50 between identical baseline runs, [str0m-partial](research/m6/str0m-partial.md)). The C++ endpoint (`re/`, GN target `re`) is the endpoint;
its encoder layer reuses the NVENC and parameter-set ideas. Archive: `archive/rust-re/`.

### D18 WebRTC stays libwebrtc (user decision)
No pure-Rust stack. A partial str0m 0.24.1 evaluation (native playout-delay and TWCC, Chrome 89 negotiates, pacer defaults 1.1 vs the
tuned 2.5, no valid high-rate or timing A/B because the host was saturated) showed it viable but unproven; it was stopped when the user kept
libwebrtc. Evidence: [str0m-partial](research/m6/str0m-partial.md).

### D19 Mojo stays for browser <-> re (user decision)
Mojo is kept (shared `.mojom`, handle passing, same pin). The Rust variant needed Mojo bindings; instead a C++ `re_native` shim bridged
Mojo, which the rollback removed. Not pursued: Rust Mojo bindings.

### D20 NVENC backend added, untested on hardware
Encoders sit behind a shared `EncoderParams` + `VideoEncoderBackend` interface: oneVPL (default on Intel, tuned), OpenH264 (fallback), NVENC
(new). `--encoder=auto|vpl|nvenc|openh264`; NVENC loads `nvEncodeAPI64.dll` at runtime and falls back to OpenH264 when absent. No NVIDIA GPU
was available: only compile and the no-GPU fallback were run ([re](re.md#encoders)).

### D21 Archive policy
Workflow prompts, stage plans, status/ops notes, reviews, the Rust endpoint and superseded or working research notes live under
`archive/` (see `archive/README.md`). `docs/` holds the current design and the result notes decisions link to; `docs/research/` keeps
evidence notes only.

### D22 Widevine bundled (valid; licensing deferred)
The CDM is fetched from the component updater source and placed under `third_party/widevine/cdm/win/x64/`; GN `enable_widevine` etc.
([browser](browser.md#widevine)). Limitation: no Google VMP signature, so high-security licences may be refused (L3).

### D23 Isolation target (target; experiment outcome in D24)
User target (2026-10-26): Job Object + `JOB_OBJECT_UILIMIT_*` + LPAC managed by the endpoint, fallback Job + AppContainer if more than ~1k
Chromium LOC are needed ([isolation](isolation.md)).

### D24 Opt-in LPAC + outer UI limits + private desktop (2026-10-28; supersedes Stage 6 block)
Stage 8 resolved SBOX44 by disabling nested network AppContainer sandboxing for an already-contained browser, with five minimal LPAC
capabilities. Chromium retains restricted child tokens and its jobs/process/memory limits; clearing child job UI masks lets the outer
Job own all eight UI restrictions. Broker-provisioned private startup desktops close the observed FindWindow/hook gap. Profile DACLs
are protected from shared package inheritance. LPAC/all and plain AC/all pass Intel D3D11 capture, input/IME and MediaKeys readiness;
controlled cross-tenant file/desktop/window/hook/clipboard checks and browser/GPU recovery pass. Keep the default off as instructed;
production ACL/path/broker review, unexercised nested-container utilities, packaged builds and full DRM playback remain open.
[evidence, enabling and limitations](isolation.md). Patch20 remains a small maintenance unit; no core IPC/job tracking redesign.
