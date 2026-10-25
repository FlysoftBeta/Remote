# Common context (read first)
Project "remotekit": a cloud-browser framework on Windows. Chain: custom Chromium (product branding "PRTS")
-> browser-endpoint daemon (encodes frames, WebRTC to the user) -> control-plane (Node.js sample).
Full brief: C:\Users\Flysoft\Desktop\remotekit\design\00-brief.md (read it, it is short).
Key constraints: must reuse the Chromium *new headless* code path on Windows (Aura + a DWMWA_CLOAKED stand-in HWND
stays); Ozone headless is ruled out on Windows; patches must live at the Aura level and be as small as possible;
the browser UI chrome (tab strip, omnibox) must be visible in the stream; GPU required, software fallback ok;
input: mouse/keyboard/touch without synthesized OS input; web-exposed branding must be "Google Chrome";
transport floor: WebRTC + H.264 + Chrome 89+ receiver; target E2E latency P80 < 100 ms, P99 < 200 ms at 1080p,
10-16 Mbps, 50 ms RTT with jitter; encoder Intel oneVPL (dev box iGPU UHD 630). Server OS: Win10 24H2-level APIs.

Prior art (READ-ONLY, never modify): C:\Users\Flysoft\Desktop\chromium -- an earlier project by the same user
(patches/ series against PIN, prtsd/ Rust daemon, server/ Node, docs/prts/). Reuse its findings where relevant.
Its docs/prts/ folder and patches/ are the fastest way to learn what was already discovered.

Chromium sources: use the recent stable milestone and record the exact version + commit you looked at.
Ways to read source: (1) the local partial clone at C:\src\cr-partial if it exists (git grep / git show / read files
there; add sparse dirs with `git -C C:\src\cr-partial sparse-checkout add <dir>` if needed; do NOT run gclient or a full
checkout); (2) gitiles: https://chromium.googlesource.com/chromium/src/+/<ref>/<path>?format=TEXT (base64);
(3) source.chromium.org / web search.

Rules:
- Output: ONE markdown report at the path given in the task. Dense, concrete, English. Target 250-600 lines.
  Use concrete file paths, class/function names, and short code excerpts only when load-bearing.
  Separate verified facts (you read the code) from inferences. End with a "Recommendation" section and an
  "Open questions / spikes to verify" section.
- Bounded effort: you have a hard time limit (~40 min). Create the report file early and extend it as you go so that
  partial results survive a timeout.
- Do not modify anything outside the report path (and C:\src\cr-partial only if your task says so).
- Your final message: a 5-10 line summary of the key findings plus the report path.
