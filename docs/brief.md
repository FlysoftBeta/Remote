# Project brief (source of truth, from the user)

Cloud browser framework (may grow into a cloud desktop later).

Communication: `Custom Chromium -> browser-endpoint -> control-plane`. In the repository these are `rb`, `re` and `control-plane`
(see [overview](overview.md)). The user connects to the endpoint directly over WebRTC. The control-plane is, for now, a Node.js
sample version used for debugging and acceptance testing.

## Workstream A: Browser modification
Goal: control the whole browser with changes that are as small and non-intrusive as possible.

Known findings (from the user):
- In Chromium, Win and Ozone sit side by side at the same abstraction level. Ozone has a headless platform, but it does not work on
  Windows, so it is ruled out. Win is coupled to Aura at the same abstraction level, so our patches must work at the Aura level.
- In new headless, according to the Chromium docs, Chrome creates a DWMWA_CLOAKED window to get a stand-in HWND. We must follow the
  new-headless path for best compatibility, and that HWND has to stay.

Proposed hook points:
- Reuse the new-headless code path.
- The browser UI chrome must be shown too, so export a shared texture out through Aura. If needed, the endpoint daemon can control the
  presentation pace.
- Input must work well with touch: mouse, keyboard and touch, plus an automatic on-screen keyboard when touching. Avoid synthesized OS
  input (multi-tenant focus confusion). Possibly do our own focus management, and detect focused editable fields so the remote keyboard
  can pop up. OPEN QUESTION (at the time): which layer to modify. Resolved in [decisions](decisions.md) D7-D9.
Other:
- Even on the headless path, the branding exposed to web pages (client hints, UA, ...) must be "Google Chrome" (not the product
  branding and not HeadlessChrome). The product branding is "PRTS".
- No audio for now.
- GPU acceleration is required; a software fallback is allowed.

## Workstream B: Audio/video transport
- Hard floor: WebRTC + H.264 + Chrome 89+ on the client (a better path is OK if available, but this combination is the minimum).
- Latency targets exclude the client-side presentation overhead (only decoding counts), per the user on 2026-10-26; reducing
  presentation latency is best effort.
- Network: 50 ms latency = RTT (confirmed by the user; i.e. about 25 ms one-way), with jitter, 10 to 16 Mbps available (depends on
  concurrent users), 1080p target. Internal network; one peer is on Wi-Fi, the other is wired, same subnet.
- Goal: end-to-end latency P80 < 100 ms, P99 < 200 ms.
- Ideally drive the frame pace together with Chromium.
- Tune the encoder; the target machine can use oneVPL (Intel; the dev box has a UHD 630 iGPU).
- Test scenarios: page scrolling, video playback, real-time games.
- Needs many controlled experiments.

## Workstream C: Isolation (LOW PRIORITY, do last)
Multi-tenant isolation with built-in Windows primitives (Job Objects, AppContainer, LPAC, ...). Server: Win10 24H2-level (avoid
Win11-only APIs). User-chosen target (2026-10-26): Job Object + JOB_OBJECT_UILIMIT_* + LPAC, managed by the endpoint. Small Chromium
patches are allowed; give up and fall back to a job plus a normal AppContainer (not a per-tenant account: too heavy) if it needs more
than about 1k LOC or is likely to cause merge conflicts in future versions. See [isolation](isolation.md). Investigate and implement
only after the full chain runs and the experience is usable.

## Process
- Research with web search, Chromium docs, and a partial clone of key Chromium sources.
- Write the design (initial ideas, execution order, plan) under `docs/`.
- Then implement. A shallow clone of Chromium can go to `C:/src`. We probably need our own patch-maintenance infrastructure.
- Priority: a working end-to-end chain with a usable experience, over tests and hard gates. Keep tests minimal; they are for
  debugging only. Never run full test suites.

## Environment notes
- Dev box: Windows 11 IoT LTSC 24H2 (26100), 32 logical CPUs, Intel UHD 630, 768 GB free on C:.
- An EARLIER related project by the user (patches, endpoint code, server, scripts, PIN, args.gn, cr.cmd) exists as prior art. It may be
  referenced ONLY PARTIALLY: it contains decisions and practices the user disagrees with. Do not copy it wholesale or inherit its
  architecture by default; evaluate each reused piece against our own design.
- Never use `C:\src.old` (junk).
- All temporary files go to ONE place: `C:\src\tmp` (subfolders per task).
- Sync every commit to GitHub (`origin` = https://github.com/FlysoftBeta/Remote.git), WIP included: commit and
  `git push origin main` often. Work directly on `main`; avoid branches (they can break the build infra during fast iteration).

## Delegation rules (apply to every agent)
- The main agent manages stages. Each stage has ONE orchestrator subagent (context isolation) that engages deeply: it reasons, reviews
  and verifies every delegated output, spot-checks key claims, makes the decisions and writes the design/code direction. Hand-off
  between stages goes through the status notes and the docs.
- Codex is NOT for autonomous or open-ended work. Give it only tightly bounded tasks: one specific question or deliverable, a clear
  output file, explicit scope limits and a timeout. Tool-heavy or context-heavy grunt work goes to Codex as BOUNDED tasks; runs may be
  parallel but sustainable (about 2 to 3 concurrent at most). If Codex is exhausted, fall back to Claude subagents (Sonnet).
- Claude agents orchestrate. Do not read large amounts of source code yourself. Give every task a timeout.
