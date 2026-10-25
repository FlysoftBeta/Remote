# Common context (read first, keep it short)
Project "remotekit": cloud browser on Windows. Chain: custom Chromium (product "PRTS", Chrome `--headless=new`
path on Windows: Aura + Views + a DWMWA_CLOAK'ed stand-in HWND, see ui/views/win/hwnd_message_handler_headless.cc)
-> browser-endpoint daemon (H.264 encode, WebRTC to user) -> control-plane. Browser UI chrome (tab strip, omnibox,
popups) must be streamed too. No synthesized OS input. Patches must be minimal and at the Aura/Views/viz level.

Chromium source: pinned Stable 157.0.8103.15, commit 1aa07b638799e59656559ae6c6649d4b197f555e, local sparse partial
clone at C:\src\cr-partial (read-only for you; use `rg` on the worktree, `git -C C:\src\cr-partial show HEAD:<path>`
for files outside the sparse set; do NOT run gclient/checkout/unrestricted `git grep HEAD`). If a needed dir is
missing you MAY run `git -C C:\src\cr-partial sparse-checkout add <dir>` for at most 3 narrow dirs.
Gitiles fallback: https://chromium.googlesource.com/chromium/src/+/1aa07b638799e59656559ae6c6649d4b197f555e/<path>?format=TEXT
Prior art (READ-ONLY): C:\Users\Flysoft\Desktop\chromium (patches/ against M158, prtsd/ Rust daemon, docs/prts/).
Summary of it: C:\Users\Flysoft\Desktop\remotekit\research\a-prior-art.md (skim only the sections named in the task).

Rules:
- Answer ONLY the numbered questions of your task. No general survey, no background essays.
- Every claim: file path (relative to src/) + class/function name, marked [V] verified by reading code at the pin,
  or [I] inference. Short excerpts (<= 10 lines) only when load-bearing.
- Create the output file within your first 5 minutes with headings for every question and fill it incrementally
  (partial results must survive a hard timeout). Length: 120-300 lines.
- Do not modify anything except your output file.
- Final message: 5-8 lines summary.
