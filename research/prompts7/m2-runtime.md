# Task m2-runtime (timeout 40 min): Network Service crash fix, latency re-measure, Widevine check (runtime on the existing build)
Task dir C:\src\tmp\m2\runtime\. You are the ONLY process launching chrome/endpoint now. You MAY edit endpoint/ (remotekit) launch arguments and rebuild
prts_endpoint (`be.sh`), and edit tools/latency/* (remotekit). Do NOT edit Chromium sources and do NOT build chrome. No commits.

1. Network Service startup crash (background: C:\src\tmp\m1\netsvc\NOTES.md; today chrome/browser/prts has a startup navigation retry as a stopgap;
   its log line tells you when it fires). Hypothesis H1: the LPAC network sandbox cannot read the component-build binaries in out\dev because the
   directory lacks the ACE for ALL RESTRICTED APPLICATION PACKAGES (S-1-15-2-2) / ALL APPLICATION PACKAGES (S-1-15-2-1) that the Chrome installer adds.
   Check with icacls; if missing, grant read+execute (inherit) on out\dev (sudo is available) and on the user-data-dir parent if relevant, then run the chain
   3 times: does the utility/network crash disappear and does the first navigation succeed without the retry firing? H2 (only if H1 fails):
   `--disable-features=...,NetworkServiceSandbox` merged into the existing single --disable-features list in endpoint/main.cc. Pick the fix that keeps the
   most isolation; implement it (ACL: add a small idempotent script scripts/grant-lpac-acl.ps1 in remotekit and note it for deployment; flag: endpoint/main.cc).
   Evidence: before/after chrome_debug.log lines.
2. Latency (tab mode; use the harness default page). `node tools/latency/run.mjs --no-mock --secs 60` via test.sh:
   capture->display P50/P95/P99 (marker row auto-detection was just added; confirm it locks). Then input-to-photon: if the harness lacks it, add it minimally
   (viewer sends a mouse-down or key on the input DataChannel at viewer time T; tools/latency/remote-page.html reacts by flipping a large block's colour;
   the viewer detects the flip in decoded frames via the existing canvas sampling; latency = detect - T; >= 40 samples, spaced >= 300 ms). Report P50/P95/P99.
3. Widevine (CDM bundled in out\dev, see design/07-widevine.md): in the REMOTE chrome, run `navigator.requestMediaKeySystemAccess('com.widevine.alpha',
   [{initDataTypes:['cenc'],videoCapabilities:[{contentType:'video/mp4; codecs="avc1.42E01E"'}]}])` (a small page served by the control-plane or a data: URL;
   print the result into the page title/body; read it from chrome_debug.log console lines or a viewer screenshot), then try one public Widevine test stream
   (e.g. the Shaka Player demo asset "Angel One (multicodec, multilingual, Widevine)" at https://shaka-player-demo.appspot.com/) and record whether the
   license request succeeds and video time advances (screenshot). Note chrome://components / CDM load log lines.
Deliverable: C:\src\tmp\m2\runtime\NOTES.md with a short results table (fix chosen + evidence; latency numbers with sample counts; Widevine result).
