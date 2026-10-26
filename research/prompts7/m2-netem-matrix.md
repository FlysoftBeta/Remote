# Task m2-netem-matrix (timeout 45 min): latency under emulated networks with realistic content
Task dir C:\src\tmp\m3\netem-matrix\. You are the ONLY launcher now. You MAY edit tools/latency/ and tools/netem/ (remotekit). No Chromium/endpoint edits,
no builds. No commits. Background: research/m2/netem.md (relay, profiles, how to run, pipeline block) and research/m2/latency.md.
Problem: the marker page sends only ~0.17 Mbps, so bandwidth caps and the shaper queue never mattered. Add scenarios to tools/latency/remote-page.html
(or sibling pages) that keep the marker + input flip but generate realistic load: (S) continuous smooth page scrolling of a long text/image page,
(V) a playing 1080p video element (generate a local test clip with ffmpeg if available, else a canvas-rendered moving pattern at 30 fps full screen),
(G) a game-like full-screen canvas animation with high motion. Record the achieved send bitrate (endpoint/sender stats) per run.
Matrix (each 60 s, headless Edge viewer, --encoder=openh264 unless the vpl encoder is present and the endpoint default; record which):
The brief's "50 ms" is the RTT, so the ACCEPTANCE profiles are d25j5 (25 ms +-5 ms one-way): d25j5-c16, d25j5-c12, d25j5-c10, d25j5-c12-wifi
x scenarios S, V and G (run these first, 2 runs each if time allows). Stress only (after acceptance rows, 1 run, S only): d50j10-c12, d50j10-c12-wifi.
Then one headed-viewer repeat of d25j5-c12 S. Evaluate BOTH capture->display and input->flip against the targets (input->flip is the stricter one).
Report per run: capture->display and input->flip P50/P80/P95/P99, send bitrate, relay drops / max queue delay, freezes (frames >200 ms apart), and flag
PASS/FAIL against P80 < 100 ms and P99 < 200 ms (state which metric). Put the table + the pipeline configuration in NOTES.md, and a short
"where the time goes" per profile (network + queue vs encode vs viewer) using the existing stage stats.
