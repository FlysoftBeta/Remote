# Task clone+d: partial clone of chromium/src, then branding research
Write: C:\Users\Flysoft\Desktop\remotekit\research\cr-partial.md and C:\Users\Flysoft\Desktop\remotekit\research\d-branding.md
You MAY create C:\src\cr-partial.

## Part 1 (do this first, keep it fast, < 15 min)
Create a blob-less sparse partial clone of chromium/src at C:\src\cr-partial, for grepping by later tasks.
- Determine the current Chrome Stable version for Windows, e.g. from
  https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Windows&num=1 , and use its tag (like 142.0.7444.59).
- Suggested: `git -c core.longpaths=true clone --filter=blob:none --no-checkout --depth 1 --branch <tag> https://chromium.googlesource.com/chromium/src.git C:\src\cr-partial`
  (fallbacks if that fails: init + `git fetch --depth 1 --filter=blob:none origin refs/tags/<tag>`), then
  `git config core.longpaths true`, `git sparse-checkout init --cone`, `git sparse-checkout set <dirs>`, `git checkout`.
- Sparse dirs (tolerate missing ones): headless, ui/aura, ui/base/ime, ui/base/win, ui/events, ui/views/widget, ui/views/win,
  ui/views/controls/textfield, ui/compositor, ui/gl, ui/gfx/win, ui/display/win, ui/wm, components/viz, gpu/ipc,
  media/capture/mojom, content/browser/renderer_host, content/browser/devtools/protocol, content/public/browser,
  components/embedder_support, components/version_info, chrome/browser/headless, chrome/browser/ui/views/frame,
  chrome/app/theme, chrome/browser/ui/startup, chrome/common, build/config, third_party/blink/public/common/user_agent,
  sandbox/win/src, services/viz, ui/ozone/public, components/input, third_party/blink/renderer/core/frame.
- Write research/cr-partial.md: exact tag, commit hash, commands used, how to add sparse dirs, disk size, caveats.

## Part 2: Branding research -> research/d-branding.md
Goal: the web-exposed UA string, UA client hints (Sec-CH-UA brand list incl. GREASE, Sec-CH-UA-Full-Version-List,
navigator.userAgentData brands / getHighEntropyValues) must report "Google Chrome" exactly like official Chrome Stable
(not "HeadlessChrome", not "Chromium", not "PRTS"), while product branding (window titles, about page, resources) is "PRTS".
We run the new-headless chrome path (chrome.exe --headless) on Windows, built with is_chrome_branded=false.
Find the exact code points:
- components/embedder_support/user_agent_utils.cc (GetUserAgentBrandList / GenerateBrandVersionList, GREASE logic, the
  brand name source, GetProduct / GetFullUserAgent / reduced UA "Chrome/<major>.0.0.0", GetUserAgentMetadata).
- version_info::GetProductName and chrome branding: BRANDING files (chrome/app/theme/chromium/BRANDING,
  google_chrome/BRANDING), build flags (is_chrome_branded, branding_path_component / BRANDING_PATH, chrome_product_full_name).
- Where headless mode substitutes "HeadlessChrome" (headless/lib/browser, chrome/browser/headless, headless UA
  product handling, the --user-agent switch path), and how the Sec-CH-UA brand list behaves in headless.
- Read the prior art's branding patches: C:\Users\Flysoft\Desktop\chromium\patches\0029-*, 0030-*, 0031-*, 0032-*.
Deliver: the minimal patch design (files, functions, what to change, as a diff sketch), how to set product branding to PRTS
cheaply (custom BRANDING values vs. a new branding dir; consequences for exe name, user-data-dir, registry, installer),
plus a verification checklist (JS snippets + HTTP headers to check, e.g. via a local echo server).
Briefly list other headless-detectable surfaces you notice (not required to fix now).
