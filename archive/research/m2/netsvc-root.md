# Network Service startup crash investigation

Started: 2026-10-26. Task: m2-netsvc-root. No repository/Chromium edits, builds, shared launch scripts, or port 8300.

Completed approximately 11:01 SGT (about 16 minutes). **The current startup crash did not reproduce; its exact root cause and real fix remain unproven. Keep the PRTS startup-navigation retry.** Baseline, sandbox-disabled, scheduled launch and Temp profile comparisons each have 3 complete fresh-profile runs. Three WER-enabled HTTPS runs and three active-handler CDB runs also passed. See final-state.json and the individual run table; interrupted runs are excluded.

## Work log
- Created task notes before running discriminators.
- Final deliverables: this file, RUNS.md, summary.json, debugger-summary.json, final-state.json, deployment-acls.json, task-local runner/token/CDP/debugger/summarizer scripts, and per-run logs/metadata/histograms/ACL captures. Source paths were read only. No git commits, repository or Chromium edits, builds, broad kill commands, or shared launch scripts were used.
- Profiles are retained under this task's profiles/ directory, except the explicitly requested comparison profiles under %LOCALAPPDATA%/Temp/remotekit-m2-netsvc (full unique paths are in each temp-N/run.json). Other artifacts stayed in the task directory.
- Temporary scheduled task RemotekitM2NetSvc-20261026 and the newly created HKCU LocalDumps/chrome.exe key were removed. Final inventory found zero own Chrome/CDB processes and zero dumps in either profile root.

## Run table
| Run | Profile parent | Seconds | Network restart | Sandbox state (value:count) | Early crash | Grant result | Status |
|---|---|---:|---:|---|---|---|---|
| baseline-1 | C:/src/tmp/m2/netsvc | 15.29 | no | 1:5 | absent | not collected | complete |
| baseline-2 | C:/src/tmp/m2/netsvc | 15.24 | no | 1:5 | absent | not collected | complete |
| baseline-3 | C:/src/tmp/m2/netsvc | 15.26 | no | 1:5 | absent | not collected | complete |
| disabled-1 | C:/src/tmp/m2/netsvc | 15.22 | no | 0:5 | absent | not collected | complete |
| disabled-2 | C:/src/tmp/m2/netsvc | 15.19 | no | 0:5 | absent | not collected | complete |
| disabled-3 | C:/src/tmp/m2/netsvc | 15.21 | no | 0:5 | absent | not collected | complete |
| scheduled-1 | C:/src/tmp/m2/netsvc | 15.26 | no | 1:5 | absent | 0:1,7:1 | complete |
| scheduled-2 | C:/src/tmp/m2/netsvc | 15.14 | no | 1:5 | absent | 0:1,7:1 | interrupted/excluded |
| scheduled-3 | C:/src/tmp/m2/netsvc | 15.26 | no | 1:5 | absent | 0:1,7:1 | complete |
| temp-1 | LocalAppData/Temp | 15.21 | no | 1:5 | absent | 0:1,7:1 | complete |
| temp-2 | LocalAppData/Temp | 15.28 | no | 1:5 | absent | 0:1,7:1 | complete |
| temp-3 | LocalAppData/Temp | 15.3 | no | 1:5 | absent | 0:1,7:1 | complete |
| scheduled-4 | C:/src/tmp/m2/netsvc | 15.32 | no | 1:5 | absent | 0:1,7:1 | complete |
| wer-1 | C:/src/tmp/m2/netsvc | 12.42 | no | absent | absent | absent | interrupted/excluded |
| wer-2 | C:/src/tmp/m2/netsvc | 15.24 | no | 1:5 | absent | 0:1,7:1 | complete |
| wer-3 | C:/src/tmp/m2/netsvc | 12.49 | no | absent | absent | absent | interrupted/excluded |
| wer-4 | C:/src/tmp/m2/netsvc | 15.25 | no | 1:5 | absent | 0:1,7:1 | complete |
| wer-5 | C:/src/tmp/m2/netsvc | 12.35 | no | absent | absent | absent | interrupted/excluded |
| wer-6 | C:/src/tmp/m2/netsvc | 15.36 | no | 1:5 | absent | 0:1,7:1 | complete |
| debugger-1 | C:/src/tmp/m2/netsvc | 15.12 | no | 1:5 | absent | 0:1,7:1 | complete (handlers not active) |
| debugger-2 | C:/src/tmp/m2/netsvc | 15.2 | no | 1:5 | absent | 0:1,7:1 | complete (handlers not active) |
| debugger-3 | C:/src/tmp/m2/netsvc | 15.19 | no | 1:5 | absent | 0:1,7:1 | complete (handlers not active) |
| debugger-4 | C:/src/tmp/m2/netsvc | 12.36 | no | absent | absent | absent | interrupted/excluded (handlers active) |
| debugger-5 | C:/src/tmp/m2/netsvc | 15.19 | no | 1:5 | absent | 0:1,7:1 | complete (handlers active) |
| debugger-6 | C:/src/tmp/m2/netsvc | 15.18 | no | 1:5 | absent | 0:1,7:1 | complete (handlers active) |
| debugger-7 | C:/src/tmp/m2/netsvc | 12.42 | no | absent | absent | absent | interrupted/excluded (handlers active) |
| debugger-8 | C:/src/tmp/m2/netsvc | 15.18 | no | 1:5 | absent | 0:1,7:1 | complete (handlers active) |

## API citations
[V] = read in the pinned checkout (shared HEAD read as `5006faed8bb5040a2654b98d5ee2c3961df966ba`); [I] = inference. Runtime artifacts are direct observations. Source paths below are relative to C:/src/chromium/src. No new Chromium APIs were called or source changed.

- [V] `sandbox/policy/features.h:18` declares kNetworkServiceSandbox; `sandbox/policy/features.cc:17-20` defines the feature (NetworkServiceSandbox), and :63-66 defines optional WinSboxNetworkServiceSandboxIsLPAC, disabled by default. `testing/variations/fieldtrial_testing_config.json:11461-11474` enables NetworkServiceSandboxWindows/Enabled_20231025; baseline-1/stderr.log:301 confirms selection at runtime. `sandbox/policy/win/sandbox_win.cc:427-436` configures network AppContainer and consults the separate LPAC feature.
- [V] `chrome/browser/net/system_network_context_manager.cc:124-142` defines sandbox states 0=disabled by platform, 1=enabled by platform, 4=disabled following failed launch. :274-318 selects state and :1096-1121 logs NetworkSandboxState. :449-460 records early bootstrap crash and failed-major-version preference. Histogram sample counts are configuration checks, not numbers of process launches.
- [V] `content/browser/network_sandbox.h:24-43` declares directory-grant APIs. `content/browser/network_sandbox.cc:237-279` derives the named capability SID, checks directory access, and grants recursive read/write/execute/delete. `base/win/security_util.h:25-29` declares GrantAccessToPath. `content/browser/network_sandbox_grant_result.h:15-37` defines 0=success, 5=cache grant failure, 6=data grant failure, 7=no grant attempted. `network_sandbox_grant_result_helper.cc:91-112` records GrantSandboxResult and logs errors; successful grants need not emit an individual v=1 success line.
- [V] `content/browser/service_host/utility_process_host.cc:586-588,630-639` transfers/reads the bootstrap event to classify pre-IPC failure. `content/utility/utility_main.cc:215-221` signals it; LowerToken is later at :500. This supports the historical bootstrap-stage localization, not a unique faulting instruction.
- [V] `chrome/browser/prts/prts_controller.cc:36-145` guards recovery with an observed pre-IPC crash, replacement service readiness, an uncommitted initial browser GET/ERR_ABORTED, no competing navigation, and one retry; it retires on a committed primary navigation. The guard does not fix the service's cause.
- Windows helper signatures and enum/layout constants checked in installed Windows SDK 10.0.26100.0 headers (not Chromium C++ additions): `um/jobapi.h:33` IsProcessInJob; `um/jobapi2.h:96` QueryInformationJobObject; `um/processthreadsapi.h:538,888` OpenProcessToken/OpenProcess; `um/securitybaseapi.h:928,1020` GetTokenInformation/IsTokenRestricted; `um/handleapi.h:38` CloseHandle; `um/winnt.h:12041-12082,13714-13716` token enum/elevation and job-limit constants. Helper uses TOKEN_QUERY, PROCESS_QUERY_LIMITED_INFORMATION and x64 native layout. CDB flags verified using its local -? output (cdb-help.txt).

## Root cause and recommendation
**Root cause: unresolved; high confidence in current non-reproduction, low confidence in any specific historical cause.** The historical evidence localizes the original fault to the sandboxed Network Service utility bootstrap before its status event, but every complete fresh-profile baseline here retained the network sandbox and passed. Disabling NetworkServiceSandbox also passed and correctly changed state 1 to 0, so it cannot implicate the sandbox by comparison when the baseline does not fail. Running outside the Codex process ancestry and changing the profile parent likewise made no observable difference. Both launcher tokens are ordinary medium-integrity UAC-limited interactive-user tokens with no restricting SIDs; job membership alone is not an explanation. Runtime Default/Network capability grants succeeded in both profile locations. Existing out/dev package-SID RX grants were present throughout, so this task cannot validate or refute their causal effect on the earlier cold fault; the prior warm rollback already prevents claiming that ACLs fixed it. WER and creation-time debugger attempts obtained no Network Service fault dump or faulting stack. Cold loader/resource access or another intermittent bootstrap precondition remains an inference, not an identified cause [I].

**Recommended real fix:** no code, launch-environment, or deployment-ACL change is proven to repair the original crash. Keep sandboxing enabled and retain the existing deployment ACL provisioning provisionally; do not treat it as root-cause closure or add broad profile-parent package grants, since Chromium's specific Network capability grant is working. A scheduled-task production launcher or moving profiles has no demonstrated benefit. On the next genuinely cold reproduction, use the retained creation-time capture tooling to obtain the first utility fault/termination stack and repair its specific dependency-access or bootstrap precondition. No source patch or sandbox-disable workaround is justified by these passing comparisons.

**Startup navigation retry: KEEP as a guard.** Its removal is not supported by warm clean launches and an unresolved historical bootstrap fault. It can be reassessed after the underlying cause has a captured failure, a targeted repair, and cold fresh-profile validation with the sandbox retained. This task did not edit or exercise PRTS recovery itself.

## Open risks
- No cold reboot, ACL rollback, binary relocation, or dependency-access trace was performed. The deployment ACLs were already applied; current passes do not establish their necessity.
- Short ~15-second launches discriminate startup only. Longer-running service stability and endpoint/PRTS behavior are outside this task.
- Interrupted browser trees are excluded and repeated. The debugger observed abrupt multi-process exit status 0xffffffff, consistent with the concurrent kill script [I], but the terminating process was not captured.
- Current-job queries do not enumerate all enclosing jobs. Both direct and scheduled launches are in jobs; their queried limits differ, but neither reproduced the fault.
- No matching service fault occurred, so no faulting stack can be supplied. No claim of a verified real fix is made.

## Initial discriminators (10:49 SGT)
- [V] Exact feature: sandbox/policy/features.{h,cc}, kNetworkServiceSandbox / NetworkServiceSandbox. Optional WinSboxNetworkServiceSandboxIsLPAC is disabled by default; distinguish AppContainer from LPAC.
- First fresh-profile baseline: 15.29s, zero Network Service restart lines; CDP NetworkSandboxState=1 (5 samples), no EarlyLaunchCrashed histogram. This clean run retained sandboxing rather than silently falling back.
- Shell token: medium integrity S-1-16-8192, IsTokenRestricted=false, no restricted SIDs; shell/browser are in a job. whoami/all shows filtered admin group (deny-only), not an elevated administrator. Initial baseline-1 token JSON mislabeled TokenElevation (class 20) as elevationType; ignore that field for baseline-1. Helper corrected to query class 18 and separately class 20 for subsequent runs.
- Scheduled interactive-user one-off task created successfully; awaiting sequential runs. No changes to ACLs or registry made so far.
- Existing deployment RX ACEs for both package SIDs remain on out/dev. Task-parent C:/src/tmp has ordinary Users RX / Authenticated Users modify but no package SID grant. Default/Network receives a distinct capability SID modify grant at runtime (saved icacls/SDDL).

## Midpoint (10:52 SGT)
- Baseline 3/3 clean, sandbox-disabled 3/3 clean. Baseline state=1 only; disabled state=0 only, confirming the switch works but providing no causal discrimination while the fault is absent.
- Scheduled-1 and scheduled-3 clean; scheduled-2 ended by 15s after a healthy 12s histogram, with no service restart; exclude as interrupted and repeat scheduled-4. Its termination is compatible with the concurrent agent's kill script, but the killer is not proven.
- Scheduled parent chain is Task Scheduler svchost.exe PID2136 -> cmd.exe -> powershell -> own Chrome, outside the Codex launcher ancestry. Scheduled shell/browser still report inJob=true, so scheduled task is not a job-free comparison. Both have medium, non-restricted, non-elevated interactive-user tokens.
- LocalAppData Temp runs are in progress. temp-1 grant histogram has 0=success and 7=no grant attempted (in-memory system context). No grant failure. Profile disk Network directory is Default/Network, not root/Network; saved root checks correctly report missing.
- [V] content/browser/network_sandbox.h:24-43 declares grant APIs; network_sandbox.cc:237-279 derives named capability SID and grants recursive R/W/X/DELETE. It logs failures, not unconditional successful grants. network_sandbox_grant_result_helper.cc:91-112 records GrantSandboxResult and logs failures. base/win/security_util.h:25 declares GrantAccessToPath. Capability modify ACE in icacls is positive runtime evidence of the grant.

## Capture and environment conclusions (10:55 SGT)
- Three valid WER-enabled runs of https://example.com (wer-2/4/6) were clean with NetworkSandboxState=1, no EarlyLaunchCrashed and successful data ACL grants. wer-1/3/5 were interrupted before 15s and excluded. No WER or Crashpad dump found under this task. Per-application HKCU WER key was absent initially, created temporarily with DumpType=2 and task-local DumpFolder, and removed in finally; wer-after.json confirms absence.
- One-off task was deleted after three valid scheduled runs (1/3/4); scheduled-delete.txt and scheduled-query-after.txt retain evidence.
- Codex-launched shell job reports limit flags 0x2800 (BREAKAWAY_OK | KILL_ON_JOB_CLOSE), UI restrictions 0; scheduled task shell job reports flags 0 and UI restrictions 0. Neither has an active-process limit. Current-job query describes the accessible current job and does not enumerate every outer nested job. Both shell tokens are TokenElevationTypeLimited=3, elevated=false, medium integrity, not AppContainer and no restricting SIDs. Network child in temp-2 is positively AppContainer=true, low integrity, sandbox-type=network.
- Existing binary chrome.dll timestamp stayed 10:38:22 and size 178562560 throughout the comparisons; no binary rebuild by this task. Current source HEAD read as 5006faed8bb5040a2654b98d5ee2c3961df966ba (the checkout is shared).
- Additional creation-time CDB capture started using -o (own browser/children only), local out/dev symbols, and second-chance AV/DLL-init/failfast dump handlers. This avoids mistaking an initial debugger breakpoint for the Network Service fault.

## Debugger capture correction (10:57 SGT)
- debugger-1/2/3 had 15s clean sandboxed histograms, but CDB -g skipped the first command prompt and the -cf handler file was never executed (no NETSVC_CREATION_DEBUGGER_CAPTURE marker). These are clean debugged launches only, NOT proof that configured exception handlers were active. Removed -g and set sxd ibp in the command file, then repeated as debugger-4/5/6. This correction changes only task-local tooling.

## Active debugger evidence (10:58 SGT)
- debugger-4/5/6 logs contain the NETSVC_CREATION_DEBUGGER_CAPTURE marker and executed event commands. debugger-4 was interrupted and excluded: parent browser PID11924 (0x2e94) and several children exited with 0xffffffff within ~100ms at 10:57:01; no captured unhandled AV/DLL-init/failfast, no FATAL or service restart. This strongly supports abrupt process-tree termination rather than the original isolated service bootstrap failure [I]; it does not identify the terminating process. The final "No runnable debuggees" occurs after all targets have exited, not a Chrome exception.
- debugger-5 and debugger-6 completed ~15s with sandbox state 1 only, successful capability grants and no early-crash/restart line. debugger-7 repeats the interrupted capture to obtain three complete active-handler captures. No dump produced so far.

## Final verification (11:01 SGT)
- 27 launches total: 15 complete plain comparisons/capture attempts (12 sandboxed, 3 sandbox-disabled); 3 complete CDB captures with active handlers (debugger-5/6/8); 3 earlier complete exploratory debugged launches without active handlers; 6 interrupted/excluded attempts. Every completed run has a fresh unique profile and a ~15-second observation window.
- Active debugger-7 was interrupted and repeated as debugger-8, which passed. Active capture initialization is proven by the NETSVC_CREATION_DEBUGGER_CAPTURE marker; no UNHANDLED handler marker, FATAL, or Network Service restart occurred. No service stack can be recovered without a service fault.
- Final-state.json: plainComplete=15, plainEnabled=12, plainDisabled=3, activeDebuggerComplete=3, exploratoryDebuggerComplete=3, interruptedExcluded=6, networkRestartLogLines=0, ownedProcessesRemaining=0, werKeyExists=false, taskDumps=0, tempDumps=0. schtasks query confirms the one-off task is absent.
- Grant result was not collected in baseline-1/2/3 or disabled-1/2/3 because the initial CDP query was restricted to NetworkSandbox histograms; the table labels this explicitly. All subsequent complete runs collected successful grant result 0 and no-attempt result 7, never grant failure 5/6. This is consistent with the named-capability modify ACE captured by icacls. No unconditional grant-success v=1 log line is assumed.
- Recommendation remains: no verified real fix; preserve current sandbox-compatible deployment ACL provisioning provisionally, keep the narrow navigation retry, and obtain a genuinely cold first-utility fault/termination stack before selecting a code, environment, or ACL repair. The comparison matrix is complete even though the original fault remains unresolved.
