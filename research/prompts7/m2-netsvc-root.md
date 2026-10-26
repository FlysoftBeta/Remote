# Task m2-netsvc-root (timeout 35 min): root cause of the Network Service crash at startup (cheap discriminators)
Task dir C:\src\tmp\m2\netsvc\. Another agent is building and running the PRTS chain at the same time: do NOT use kill.sh/be.sh/cp.sh/test.sh, do NOT
build, do NOT edit C:\src\chromium\src or the remotekit repo, do NOT use port 8300. Launch plain `C:\src\chromium\src\out\dev\chrome.exe --headless=new`
(no endpoint needed: the crash also happens without --prts-remote) with a FRESH user-data-dir under your task dir per run, `--enable-logging=stderr --v=1`
(or `--enable-logging --v=1` -> <profile>\chrome_debug.log), `about:blank` or https://example.com, ~15 s, then kill only YOUR process tree (by PID).
If the other agent's kill script terminates your run (chrome under C:\src\chromium is killed), just rerun.
Background: C:\src\tmp\m1\netsvc\NOTES.md (fails in the sandboxed utility bootstrap before LowerToken(); NetworkServiceInProcess2 avoids it; no dump);
C:\src\tmp\m2\runtime\NOTES.md (after granting S-1-15-2-1/S-1-15-2-2 RX on out\dev and C:\src\tmp\m1 there were 3/3 clean runs, but a warm rollback also
passed, so the ACL is not proven; note out\dev already had S-1-15-2-2). Crash signature: "Network service crashed" / restart in the log; the
NetworkSandboxState histogram (system_network_context_manager.cc).

Run each discriminator >= 3 times (fresh profile each), record crash yes/no per run in a table:
1. Baseline from your own shell. Then `--disable-features=NetworkServiceSandbox` (check the exact feature name at the pin). If the crash disappears -> the
   network (LPAC) sandbox setup is implicated.
2. Launch environment: Codex runs under a sandboxed/restricted token or job (windows sandbox "elevated"), which can break nested sandbox creation.
   Compare (a) your shell, (b) a launch outside your process tree via a one-off scheduled task (`schtasks /create /tr <cmd file> /sc once /st 00:00 /f`
   then `schtasks /run`, as the interactive user; delete the task afterwards), and record the parent job/token of each (e.g. whether chrome.exe is in a job:
   PowerShell + IsProcessInJob via Add-Type, and the integrity level / restricted SIDs of your shell token: `whoami /all`).
3. Profile location / ACLs: profile under C:\src\tmp (as the chain uses) vs under %LOCALAPPDATA%\Temp; look for the runtime ACL grant on <profile>\Network
   (and failures of it) in the v=1 log; check `icacls <profile>\Network` after a run.
4. Only if 1-3 do not settle it: capture the utility crash (WER LocalDumps for chrome.exe via HKCU\Software\Microsoft\Windows\Windows Error Reporting\
   LocalDumps\chrome.exe DumpType=2 DumpFolder=<task dir>, remove the key afterwards; or `--utility-startup-dialog` + cdb attach if available) and get
   the faulting stack with cdb (C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe if present; symbols are in out\dev *.pdb).
Deliverable: NOTES.md with the run table, the one-paragraph root cause (state confidence), and the recommended real fix (code, launch environment or
deployment ACL) plus whether the startup-navigation retry in chrome/browser/prts can be removed or should stay as a guard.
