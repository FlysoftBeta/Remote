# Task m2-touch-l10n (timeout 40 min): DRAFT touch (patch 09 + touch injection check) and zh-CN PRTS strings
Task dir C:\src\tmp\m2\touch-l10n\. DRAFT ONLY: another agent is building and running the chain. Do NOT launch chrome/endpoint, do NOT build, do NOT edit
C:\src\chromium\src or the remotekit repo in place. Produce diffs as in other draft tasks: chromium.diff against src `prts` HEAD (a\ and b\ copies under the
task dir, verified with `git -C C:\src\chromium\src apply --check`) and, if needed, remotekit.diff (viewer/, endpoint/) verified with
`git -C C:\Users\Flysoft\Desktop\remotekit apply --check`. Keep them small so they apply with `git apply -3` after other changes to the same files.

1. Touch (design/02 patch 09 `prts-web-prefs`, design/01 D10, research/c1-input-layer.md touch section): pages in the remote browser must see a touch device
   when the viewer has touch: navigator.maxTouchPoints > 0, (pointer: coarse)/(any-pointer: coarse), (hover: none) when the viewer is touch-only, ontouchstart
   and TouchEvent support. Implement prts_content_browser_client_part (OverrideWebPreferences) + the 1-3 line registration in
   chrome/browser/chrome_content_browser_client.cc, driven by the viewer caps (hello caps.touch) that the endpoint passes over Mojo (add the minimal Mojo
   field/method; check what chrome/browser/prts already receives). Also review chrome/browser/prts/prts_input.cc touch injection (ui::TouchEvent per
   pointer id, pointer details, radius, cancel, root coordinate mapping) and the viewer side (viewer/input.js: pointer events with pointerType touch ->
   binary ptype=1 per design/06; touch-action:none on the video; no synthetic mouse duplicates) so that tap, scroll (pan) and pinch-zoom work in pages:
   list concrete defects and fix them in the diffs. State whether Aura gesture recognition produces scroll/pinch from our injected touches in headless
   (cite the code path).
2. zh-CN branding: patch 12 changed only English GRIT strings, so zh-CN UI still says "Chromium" (e.g. the profile menu text). Find the user-visible
   product-name strings in the zh-CN (and zh-TW) .xtb files for chromium_strings.grd / settings_chromium_strings.grdp (plus any other branded grd the UI
   shows: menus, about, profile, new tab). First explain what actually happens today (translation ids are fingerprints of the English message text: did
   patch 12 change the ids so translations fall back, or are the old translations still used?), then choose the smallest correct fix and list the strings.
NOTES.md: defects found, fixes, citations, a manual test plan for the integrator (CDP touch emulation steps on the viewer), risks.
