# Browser (rb): patch series and maintenance

rb is Chromium Stable **157.0.8103.15** (`1aa07b638799e59656559ae6c6649d4b197f555e`, `browser/PIN.json`) with a small patch series, run as
`chrome.exe --headless=new --rb-remote`. Day-to-day commands: [browser/README.md](../browser/README.md). Rationale: [decisions](decisions.md) D1-D11.

## Principles
1. **New code in new files**: rb logic lives in `chrome/browser/rb/`; the part that `ui/` cannot take from `chrome/` is
   `ui/base/ime/remote_input_method.*`.
2. **Upstream files get hooks only**: each at most ~30 lines, marked `// RB:`, guarded by the runtime switch `ui::IsRbRemoteMode()`
   (`--rb-remote`). Without the switch behaviour equals upstream. Views/Aura never depend on `chrome/`; content gets no new public API.
3. Prefer upstream extension points (embedder interfaces, `ChromeContentBrowserClientParts`, `aura::EventInjector`,
   `HostFrameSinkManager`, `--screen-info`) over hooks.
4. Upstreamable fixes are separate patches.

## Series (`browser/series`, application order)
Patch numbers are stable names; consecutive units were folded or dropped over time (03, 05, 07 live in 02; 10 was rejected).

| # | Name | Upstream touch | Purpose |
|---|---|---|---|
| 01 | `rb-switch` | `ui/base/ui_base_switches.*` | `--rb-remote`, `IsRbRemoteMode()` shared guard |
| 04 | `viz-capturer-complete-capture` | `frame_sink_video_capturer_impl.cc` + unittest | `CompleteCapture` on the failed/version-changed delivery path (upstreamable) |
| 11 | `remote-popups-in-root` | `chrome_views_delegate_win.cc`, `desktop_native_widget_aura.cc` | popups and parented modal dialogs (e.g. beforeunload) stay inside the window root (D4b) |
| 12 | `rb-product-branding` | `chrome/app/theme/chromium/BRANDING` | product name PRTS (UI resources are generated, see below) |
| 13 | `chrome-web-identity` | `user_agent_utils.cc` + tests | brand "Google Chrome", no `Headless` prefix in remote mode |
| 06 | `aura-views-remote-input` | `input_state_lookup_win.cc`, `hwnd_message_handler_headless.*`, `desktop_window_tree_host_win.cc`, `desktop_drag_drop_client_win.cc`, `screen_win_headless.*` + `remote_cursor_sink.h` | no `::GetKeyState`, logical capture, cursor/keyboard-hook/drag Win32 calls reported or no-op |
| 08 | `remote-ime` | `input_method_factory.cc`, `ui/base/ime/BUILD.gn` + `remote_input_method.*` | `RemoteInputMethod` (D9) |
| 15 | `remote-window` | 4 files | windows maximized and frameless; runtime screen geometry setter |
| 09 | `rb-web-prefs` | `chrome_content_browser_client.cc` (3 lines) | touch capabilities via `OverrideWebPreferences` (D10) |
| 02 | `rb-browser` | headless extra parts, `chrome/browser/BUILD.gn` + `chrome/browser/rb/*` (24 files) | controller, Mojo link, window registry, capture, input (`aura::EventInjector`), touch pool, IME bridge |
| 14 | `rb-endpoint-target` | `//BUILD.gn` (1 line) | `gn_all` builds `//re` |
| 17 | `remote-vsync` | 5 files | precise 60 Hz fake-vsync timer (headless has no DXGI output) |
| 18 | `rb-capture-recovery` | rb only | recreate captures after a GPU/viz restart, ask `re` to drop texture caches and send an IDR |
| 19 | `rb-shared-fence` | 22 files | shared D3D11 fence hand-off: viz signals at submit, `re` waits on the GPU (D14) |

Upstream footprint is on the order of 15 files and a few hundred lines outside tests and strings; the new rb code is mostly in
`chrome/browser/rb/`. `cr stat` warns above 30 lines per hook or 400 lines total upstream.

### Details worth keeping
- **Startup and link**: `re` launches chrome.exe, inheriting one `mojo::PlatformChannel` end (`--rb-mojo=<handle>`, exact handle list); the
  browser clears the inherit flag early and attaches the controller to `ChromeBrowserMainExtraPartsHeadless`. All Chrome object access is
  on the UI thread. Contract: [protocol](protocol.md#browser-to-re-mojo).
- **Capture**: per window root `HostFrameSinkManager::CreateVideoCapturer`, `ChangeTarget(VideoCaptureTarget(frame_sink_id))`, hold
  `WindowTreeHost::CreateVideoCaptureLock()`; `SetFormat(ARGB)`, `SetMinCapturePeriod(1/fps)`, auto-throttling and animation lock-in off,
  fixed root size, `kPreferMappableSharedImage` (`kDefault` without GPU). Startup checks: GPU LUID equals the endpoint's, not
  SwiftShader/WARP; a source falls back to CPU shared memory if no GPU frame arrives in time.
- **Input**: the UI thread builds `ui::MouseEvent` / `ui::TouchEvent` (new event per changed contact) / `ui::KeyEvent` (DOM code/key/VKEY,
  `EF_IS_SYNTHESIZED` so IME is skipped) and calls `aura::EventInjector().Inject(host, &event)`. Viewer timestamps are mapped to
  `TimeTicks` preserving intervals (fling velocity). Patch 06 returns `Env::IsMouseButtonDown()` to event flags.
- **Soft keyboard**: `IME_STATE` is emitted from `RemoteInputMethod` focus/type/caret changes and its virtual-keyboard controller
  (content's `VirtualKeyboardControllerWin` already decides show/hide); caret via `GetCaretBounds()` converted to the root.
- **Launch switches** (no patch needed): `--headless=new --rb-remote --screen-info={WxH devicePixelRatio=...} --user-data-dir=<per instance>
  --no-first-run --no-default-browser-check --disable-legacy-window` (no `LegacyRenderWidgetHostHWND`).

## Branding generation
UA, client hints and `userAgentData` flow through the upstream pipeline: expected UA is
`Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/157.0.0.0 Safari/537.36`, `Sec-CH-UA` contains
`"Google Chrome";v="157"` and `"Chromium";v="157"`. User-visible UI strings are **not** patched: `browser/branding/apply_branding.py` rewrites
allowlisted GRIT message bodies and translations from `branding/config.json`, keeping attributes, placeholders, URLs and identifiers; IDs
follow the checkout's fingerprint algorithm. The result stays uncommitted in the Chromium tree; `cr export` rejects commits that include
these resources, `cr apply` restores them before its clean check and regenerates them after replay (`cr brand` is repeatable). Restore
before editing the resources or rebasing. Evidence: [d-branding](research/d-branding.md).

## Widevine
Bundled at the user's request (licensing deferred). Source: the component updater endpoint (`update.googleapis.com/service/update2/json`,
component `oimompecagnajdejgnnjijobebaeigek`, os=win, arch=x64, prodversion=157); download the `.crx3`, verify SHA-256, strip the CRX3 header
(zip). Version 4.10.3112.0 matches the pin (module 4 / interface 10 / host 10-11). Copy `manifest.json`, `widevinecdm.dll`,
`widevinecdm.dll.sig` into `src/third_party/widevine/cdm/win/x64/` (not in a patch, not in the repo; repeat on a new checkout). GN:
`enable_widevine`, `enable_library_cdms`, `bundle_widevine_cdm = true`, `enable_cdm_host_verification = false`; the CDM then lands in
`out\<cfg>\WidevineCdm\_platform_specific\win_x64\` and ships with chrome.exe. Verify with
`navigator.requestMediaKeySystemAccess('com.widevine.alpha', ...)` and `chrome://media-internals`. Limitation: no Google VMP signature, so
some licence servers refuse (L3 software CDM). Evidence: [widevine-steps](research/m1/widevine-steps.md).

## Build configurations (`browser/args/`)
| | dev | rel |
|---|---|---|
| use | iteration, component build, dcheck | measurement and packaging (latency is measured on rel) |
| flags | `is_component_build = true`, `dcheck_always_on = true`, `symbol_level = 1` | non-component, no dcheck, no symbols, `is_official_build = false` |
| both | `is_chrome_branded = false`, `use_siso = true`, `proprietary_codecs = true`, `ffmpeg_branding = "Chrome"`, `rtc_use_h264 = true`, Widevine bundle flags | |

Targets: `chrome` and `re` (`cr build dev chrome`, `cr build dev re`). Checkout `C:\src\chromium\src` (not in the repo); `src\re` is a
directory symlink to `remotekit\re`, not part of any patch; `cr apply` makes sure it exists.

## Patch maintenance (`browser/cr.ps1`, `cr.cmd`)
- Layout: `browser/{PIN.json, series, patches/*.patch, args/{dev,rel}.gn, branding/}`. In the checkout: branch `upstream` (= PIN) and `rb`
  (= upstream + series); backup branches are created before destructive steps.
- Commands: `init` (unmanaged `.gclient`, shallow sync by tag), `apply` (restore generated resources, refuse other dirty changes,
  `git am --3way` per series, then `brand`), `export` (stable file names from the `Patch-Name:` trailer, only changed files written),
  `check` (trial apply in a temporary worktree), `stat` (per-patch upstream files/lines, thresholds above), `rebase <tag>` (Stable
  milestone upgrades only), `gen` / `build` / `run` (build lock, logs with `.exit` files).
- Rules: one maintenance unit per commit with a unique `Patch-Name: NN-rb-description` trailer (`--fixup` + autosquash); remotekit commits only
  series/patches/PIN/args; patch headers state purpose, upstream files, guard switch, design reference and upstreamability; a milestone upgrade
  is `cr check` + build + smoke.

## Considered and not adopted
Synthetic input APIs in `content/public` or RWH injection (cannot reach browser UI), serialising input for tests (head-of-line delay; the
viewer observes via rVFC instead), a virtual touch screen in `pointer_device_win.cc` (replaced by patch 09), a custom viz `OutputDevice`
(backup of D4), porting `DesktopWindowTreeHostPlatform` to Windows (D2), `--user-agent` / `is_chrome_branded=true` (D11).
