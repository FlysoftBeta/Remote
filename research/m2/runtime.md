# m2-runtime
Started: 2026-10-26. Scope: existing Chromium build; no Chromium edits/builds or commits.

| Check | Result | Evidence |
|---|---|---|
| Network Service | Choose inheritable RX ACLs for both package SIDs; sandbox retained. 3/3 adjusted runs: no service crash, no navigation retry, first page loaded. Root cause not conclusively established. | acl-run{1,2,3}-chrome.log; acl-run{2,3}-state.json; direct-stderr.txt:5 before change |
| Capture to display, 60s tab mode | P50 **85.7**, P95 **101.7**, P99 **114.3 ms**, **n=1431** unique marker frames | latency-2026-10-26T02-12-11-223Z.{json,csv}; acl-run3-latency60.txt |
| Input to decoded colour flip, same 60s run | P50 **132.9**, P95 **147.7**, P99 **155.7 ms**, **n=120**; 120 attempts, zero timeouts; minimum send spacing **458.6 ms** | Same JSON summary.inputSamples; results-summary.json |
| Widevine | **PASS**: requested H.264/cenc EME configuration and createMediaKeys; Angel One license HTTP 200 / 1099 bytes; playback advances, visible through WebRTC | widevine-result.json, widevine-network.json, widevine-viewer{,-later}.png, widevine-loaded-modules.txt |

## Work / files changed
- Created task notes before investigation. Task completed approximately 10:17 SGT, 2026-10-26.
- Repo files changed by this task: `scripts/grant-lpac-acl.ps1` (new), `tools/latency/remote-page.html`, `tools/latency/measure.js`, `tools/latency/run.mjs`. No endpoint C++ changes, Chromium source changes, Chrome builds, or git commits.
- Host changes: inheritable RX ACEs for S-1-15-2-1 / S-1-15-2-2 on `C:/src/chromium/src/out/dev` and `C:/src/tmp/m1`. Original and final SDDL in acl-before.txt / acl-final.txt. Script rerun successfully (acl-grant.txt / acl-reapply.txt), preserves other principals and adds no writes.
- Deployment: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/grant-lpac-acl.ps1 -BuildDirectory C:\path\to\deployed\build -ProfileParent C:\path\to\dedicated\profiles`. Apply after deploying component binaries. Use elevation if directory ownership requires it. No sandbox-disabling flags are needed in the measured setup; H2 was not used.

## Evidence and API checks
[V] = inspected at pinned checkout; [I] = inference.

## Open risks
- ACL causality remains unproven: a warm rollback with newly created profile also passed. A truly cold machine reproduction was not done. Initial two pre-change chain attempts timed out before Chrome logging/Hello, whereas a direct pre-change Chromium run showed the Network Service restart. Do not claim the ACLs explain all startup faults.
- Latency is one local-loopback headless Edge 156 run, OpenH264, dev/component build, 1898x1068 decoded video, DPR 1; real colour input sampling was enabled concurrently. Marker draw timestamp to rVFC expectedDisplayTime is labelled capture-to-display by the existing harness. Input measurement is viewer key-send time to decoded-frame colour detection, not a physical monitor photon measurement. Numbers need not match prior M1 runs.
- Widevine tested only the public software-secure demo at 192x144 H.264 + AAC. Commercial services, VMP enforcement, HDCP, high resolution and persistent licenses remain unverified.
## Initial inspection (10:00-10:05 SGT)
- out/dev SDDL already has inherited RX for S-1-15-2-2 on chrome.dll/base.dll, but no S-1-15-2-1. m1 profile parent has neither SID.
- First test.sh fresh-profile baseline did not reach Browser Hello within 30 seconds; no chrome_debug.log was created. This is a separate startup observation, not evidence of a Network Service crash. Retrying existing build before ACL change.
## ACL result and latency harness update (10:08 SGT)
- ACL script applied both package SIDs with inheritable RX to out/dev and C:/src/tmp/m1. No sandbox-disabling switch added.
- First successful fresh-profile chain after ACL: acl-run1-chrome.log; zero Network service crashed / bootstrap / retrying lines. Marker row 86, one scan; 238/238 marker frames decoded.
- Before change, direct-stderr.txt line 5 records Network Service restart. Initial two chain attempts were cold-start/Hello timeouts; baseline2 reached Hello and only frame 1. Capturing an explicit same-chain ACL baseline next, restoring only newly added ACEs temporarily.
- Incremental endpoint-only build succeeded; executable unchanged (no rebuild work needed). Chromium not built.
- Edited tools/latency/{remote-page.html,measure.js,run.mjs}: real keydown/up through RKInput.sendKey/DataChannel every 500ms, large green/magenta block, decoded-frame one-pixel colour sampling. Keeps one outstanding key and records attempts/timeouts/sample timestamps; P95 output added. Canvas CSS sizing fixes DPR presentation.
## Network Service three-run check (10:12 SGT)
- acl-run1/2/3 logs contain zero Network Service crash/bootstrap/retry lines; first navigation produced latency frames each run.
- acl-run2-state.json and acl-run3-state.json: NetworkSandboxState = 1 only (5 samples), no failed-launch histogram. Verified enum [V] chrome/browser/net/system_network_context_manager.cc:124-142: 1=enabled-by-platform; 4=disabled-after-failed-launch.
- Network utility command line remains --service-sandbox-type=network. No NetworkServiceSandbox / NetworkServiceInProcess2 override.
- Important causal limit: removing new ACEs for a subsequent warm fresh-profile baseline did NOT reproduce the crash (baseline-restored-chrome.log). The initial pre-change direct run did restart Network Service (direct-stderr.txt:5). ACL deployment script retains sandbox isolation, but the fault's exact cause is not proven by this warm A/B.

## Measurement details and validation
- Command: `PTRACK_TMP=C:/src/tmp/m2/runtime bash /c/src/tmp/m1/test.sh 60 --url=http://127.0.0.1:8300/tools/latency/remote-page.html --cr-enable-logging --cr-remote-debugging-port=9333`. This restarts the chain and calls `node tools/latency/run.mjs --no-mock --port 8300 --secs 60`. Tab mode (no --app), default fixture, three-second warmup.
- Marker lock: row **86**, **one scan**, **1726/1726** callbacks decoded, **0 failures**, 295 duplicate IDs excluded, 1431 latency samples. The fixture runs rAF, so skipped marker IDs do not directly count lost encoded frames. Clock offset 0.0303ms, minimum probe RTT 0.5791ms.
- Input uses ordinary `rk.input.sendKey` (keydown/up), then the ordered binary input DataChannel. Fixture toggles a large green/magenta block on keydown; RKMeasure samples decoded video at 75% width/50% height. Send/detect use viewer performance.now, with one outstanding key and a five-second timeout. All 120 colours alternate; measured send spacing **458.6-543.8ms**, meeting >=300ms.
- Raw input timestamps are in JSON `summary.inputSamples`. Original 60s CSV has empty i2p columns; a final small change also exports colour samples into CSV, syntax checked but not remeasured. Capture and input were collected concurrently in the same 60s run. Mock sequence-based measurement remains when --no-mock is absent.
- Validation: adjusted real chain 8s/8s/60s; JS syntax checks on measure.js and run.mjs; endpoint-only incremental build reported success without executable changes. No Chrome/full build or test suite.

## Before/after evidence
Before ACL, `direct-stderr.txt:5`:
```
[19932:5224:1026/100502.104:ERROR:content\browser\network_service_instance_impl.cc:652] Network service crashed or was terminated, restarting service.
```
After ACL, all three saved chrome logs have zero service-crash/bootstrap/retry lines and frames from the first requested page. `acl-run2-state.json` and `acl-run3-state.json` show only NetworkSandboxState=1. `network-process.txt` preserves `--service-sandbox-type=network`. Final ACLs remain applied (`acl-final.txt`). The passing warm rollback is a limit on causality, not omitted evidence.

## Widevine evidence
- Task-local `widevine.html` served on loopback port 8310 and loaded in **remote out/dev Chromium**. Exact requested H.264/cenc EME configuration succeeded; `createMediaKeys` succeeded. Results appear in body/title, `widevine-result.json`, and `widevine-chrome.log:13129,13160`.
- Manifest/license URLs verified against [official Shaka demo asset definitions](https://raw.githubusercontent.com/shaka-project/shaka-player/main/demo/common/assets.js) lines 353-365, saved as shaka-assets.js: `https://storage.googleapis.com/shaka-demo-assets/angel-one-widevine/dash.mpd` and `https://proxy.uat.widevine.com/proxy`. Official compiled release script downloaded HTTP 200 (shaka-download.txt).
- License POST **HTTP 200**, **1099 bytes** (`widevine-network.json`; chrome log:13740). Active encrypted AVC + AAC track, `video.mediaKeys != null` (components-check.txt). ReadyState=4, paused=false; video time advances **0.884 to 21.885s** in recorded samples, later reached 60s.
- Screenshots: `widevine-remote-15s.png`, `widevine-remote-30s.png`, `widevine-viewer.png`, `widevine-viewer-later.png`. Viewer screenshot inspected: decoded episode imagery and EME/license results are visible through normal WebRTC. CDP was used to navigate/read results.
- Process module inventory confirms bundled `out/dev/WidevineCdm/_platform_specific/win_x64/widevinecdm.dll`, **4.10.3112.0**, PID 20988 (`widevine-loaded-modules.txt`).
- `chrome://components` actually **lists Widevine Content Decryption Module 4.10.3112.0** in this build (`components.txt:805-810`, components.png). This contradicts design/07-widevine.md; design edits were outside scope.

## API/source citations
- [V] `sandbox/policy/features.cc:17-20,63-66`: network sandbox and LPAC feature declarations; LPAC token itself was not inspected. State retained network sandbox rather than claiming LPAC-specific causality.
- [V] `chrome/browser/net/system_network_context_manager.cc:124-142`: histogram 1=enabled-by-platform, 4=disabled-after-failed-launch. Runtime state 1 proves the successful runs did not silently use that fallback.
- [V] `third_party/blink/renderer/modules/encryptedmedia/navigator_request_media_key_system_access.idl:10` and `media_key_system_access.idl:13`: requestMediaKeySystemAccess and createMediaKeys declarations. Loopback supplies the observed secure context.
- [V] `chrome/common/media/cdm_registration.cc:105-108`: Windows/Mac register through component update and load on demand. `chrome/browser/component_updater/widevine_cdm_component_installer.cc:79,320-438` registers the selected compatible CDM. Runtime module path confirms which file loaded.
- [V] `viewer/input.js:123-136`: sendKey uses action 0=down,1=up; `viewer/protocol.js:encodeKey` serializes the normal binary path used here. No new C++ API calls/hooks.
- [I] Missing ALL APPLICATION PACKAGES access may explain initial AppContainer bootstrap failure; passing warm rollback prevents definitive attribution. Cold-machine A/B or a launch stack remains unverified.
Runtime chrome/endpoint/control-plane and fixture server stopped; ACLs retained.
Final checks: git diff --check, JS syntax, PowerShell parser passed; no runtime Chrome/endpoint left running.
