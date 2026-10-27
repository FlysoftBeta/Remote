# Task netsvc-crash (timeout 40 min): root cause of the bootstrap Network Service crash in PRTS/headless chrome
Output: C:\src\tmp\m1\netsvc\NOTES.md (create in the first 5 minutes). READ-ONLY: do not edit or build in C:\src\chromium\src, and do NOT
launch C:\src\chromium\src\out\dev\chrome.exe (a build is running and would fail on locked DLLs). No commits.

Facts: every launch of our dev build (157.0.8103.15, component, dcheck_always_on) logs once at startup
"network_service_instance_impl.cc:652 Network service crashed or was terminated, restarting service" - with and without --prts-remote,
--headless=new on Windows 11 24H2 (26100). With --enable-features=NetworkServiceInProcess2 there is no crash. A P0 workaround retries the
first navigation (C:\src\tmp\m1\debug-frames\NOTES.md). Crashpad dumps may exist under the profiles C:\src\tmp\m1\prof-*\Crashpad\reports
and C:\src\tmp\m1\input-validate\chrome-profile\Crashpad, C:\src\tmp\m1\debug-frames\ (search for *.dmp). Debugging Tools for Windows
(cdb.exe under C:\Program Files (x86)\Windows Kits\10\Debuggers\x64) and symbols in out\dev (*.pdb) are available read-only
(use `.sympath C:\src\chromium\src\out\dev` and `-z <dmp>`; `!analyze -v; ~*kb`).
Deliver: the crashing stack and exception (DCHECK message if any), the responsible code at the pin with file:line, why it happens in our
setup (headless? sandbox? DCHECK-only? process launch environment?), and the smallest correct fix or switch (and whether it affects
non-dcheck rel builds). If there are no dumps, say how to obtain one with the least effort (e.g. --enable-crash-reporter flags,
--noerrdialogs, `--wait-for-debugger-children=utility`), and still analyze from the source what kills the service early.
Final message: 5-8 lines.
