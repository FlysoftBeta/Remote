# Multi-tenant isolation (Stage 8; default off)

**2026-10-28, m6-iso2:** [V] Whole-browser LPAC + all eight outer-job UI bits + a private desktop passes hardware streaming,
input/IME, Widevine MediaKeys creation and the controlled cross-tenant probes. Plain AppContainer and Job with the full UI mask
also pass functional smoke. **Default remains `--isolation=off`.** These are dev-build checks, not a completed production security review.

## 1. Trust boundary

One tenant is one browser process tree and one dedicated user-data directory. Tabs share the tenant. `re`, the control-plane,
the Windows account running them, and OS brokers are trusted. Containers protect against an untrusted browser; ordinary processes
outside the containers running as the broker account can read all profiles. No per-tenant Windows accounts are required.

LPAC supplies the package/capability access check; a protected profile DACL contains only the tenant SID, broker user, SYSTEM and
Administrators. A private desktop separates window discovery and hooks. The outer Job supplies lifecycle, resource limits and UI
restrictions. Chromium's restricted child tokens, mitigations and child process/memory job limits remain in use.

## 2. Achieved modes

| `re --isolation=` | Behavior | Stage 8 evidence |
|---|---|---|
| `off` (default) | Existing ordinary launch and kill-on-close Job | final S baseline |
| `job` | Atomic job assignment; optional UI mask; private desktop when mask is nonzero | full-mask marker/input/GPU smoke |
| `appcontainer` | Job + per-profile AC SID + private desktop | full-mask marker/input/GPU smoke |
| `lpac` | Same, with `ALL_APPLICATION_PACKAGES_OPT_OUT` | chosen configuration; detailed checks below |

`--job-ui-limits=none|all|<comma-separated names>` defaults to none; `all` is 255. The names are in section 6.
Use **`--isolation=lpac --job-ui-limits=all`** for the verified configuration. Partial masks remove the child's separate UI mask too;
they are useful for diagnosis and provide only the requested outer UI restrictions. Job mode has no filesystem/capability boundary.
Unknown modes/options or container launch failures fail without retrying a weaker mode.

## 3. Provisioning and bounded fixes

`re/browser_isolation.*` creates/reuses `rb.<SHA1 of lowercase absolute profile path>` with CreateAppContainerProfile. Install and
profile must be separate trees. Install grants are RX; profile grants are FullControl (Chromium needs WRITE_DAC/WRITE_OWNER) with
low integrity. Profile DACL inheritance is disabled, so a shared parent's package RX cannot expose another tenant's data.
As of the 2026-10-29 experiment, `re` provisions only the dedicated install/profile trees; it does not read or modify ancestor DACLs.
Chromium's no-execute file protections remain enforced by patch 21's targeted path-query fallback. Existing ancestor grants from
earlier launches are not removed automatically; validate this experiment with a fresh profile/package SID.
Removing ancestor grants alone failed at `IsPathSafeToSetAclOn` for `Default/LocalStorage`. Patch **21-rb-noexecute-handle-path**
adds a fallback only for this check: when long-name expansion fails, compare normalized NT paths queried from metadata-only
handles for both the target and allowed root. Keep containment and deny-execute ACL enforcement. A fresh user-Temp profile then
passed first launch/restart; ancestor DACLs were unchanged and explicit execute access to LocalStorage was denied (error 5).
This is startup/protection smoke, not a new full streaming/security qualification. See [the audit and experiment](research/windows-ancestor-path-access.md).
A provisioning mutex prevents concurrent DACL lost updates;
identical explicit ACEs are retained to avoid reordering and propagating the whole install tree on each tenant switch.

LPAC uses **five capabilities**: internetClient, registryRead (DLL activation), lpacCom (COM STA), lpacCryptoServices (OSCrypt), and
lpacIdentityServices (identity probes). AC uses internetClient. `--isolation-capabilities=<names>` adds deduplicated explicit extras;
server/network extras were unnecessary for the chosen mode. Environment contains only system paths, dedicated USERPROFILE/APPDATA/
LOCALAPPDATA/TEMP/TMP and the desktop hand-off. Windows' redirected `LOCALAPPDATA/Packages/<name>/AC/Temp` is provisioned as well.

Nested network AC profile creation formerly returned SBOX44. `re` now disables NetworkServiceSandbox, RendererAppContainer,
PrintCompositorLPAC and WinSboxNetworkServiceSandboxIsLPAC for container launches. The network child inherits the outer token;
the other exercised children retain restricted tokens derived from it. `--no-sandbox` remains rejected. NetworkServiceInProcess2
also streamed during the runtime experiment, but the selected mode keeps a separate network process. There is no GpuLPAC feature
in this pin's exercised Windows GPU policy.

Patch **20-rb-outer-isolation** is 64 insertions/1 deletion across four upstream files. It uses LOCAL Crashpad pipes, opens the
broker-provisioned sandbox desktop, reports child launch errors, and clears only nested child job UI masks when a nonzero outer
UI mask is queried under rb-isolation. It retains the jobs, active-process restrictions, memory limits and tracking. No jobless
sandbox lifecycle changes or core IPC changes were necessary. `re` also starts the browser on that private desktop; each launch
gets a fresh UUID name. Its ACL includes the package SID and Chromium's Restricted-token deny mask, with untrusted integrity.

## 4. Decision

D24 now selects opt-in **LPAC + all UI bits + private desktop**; plain AC is a working functional fallback. The original Stage 6
failures were resolved within the small-patch budget. Keep default off as instructed. Keep the tested network feature override;
do not substitute a global no-sandbox launch. Do not present Job alone as a tenant security boundary.

## 5. Build and enable

```powershell
.\browser\cr.cmd build dev "chrome re"
# Legacy shared binary provisioning helper, if required by the deployment:
.\tools\isolation\grant-lpac-acl.ps1 -BuildDirectory C:\src\chromium\src\out\dev
C:\src\chromium\src\out\dev\re.exe --chrome=C:/src/chromium/src/out/dev/chrome.exe --user-data-dir=C:/tenants/a --isolation=lpac --job-ui-limits=all
```

`re` provisions the install/profile and desktop, leaving ancestor/root ACLs unchanged. The optional `grant-traversal.ps1` helper
still exists for explicit deployment provisioning and historical probes; it is not called by `re`. It grants directory-only RX
without inheritance. A shared ProfileParent passed to grant-lpac-acl gets directory-only traversal, never inherited shared package RX.
Profile registrations and install grants persist for reuse; removing a tenant must retire its registration and explicit SID ACEs,
including any ancestor grants left by versions before this experiment.

For tests only, local HTTP fixtures need `CheckNetIsolation LoopbackExempt -a -p=<tenant SID>` (elevated). The test exemptions are
removed after validation; none is added by `re`. Do not enable this in deployment merely to make a test fixture work: it grants the
contained browser access to host loopback services. Browser CDP inbound loopback remained blocked, so input/GPU/MediaKeys assertions
used a controlled fixture's outbound state report. The viewer's CDP connection drives real pointer/key/Unicode/IME events.

## 6. UI-bit experiment

Original failure [V]: browser broker CreateProcessAsUserW returns 50/ERROR_NOT_SUPPORTED for renderer, network and storage utility
children with overlapping job UI restrictions. GPU/browser survive. DESKTOP fails earlier in the browser's CreateAltDesktop:
CreateDesktopW returns NULL and GetLastError=5, captured by CDB at the API return. The fix gives UI ownership to the outer Job and
opens the desktop created before entering that job. Windows nested jobs themselves remain enabled.

Each row below is [V], LPAC, 20s marker + pointer/key/Unicode/IME, four variants concurrently at 970x558, hardware Intel UHD630 D3D11
and native VPL/shared fences. All input probes committed exact `a?????` and checked composition preedit.

| Only outer bit | Value | Decoded marker frames | GPU / input |
|---|---:|---:|---|
| handles | 1 | 1023 | pass / pass |
| readclipboard | 2 | 999 | pass / pass |
| writeclipboard | 4 | 959 | pass / pass |
| systemparameters | 8 | 993 | pass / pass |
| displaysettings | 16 | 940 | pass / pass |
| globalatoms | 32 | 1003 | pass / pass |
| desktop | 64 | 968 | pass / pass |
| exitwindows | 128 | 919 | pass / pass |

Full-mask private-desktop smoke [V]: LPAC A/B **933/956**, Job **970**, AC **966** decoded markers, input/IME and Intel D3D11 pass.
A later protected-profile run had concurrent LPAC A/B **947/935** decoded, both input pass. Widevine `requestMediaKeySystemAccess`
followed by `createMediaKeys()` succeeded; licensed playback, VMP and high-security licences were not tested.

## 7. Cross-tenant and recovery verification

The standalone [probe](../tools/isolation/README.md) is launched through the re isolation path with A/B's package SIDs and dedicated
profiles. Helpers have fresh per-launch jobs/desktops using the same policy, rather than the broker's token. Two real LPAC browser
instances stream concurrently with Intel GPU; helpers check controlled B windows/files. Host positive controls pass file read,
FindWindow, PostMessage, hook installation and clipboard canary read.

| A operation against B | LPAC, no UI mask | LPAC, full UI mask |
|---|---|---|
| Read B canary file | denied 5 | denied 5 |
| Open known B desktop | denied 5 | denied 5 |
| FindWindow by B title | no HWND, error 0 | no HWND, error 0 |
| PostMessage to known B HWND | denied 1400 | denied 1400 |
| SetWindowsHookEx (valid DLL/thread) | denied 5 | denied 5 |
| OpenClipboard/read canary | denied 5 | denied 5 |

Before private startup desktops, FindWindow exposed B's HWND and hook installation returned success even with all UI bits. That
failed candidate is not the chosen policy. Clipboard is still a window-station resource, not a per-desktop namespace; the observed
AppContainer denial plus the full Job's read/write restrictions are the protection here. B itself could not open that clipboard.

Token snapshot [V]: browser, Intel GPU, network, storage, unzip and five renderers (10 live processes) all retain the same tenant SID,
AppContainer=1 and observed LPAC opt-out=1. The opt-out is Chromium's internal WIN://NOALLAPPPKG attribute, not a portable documented
LPAC token-identity API. Windows rejected TokenIsLessPrivilegedAppContainer on this host.

Selected `tools/soak --smoke` [V], final LPAC/all/private/protected-profile policy: browser crash recovered automatically in **19,707ms**;
GPU kill/replacement plus new frames in **1,099ms**, within 30s. Marker-read failures during faults **143/4**, dropped-frame deltas **0/0**;
recorded >500ms freeze deltas **0/0**. The browser test overlapped other functional checks; this is a recovery check, not a startup
performance comparison. The harness used only a temporary adapter for the new viewer Connect flow and separate CP data directories.

[V] Final dev build `rb-build-20261028-004750.log`: incremental chrome/re passed in 35s. One serial scenario S comparison,
60s/run, m3-combined, loopback/headless, order off1/LPAC1/off2/LPAC2; actual 1890x938, native VPL and shared fences throughout.
All four have zero >200ms freezes. Before each measurement the harness reapplies Idle to any siso/clang-cl/lld-link processes;
all four snapshots are empty, so no background release build was observed.

| Run | capture?decoded P50 / P99 ms | input?decoded P50 / P99 ms | samples capture / input | marker-read failures |
|---|---|---|---|---|
| off 1 | 49.4 / 56.3 | 66.1 / 87.3 | 2775 / 110 | 146 |
| LPAC/all 1 | 34.3 / 44.7 | 44.3 / 63.1 | 2725 / 109 | 162 |
| off 2 | 34.2 / 42.7 | 41.9 / 63.2 | 2738 / 110 | 141 |
| LPAC/all 2 | 34.1 / 41.3 | 52.5 / 70.7 | 2714 / 109 | 163 |

Capture P50 paired differences are -15.1/-0.2ms; input P50 differences -21.8/+10.6ms. The off baseline itself varies 15.2ms at
capture P50 and 24.2ms at input P50. These two pairs do not establish a repeatable latency change attributable to isolation.
Marker-read failures are not measured H.264 decoder errors; percentiles include valid markers only. No additional S trials were run.
The six temporary test loopback exemptions were removed; the one pre-existing exemption was retained. No owned test processes remain.


## 8. Known gaps and next steps

- These checks cover the exercised process tree and controlled canaries, not every Windows resource or a hostile-browser security audit.
  Broker handle access, reparse/canonical-path races, profile retirement and least-privilege ACL review remain production work.
- Network proxy/PAC, XR, WebNN, Media Foundation protected CDM and other unexercised utilities may still request nested AC profiles and
  hit SBOX44. Keep those features out of the supported surface until separately validated with a bounded policy/provisioning fix.
- Shared GPU scheduling/VRAM, driver/kernel attacks, OS broker attack surface and external-network policy are not isolated by a Job.
  Private desktops share a window station; do not infer general clipboard-object separation from this test.
- Component updater/SODA extraction logs FILE_ERROR_NOT_FOUND in container profiles. Speech/model update behavior was not validated.
  Widevine MediaKeys readiness does not establish end-to-end DRM playback.
- Default stays off. Next is production ACL/path/broker review and packaged-build validation; no release output was touched.

Evidence: `C:/src/tmp/m8/iso2` (ui4-*, private-*, concurrent-*, boundary*, tokens-final, desktop-api-cdb, handles-api, soak, S-final-*).
Report: `C:/src/tmp/m8/out/iso2.md`. Earlier failed candidates remain in the local Stage 6/8 evidence and are not deployment modes.
