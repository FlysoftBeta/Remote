# Multi-tenant isolation (Stage 6 experiment; default off)

**2026-10-27, m6-iso1:** Job lifecycle containment works. All eight outer-job UI-limit bits fail the real browser pipeline. Whole-browser
LPAC and plain AppContainer launch prototypes exist behind `re --isolation=...`, but neither passes child sandbox initialization. They are
experimental and unsuitable for deployment. **The usable fallback is Job with zero UI bits; it is not a tenant security boundary.** Default
is `--isolation=off` until v0.2.0. Builds and tests below use `out/dev` only; the concurrent release was not modified.

## 1. Trust boundary
- An instance = one rb process tree (browser + GPU + network + utility + renderers) + one user-data-dir. Tabs inside an instance are
  **not** different tenants.
- `re` and the control-plane are outside the tenant container and act as trusted brokers; a compromised browser must not obtain the
  endpoint's process handle, other tenants' texture handles, files or network rights.
- Chromium's renderer/GPU sandbox still protects page -> browser; the outer boundary protects compromised browser -> host/other tenants.

## 2. Stages
| Stage | Mechanism | Provides | Prior evidence (earlier project) |
|---|---|---|---|
| I0 (from MVP) | one Job Object per instance (`PROC_THREAD_ATTRIBUTE_JOB_LIST` at `CreateProcess`, `KILL_ON_JOB_CLOSE`), process/memory limits, explicit `--user-data-dir`, `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` for inheritance | lifecycle, limits, whole-tree reclaim on endpoint crash | implemented and passing there |
| I1 | per-tenant local Windows account (or low-privilege token) + profile/temp ACLs | minimum boundary between mutually distrusting tenants | designed, untested |
| I2 (experiment) | whole browser tree in a unique AppContainer SID (plain AC, then LPAC) | file/registry/named-object/network capability isolation | partial: Crashpad `CreateNamedPipe` ACCESS_DENIED (fixed), `SBOX12/OpenWindowStation 5` with the pre-created alternate desktop, LPAC not working |
| I3 (optional) | per-tenant desktop (`CreateDesktop` + AC SID ACL) | cross-tenant `FindWindow/PostMessage`, hooks, clipboard isolation | named window station needs admin; private desktop in the same station stalled |

## 3. Known pitfalls (from the earlier project, not yet re-verified here)
- Confirmed here: seven individual outer UI bits cause child `CreateProcessAsUserW` failure (`SBOX18`, Win32 50); `DESKTOP` crashes the
  browser earlier at alternate-desktop initialization. No nonempty combination is usable.
- AppContainer needs `USERPROFILE/APPDATA/LOCALAPPDATA` on AC-writable paths; Crashpad pipe names must live in the AC namespace.
- A D3D11 hardware device can be created inside AC (Intel LUID matched), but verify "not SwiftShader/WARP" at every start.
- Shared texture NT handles: created by the browser, duplicated by `re` (`DuplicateHandle` with the browser process handle); keep that
  direction; no named shared objects.
- A Job does not isolate GPU scheduling or VRAM; mutually hostile tenants eventually need a VM boundary.

## 4. Decision
**User target (2026-10-26):** Job Object + `JOB_OBJECT_UILIMIT_*` + LPAC, managed by `re` (creates the job, LPAC profile/capabilities and ACLs, and
launches the whole rb tree with the LPAC token). Small Chromium patches are allowed (nested job, alternate desktop/window station,
Crashpad pipe, GPU/D3D11 access). **Give up** if the Chromium change exceeds ~1k lines or is likely to conflict in future versions
(core sandbox/IPC hot files): fall back to Job (+UILIMIT if possible) + plain AppContainer (no per-tenant accounts: too heavy) and record why.

Current decision: retain I0, leave the new modes opt-in, and stop at the nested child-AppContainer policy boundary under the high-conflict
give-up criterion. The ~1k-LOC threshold was not reached. No renderer/GPU/network sandbox was disabled. Rejected: `--no-sandbox` as an AC
compatibility fix; outer Job UI bits as the UI-isolation mechanism; per-tenant named window stations.

## 5. Implemented switches and provisioning

| `re` mode | Behavior |
|---|---|
| `off` (default) | Existing kill-on-close Job and ordinary Chromium launch. |
| `job` | Atomic `PROC_THREAD_ATTRIBUTE_JOB_LIST` assignment, kill on close, explicit Mojo handle inheritance. UI mask defaults to zero. |
| `lpac` | Job + profile-specific AppContainer SID + `ALL_APPLICATION_PACKAGES_OPT_OUT`; startup prototype, blocked below. |
| `appcontainer` | Same provisioning without the LPAC opt-out; startup prototype, blocked below. |

`--job-ui-limits=none|all|<comma-separated names>` accepts the lowercase names in the table below. Unknown modes/names fail; container
launch failures never retry with weaker isolation. `--isolation-capabilities=<comma-separated named capabilities>` adds explicitly requested
capabilities to the default `internetClient`; it is only for container modes.

`re/browser_isolation.*` creates/reuses `rb.<hash-of-absolute-profile-path>` via `CreateAppContainerProfile` (derive SID if it already exists).
It grants that SID RX on the browser install tree, FullControl on its dedicated profile (Chromium needs WRITE_DAC/WRITE_OWNER for its own
cache ACLs), and labels the profile low integrity. The host profile owner first receives FullControl so a medium-integrity endpoint can
set the label without elevation. Profile and install must be separate trees. Existing deployment ALL APPLICATION PACKAGES / ALL RESTRICTED
APPLICATION PACKAGES RX grants from `tools/isolation/grant-lpac-acl.ps1` were present on this dev build.

The container environment contains Windows system paths, dedicated USERPROFILE/APPDATA/LOCALAPPDATA/TEMP/TMP under its profile, and the
isolation hand-off variables; broker credentials and the host environment are not copied. Token AppContainer status, package SID and the
observed `WIN://NOALLAPPPKG=1` attribute are checked. The latter is an internal Windows attribute, not a portable documented LPAC identity
API; `GetTokenInformation(TokenIsLessPrivilegedAppContainer)` was unusable on this host and is not used.

`RB_ISOLATION=lpac|appcontainer` selects `\\.\pipe\LOCAL\...` for both Crashpad and its self-monitor. `RB_SANDBOX_DESKTOP` names a private
untrusted-integrity desktop created and retained by `re`, with the package SID and Chromium's Restricted-token UI deny mask; the guarded
Chromium hook opens it instead of creating an alternate window station. This is the **child sandbox desktop**, not a separate desktop for
the browser UI. Container cold startup gets a 60s Hello deadline; off/job keep 15s.

## 6. One-bit UI experiment

Each row used the real VPL/shared-fence chain, marker workload, a 20s viewer measurement, fresh profile and `--v=1` browser file logs.
Chrome stderr is excluded by the explicit inherited-handle list, so equivalent `--enable-logging --log-file=...` logging was used.

| Bit / value | Browser starts | Hardware GPU | Valid decoded markers | Input / IME | Failure |
|---|---|---|---|---|---|
| none / 0 | yes | Intel UHD 630, D3D11 | 690 | pass | none |
| HANDLES / 1 | yes | yes | 0 | unreachable | renderer + sandboxed utility launch, `SBOX18`, Win32 50 |
| READCLIPBOARD / 2 | yes | yes | 0 | unreachable | same |
| WRITECLIPBOARD / 4 | yes | yes | 0 | unreachable | same |
| SYSTEMPARAMETERS / 8 | yes | yes | 0 | unreachable | same |
| DISPLAYSETTINGS / 16 | yes | yes | 0 | unreachable | same |
| GLOBALATOMS / 32 | yes | yes | 0 | unreachable | same |
| DESKTOP / 64 | process created; no Hello | not reached | 0 | unreachable | browser `Sandbox::Initialize -> CreateAlternateDesktop` CHECK; exit `0x80000003` |
| EXITWINDOWS / 128 | yes | yes | 0 | unreachable | renderer + sandboxed utility launch, `SBOX18`, Win32 50 |

All successful bits combined = **none (0)**. Seven failures are `CreateProcessAsUserW` returning `ERROR_NOT_SUPPORTED` (50), captured through
CDP `Browser.getHistogram("Process.Sandbox.Launch.Error")`; `sandbox/win/src/broker_services.cc` maps that call's failure to SBOX18. Browser
and hardware GPU survive while renderers/utility launches fail. Occasional startup blank frames do not count as decoded markers. The
DESKTOP dump establishes the failing Chromium initialization call and exit code; its lower-level Win32 failure code was not captured.

Chromium retains its own child jobs and UI restrictions; the unrestricted outer job works with those jobs, while the outer UI mask makes
child creation fail. No breakaway or child-job suppression was enabled. Microsoft's [nested-job constraints](https://learn.microsoft.com/en-us/windows/win32/procthread/nested-jobs)
explain why Windows 10+ support for nested jobs does not make outer UI restrictions compatible.

## 7. Container blockers and bounded fixes

| Stage | Evidence | Outcome |
|---|---|---|
| Profile ACL/low label | Win32 5 on fresh directory with inherited Modify | fixed by owner DACL grant before label update |
| LPAC DLL activation | `BasepCreateActCtx/BasepSxsCreateStreams`, `0xC0000022` loading chrome_elf/absl | `registryRead` gets past this |
| Crashpad pipes | `CreateNamedPipe` 5 in browser, then handler `MonitorSelf` | fixed with inherited environment guard and LOCAL prefix |
| Alternate window station | browser `Sandbox::Initialize` CHECK before logging/Hello | fixed by re-created sandbox desktop and guarded `OpenDesktopW` hook |
| LPAC COM STA | `CoInitializeEx`, crash key HRESULT `0x80070005` | `lpacCom` gets past this |
| LPAC OSCrypt | `CryptProtectData`, Win32 5 | `lpacCryptoServices` gets past this |
| LPAC default-browser probe | `GetUserName` failure -> shell_util.cc:2206 NOTREACHED | `lpacIdentityServices` gets past this; exact GetLastError not logged |
| Network cache/log ACLs | grants fail with 5 under profile Modify | fixed with package FullControl on its own profile; errors disappear |
| Nested child AppContainer | both modes: network sandbox=5, result=44 (`SBOX_ERROR_CREATE_APPCONTAINER`), last Win32 error=0, then DCHECK | **blocked; give-up boundary** |

LPAC reaches the final blocker with five capabilities total: internetClient, registryRead, lpacCom, lpacCryptoServices, lpacIdentityServices.
Plain AppContainer reaches it with internetClient alone. Both root tokens and the LPAC opt-out attribute were verified. Result 44 maps to
`ConfigBase::AddAppContainerProfile -> AppContainerBase::CreateProfile`; this path does not preserve its underlying HRESULT/Win32 error.
The logged zero is not a successful registration. Continuing would
change Chromium's core nested profile/token policy; that behavior was left intact. The added guarded failure log reports sandbox/result/
Win32 error for this boundary. There is no usable AC fallback, no valid container marker smoke, and no meaningful container latency A/B.
GPU/D3D11 access from the outer container, texture/fence imports, child-token confinement, external networking, fonts and crash/GPU recovery
remain unverified. The early Mojo handshake alone does not validate these properties.

## 8. Run, validation and next steps

```powershell
.\browser\cr.cmd build dev "chrome re"
$env:ENDPOINT_PATH='C:/src/chromium/src/out/dev/re.exe'
$env:ENDPOINT_ARGS='--chrome=C:/src/chromium/src/out/dev/chrome.exe --user-data-dir=C:/src/tmp/iso-profile --isolation=job --job-ui-limits=none'
# Use the latency/soak commands in tools/latency/PRESETS.md and tools/soak/README.md.
# Experimental startup only (currently fails child policy):
# --isolation=lpac --isolation-capabilities=registryRead,lpacCom,lpacCryptoServices,lpacIdentityServices
# --isolation=appcontainer
```

Final incremental `chrome re` build passed in 17s (`rb-build-20261027-192621.log`). Same-build job marker20s: **1,055 decoded, zero marker
failures**, hardware Intel UHD630/D3D11, native VPL/shared fences. Viewer pointer/key/Unicode/IME preedit+commit probes passed with exact
remote text `a中🙂你好`. The latency KeyI timer was suppressed during this separate DOM/IME probe; the S measurements kept it intact.

Scenario S, 60s/run, loopback/headless, m3-combined preset, order off1/job1/off2/job2. Actual video 1896x942, receiver ~59.94–60.06fps;
all four runs have zero >200ms freezes and hardware VPL throughout. S has **173–185 marker-read failures per run** (cause not investigated);
the percentiles below include valid marker samples only. These are marker-read failures, not a measured H.264 decoder-error count.

| Run | capture→decoded P50 / P99 ms | input→decoded P50 / P99 ms | samples capture / input | marker failures |
|---|---|---|---|---|
| off 1 | 34.7 / 45.7 | 43.9 / 62.9 | 2677 / 107 | 185 |
| job 1 | 34.8 / 44.2 | 40.3 / 67.1 | 2671 / 107 | 183 |
| off 2 | 34.8 / 42.8 | 41.9 / 60.5 | 2677 / 107 | 174 |
| job 2 | 34.9 / 45.6 | 51.6 / 71.2 | 2677 / 108 | 173 |

Paired capture P50 differences are +0.14/+0.13ms; input P50 differences -3.6/+9.7ms. Treat these as noise. Input P99 is +4.2/+10.7ms: the
larger difference occurs in one pair only, so this does not establish a repeatable regression. This compares **off vs I0 job**, not container
performance. Build-priority snapshots before every measurement are empty: no siso/clang-cl/lld-link or concurrent release build observed;
the harness re-applies Idle if any appear.

Selected `tools/soak --smoke` in job mode passed: browser-crash automatic recovery **2978ms**, endpoint-crash **2865ms**, GPU replacement plus
three new frames **1013ms**, all within 30s. Each selected scenario recorded one freeze; marker-failure counts were 5/5/1, with dropped-frame
deltas zero. This is a short recovery check, not a long-run leak/robustness certification.

Evidence: `C:/src/tmp/m6/m6-iso1` (`job-final`, `S-*`, `soak/soak.json`, UI and container diagnostics); orchestrator report:
`C:/src/tmp/m6/out/iso1.md`. Patch20 has 44 insertions/1 deletion across three upstream files and passes reverse `git apply --check`. It was
exported as an exact working diff without commits; the orchestrator must commit the Chromium maintenance unit with its Patch-Name trailer
before the normal `cr export` workflow, which exports committed units.

The usable job mode isolates **lifecycle only**. It adds no filesystem, registry, network, clipboard, window-message, GPU scheduling or VRAM
boundary between tenants; Chromium's existing renderer/GPU sandbox remains essential. Do not host mutually hostile tenants on this basis.
Next work is a separate design for trusted provisioning of Chromium's nested container profiles/tokens, then container GPU/IPC/network
validation and restart testing. Profiles/SIDs and install RX grants persist for reuse; retiring a profile also needs removal of its explicit
install ACL grant and its Windows AppContainer registration. Production ACL/reparse-path and broker access reviews remain outstanding.
