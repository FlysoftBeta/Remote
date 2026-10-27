# Task widevine (timeout 40 min): obtain and stage the Windows x64 Widevine CDM for bundling (user-requested; ignore licensing)
Output dir: C:\src\tmp\m1\widevine\  (staging only; do NOT place anything into C:\src\chromium\src yourself)

The user explicitly asked to bundle Widevine like Brave does (Brave fetches the CDM from Google's component updater).
1. Read at the pin (read-only): third_party/widevine/cdm/widevine.gni, third_party/widevine/cdm/BUILD.gn, third_party/widevine/cdm/win/
   (expected layout, e.g. win/x64/{widevinecdm.dll, manifest.json, ...} and any README/.gitignore), chrome/browser/component_updater/
   widevine_cdm_component_installer.cc (component id oimompecagnajdejgnnjijobebaeigek, public key hash), media/cdm/... for the version
   checks, and how `library_widevine_cdm_available`, `enable_widevine`, `bundle_widevine_cdm`, `enable_library_cdms`, `enable_cdm_host_verification`
   (and any `enable_widevine_cdm_component` / `ignore_missing_widevine_signing_cert`) are computed for a non-Chrome-branded Windows build.
   Also read Brave's approach from public sources if useful (brave-core widevine component / build flags) - web search allowed, quote nothing long.
2. Download the current Windows x64 Widevine CDM from Google's official component update service only: an Omaha/update protocol 3.x
   request to https://update.googleapis.com/service/update2/json (or the xml endpoint) for appid oimompecagnajdejgnnjijobebaeigek with
   os win / arch x64 / a Chrome 157 prodversion, then the returned https://*.gvt1.com or dl.google.com URL (.crx3). Verify the SHA-256
   given in the response. Unpack the CRX3 (strip the header; it is a zip) into C:\src\tmp\m1\widevine\cdm\ and list its files and the
   manifest.json version. Check the CDM interface version in manifest.json against what the pin accepts
   (media/cdm/supported_cdm_versions.h / cdm module version) - report mismatch risks.
3. Write C:\src\tmp\m1\widevine\STEPS.md: exact copy commands into third_party/widevine/cdm/win/x64/ (or whatever the pin expects), the GN
   args to add (dev and rel) with the reason for each, whether a placeholder file or .gitignore/BUILD change is needed (if a BUILD.gn edit is
   required, provide it as an a/b diff verified with git apply --check), how bundling places the CDM next to chrome.exe in out\dev
   (WidevineCdm/_platform_specific/win_x64/...), and how to verify (chrome://components does not list bundled CDMs; use
   navigator.requestMediaKeySystemAccess('com.widevine.alpha', [...]) in a page; chrome://media-internals). Note VMP signing (no Google VMP
   signature for our chrome.exe -> some license servers reject) and whether `enable_cdm_host_verification` must be false.
Final message: 5-8 lines.
