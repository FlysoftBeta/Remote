# Task m4-field-kit (timeout 40 min): one-command two-machine field test kit (no chain launch while another task measures)
Task dir C:\src\tmp\m4\field-kit\. Deliverable: tools/field-test/ (script + README) and the minimal viewer/control-plane changes it needs.
Another task is launching the chain right now: do NOT run control-plane/endpoint/chrome; syntax-check only (`node --check`, PowerShell parser).
Do not edit remotekit/endpoint or tools/latency (other tasks).
Setup to support: server machine (Windows, wired) runs the chain; viewer = a real Chrome 89+ (phone/tablet/PC, Wi-Fi), same subnet, no CDP access.
1. `tools/field-test/start-server.ps1` (one command): checks prerequisites (chrome.exe/prts_endpoint.exe dir param, default C:\src\chromium\src\out\rel if
   both binaries exist there, else out\dev), checks/prints the firewall rule for the control-plane port (add it if elevated, else print the netsh
   command), starts control-plane + endpoint with the m3-combined-equivalent settings (read tools/latency/run.mjs preset handling and replicate the
   endpoint args/field trials; extra knobs via -EndpointArgs), listens on the LAN, prints the viewer URL(s) for every IPv4 LAN address, and writes
   logs + collected reports to a timestamped folder under the -OutDir param.
2. Viewer measurement mode without CDP: the viewer page runs the marker / input->photon measurement by itself (read how tools/latency/run.mjs +
   measure.js + marker.js drive it today), e.g. `http://<ip>:<port>/?measure=60&scenario=S`: runs N seconds, shows live P50/P80/P99, then POSTs the
   report JSON (same percentile fields as run.mjs reports, plus navigator.userAgent, decoder implementation if readable, screen size, connection type if
   available) to the control-plane (`POST /field-report`), which saves it into the run folder. Must work in Chrome 89 (no top-level await, no `??=`/`||=`,
   no private class fields, no structuredClone, no Array.prototype.at; check each API you use exists in Chrome 89).
3. `tools/field-test/README.md`: steps (build, start, open URL on the device, what to tap, where the report lands, how to compare with loopback/netem
   numbers), troubleshooting (firewall, Wi-Fi client isolation, ICE host candidates must be the LAN IP, mDNS candidates, no TURN needed).
NOTES.md: files changed, what is verified (syntax only) vs unverified.
