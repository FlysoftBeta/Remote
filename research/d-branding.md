# PRTS product branding and Chrome web identity

Research date: 2026-10-25 (Asia/Singapore).
Baseline: Windows Stable **157.0.8103.15**, milestone 157.
Exact source commit: **1aa07b638799e59656559ae6c6649d4b197f555e**.
Scope: Windows `chrome.exe --headless`, `is_chrome_branded=false`.
The brief in `design/00-brief.md` was read; prior art was accessed read-only.

## Evidence and result

**Verified** below means code or prior-art text read during this task.
**Design/inference** means a proposed edit or behavior still requiring a build/runtime check.
No Chromium build, official-Chrome comparison, or new runtime identity test was performed.
Sources were read through Gitiles while sparse checkout hydrated blobs, then key findings were checked against the local pinned commit.
Prior-art runtime results are attributed separately and are not measurements of this Stable revision.

**Result:** the minimal web identity patch changes two sites in one file:
`components/embedder_support/user_agent_utils.cc`.
Supply `Google Chrome` to the existing brand generator and suppress the Windows headless UA prefix.
Product naming remains independent: BRANDING plus GRIT strings must say PRTS.
Do not change the shared UA product helpers to return PRTS.

**Requirement clarification:** official Chrome includes a `Chromium` base brand in UA-CH.
Therefore matching official Chrome means retaining that entry, plus Google Chrome and GREASE.
It does not mean replacing every brand entry with Google Chrome.
The unwanted condition is a Chromium-only browser identity, not the presence of the Chromium base brand.

The pinned release came from [ChromiumDash Windows Stable](https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Windows&num=1).
`git rev-parse HEAD` matched the release's `hashes.chromium`.
Checkout setup and limitations are recorded in `research/cr-partial.md`.

## Verified source map

All paths in the table are relative to Chromium `src/`.

| Path | Entry point / role |
|---|---|
| `components/embedder_support/user_agent_utils.cc` | Shared UA, UA-CH, platform values, GREASE |
| `components/embedder_support/user_agent_utils.h` | Public UA helper API |
| `components/version_info/version_info_with_user_agent.cc` | Chrome product token, full and reduced versions |
| `components/version_info/version_info.h` | Facade including base version-info APIs |
| `base/version_info/version_info.h` | `version_info::GetProductName`, version constants |
| `base/version_info/BUILD.gn` | Generate version-info header using BRANDING and VERSION |
| `base/version_info/version_info_values.h.version` | `PRODUCT_NAME` comes from `PRODUCT_FULLNAME` |
| `build/config/chrome_build.gni` | Branding GN arguments and selected paths |
| `build/BUILD.gn` | Generated `CHROMIUM_BRANDING`, `GOOGLE_CHROME_BRANDING` flags |
| `build/util/branding.gni` | Exposes `chrome_product_full_name` and related GN values |
| `chrome/browser/chrome_content_browser_client.cc` | Chrome embedder's product, UA and metadata callbacks |
| `headless/lib/browser/headless_browser_impl.cc` | Standalone headless product and metadata override |
| `headless/lib/browser/headless_content_browser_client.cc` | Standalone headless embedder callbacks |
| `chrome/browser/headless/headless_mode_util.cc` | New Chrome headless detection |
| `content/browser/client_hints/client_hints.cc` | Browser-side outgoing UA-CH header construction |
| `third_party/blink/common/user_agent/user_agent_metadata.cc` | Structured-header serialization of brand lists |
| `third_party/blink/renderer/core/frame/navigator_ua.cc` | Metadata to JS `NavigatorUAData` |
| `third_party/blink/renderer/core/frame/navigator_ua_data.cc` | Low/high entropy JS getters and policy gate |
| `chrome/browser/ui/webui/version/version_ui.cc` | `chrome://version` application label and displayed UA |
| `chrome/browser/ui/window_metadata/window_metadata_controller.cc` | Formats native browser window title |
| `chrome/browser/ui/views/frame/browser_view.cc` | Delegates title construction and updates widget title |
| `chrome/common/chrome_paths_win.cc` | Default Windows user-data location |
| `chrome/install_static/chromium_install_modes.h` | Unbranded install, registry and shell identity constants |
| `chrome/install_static/install_util.cc` | Composes install and registry paths |

Primary source anchors:
[UA helpers](https://chromium.googlesource.com/chromium/src/+/1aa07b638799e59656559ae6c6649d4b197f555e/components/embedder_support/user_agent_utils.cc),
[Chrome browser client](https://chromium.googlesource.com/chromium/src/+/1aa07b638799e59656559ae6c6649d4b197f555e/chrome/browser/chrome_content_browser_client.cc),
[base version info](https://chromium.googlesource.com/chromium/src/+/1aa07b638799e59656559ae6c6649d4b197f555e/base/version_info/version_info.h).

## Legacy UA string: actual current functions

**Verified:** the shared API at this revision uses `GetProductAndVersion()` and `GetUserAgent()`.
The requested conceptual names `GetProduct` / `GetFullUserAgent` are not the shared helper names here.
`GetProduct()` exists on `ChromeContentBrowserClient` and the standalone headless browser client.
The full-product helper is `version_info::GetProductNameAndVersionForUserAgent()`.
The reduced-product helper is `version_info::GetProductNameAndVersionForReducedUserAgent()`.

`components/version_info/version_info_with_user_agent.cc` constructs:

```cpp
// Full product:
base::StrCat({"Chrome/", GetVersionNumber()});
// Reduced product:
base::StrCat({"Chrome/", GetMajorVersionNumber(), ".0.0.0"});
```

Neither helper reads `GetProductName()` or `PRODUCT_FULLNAME`.
Thus PRTS product branding does not require a legacy UA product patch.
`GetProductAndVersion()` selects the reduced helper using `blink::features::kReduceUserAgentMinorVersion`.
`ShouldSendUserAgentUnifiedPlatform()` uses the same feature to select the frozen platform template.
On Windows that frozen platform text is `Windows NT 10.0; Win64; x64`.
Keep this normal reduction behavior and its existing configuration controls.

`GetUserAgentInternal()` gets the product, then checks `embedder_support::kHeadless`.
If the switch is present, it inserts `Headless` at the beginning of the product.
Consequently new Chrome headless currently emits `HeadlessChrome/157.0.0.0` under reduction.
It then builds either the unified-platform or ordinary OS/CPU UA.
`BuildUserAgentFromOSAndProduct()` supplies the Mozilla/WebKit/Safari compatibility wrapper.

Expected patched default Windows reduced UA, derived from those functions:

```text
Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/157.0.0.0 Safari/537.36
```

This is a source-derived expectation, not a measured request.
If reduction is disabled, the Chrome product uses the complete `157.0.8103.15` version.
`ChromeContentBrowserClient::GetProduct()` returns the full Chrome product independently of the reduced UA.
`content/browser/devtools/protocol/browser_handler.cc`, `BrowserHandler::GetVersion`, reads that callback
for its `product` and the browser client's `GetUserAgent()` for its `userAgent` field.
No product-branding patch is needed there for new Chrome headless.

## UA-CH generation, GREASE and versions

**Verified:** `GetUserAgentBrandList()` parses the major version to an integer seed.
It starts with an empty optional browser brand.
Only when `!BUILDFLAG(CHROMIUM_BRANDING)` does upstream assign `version_info::GetProductName()`.
An unbranded Chromium build therefore omits the browser-specific third brand altogether.
Changing Chromium BRANDING to PRTS while keeping `CHROMIUM_BRANDING=true` does not add PRTS by itself.
Changing the flag to false would instead expose PRTS through `GetProductName()` and affect unrelated gates.
Neither is the intended solution.

`GetUserAgentBrandMajorVersionListInternal()` requests major versions.
`GetUserAgentBrandFullVersionListInternal()` requests complete versions.
Both call `GetUserAgentBrandList()` and then `GenerateBrandVersionList()`.
The latter starts with GREASE and `Chromium`, appends the optional browser brand,
then appends an optional additional brand/version supplied by a caller.
`GetUserAgentMetadata()` passes `std::nullopt` for that additional brand.
Do not add Google Chrome as an additional fourth brand: use the normal optional browser brand.

`GetGreasedUserAgentBrandVersion()` uses the major seed modulo an 11-character punctuation array.
The characters are space, `(`, `:`, `-`, `.`, `/`, `)`, `;`, `=`, `?`, `_`.
It forms `Not` + character[seed] + `A` + character[seed+1] + `Brand`.
The GREASE version is selected from `8`, `99`, `24` using seed modulo 3.
For full versions it adds `.0.0.0` to the selected GREASE version.
`ShuffleBrandList()` places input entry i at destination `order[i]`.
For three entries `GetRandomOrder()` chooses one of six fixed permutations using seed modulo 6.
Keep all of this code unchanged.

For milestone 157, source-derived values are:

| Item | Calculation / result |
|---|---|
| Punctuation indices | 157 % 11 = 3; 158 % 11 = 4 |
| GREASE brand | `Not-A.Brand` |
| GREASE major / full | `99` / `99.0.0.0` |
| Three-entry permutation | 157 % 6 = 1; destinations `{0, 2, 1}` |
| Final order | GREASE, Google Chrome, Chromium |
| Real brand major versions | `157` for both Google Chrome and Chromium |
| Real brand full versions | `157.0.8103.15` for both |

Expected serialized values after patching, assuming ordinary default metadata:

```http
Sec-CH-UA: "Not-A.Brand";v="99", "Google Chrome";v="157", "Chromium";v="157"
Sec-CH-UA-Full-Version-List: "Not-A.Brand";v="99.0.0.0", "Google Chrome";v="157.0.8103.15", "Chromium";v="157.0.8103.15"
Sec-CH-UA-Mobile: ?0
Sec-CH-UA-Platform: "Windows"
```

The Full-Version-List header is not an unconditional first-navigation header.
Its presence depends on client-hint eligibility, opt-in and policy.
Do not hardcode these strings: a milestone upgrade changes GREASE and order.
Do not reorder, sort, remove Chromium, or collapse the list to one Chrome entry.

## Metadata pipeline and override pitfalls

**Verified:** `GetUserAgentMetadata(bool only_low_entropy_ch)` populates:

- Low entropy: `brand_version_list`, `mobile`, `platform`.
- High entropy: `brand_full_version_list`, `full_version`, architecture, model,
  form factors, bitness, WoW64 and platform version.

Windows desktop mobile is false; model is empty; default form factor is Desktop.
Windows architecture/bitness come from `base::win::OSInfo`, not product branding.
On this x64 Intel target the ordinary expected values are x86 / 64 / false for architecture / bitness / WoW64.
Platform version uses `GetWindowsPlatformVersion()` -> `GetUniversalApiContractVersion()`.
It reads Windows' UniversalApiContract registry value, with a highest-known fallback of 19.
Do not replace that with the Windows marketing release or hardcode the prior machine's `19.0.0`.

`GetUserAgentFromCommandLine()` accepts `--user-agent` only if `net::HttpUtil::IsValidHeaderValue()` passes.
`GetUserAgent()` returns a valid override before calling `GetUserAgentInternal()`.
However `GetUserAgentMetadata()` detects the override and returns early:
with `kUACHOverrideBlank` enabled it returns empty metadata; otherwise only the low-entropy defaults.
It never reaches ordinary full-version/architecture population in that override path.
Therefore a launch-time UA string override does not satisfy complete official-Chrome identity.
Do not use `--user-agent` as the production solution.

`content/browser/client_hints/client_hints.cc` serializes the metadata into request headers.
It can disable hints when WebContents or DevTools supplies a UA override without metadata.
The normal path obtains `delegate->GetUserAgentMetadata()` and serializes major/full lists.
`third_party/blink/common/user_agent/user_agent_metadata.cc` uses structured-header list serialization.
There is no need to intercept raw `Sec-CH-UA` header text.

`NavigatorUA::userAgentData()` in `navigator_ua.cc` copies both metadata lists and the other fields.
`NavigatorUAData::brands()` returns the stored major list.
`getHighEntropyValues()` always includes low-entropy brands/mobile/platform,
and includes requested high-entropy fields when `AllowedToCollectHighEntropyValues()` permits them.
That permission check uses `kClientHintUAHighEntropyValues`.
`toJSON()` exposes brands, mobile and platform.
The design changes the common metadata source, avoiding independent JS and HTTP substitutions.
Worker JS consistency must still be measured in all worker types after rebasing/building.

Sources:
[client hints](https://chromium.googlesource.com/chromium/src/+/1aa07b638799e59656559ae6c6649d4b197f555e/content/browser/client_hints/client_hints.cc),
[JS UA data](https://chromium.googlesource.com/chromium/src/+/1aa07b638799e59656559ae6c6649d4b197f555e/third_party/blink/renderer/core/frame/navigator_ua_data.cc).

## New headless versus standalone headless

**Verified:** `chrome/browser/headless/headless_mode_util.cc`, `headless::IsHeadlessMode()`,
checks `chrome/browser/headless/headless_mode_switches.h`'s `kHeadless` switch.
The Chrome embedder continues to use `ChromeContentBrowserClient`:
`GetUserAgent()` -> `embedder_support::GetUserAgent()`;
`GetUserAgentMetadata()` -> `embedder_support::GetUserAgentMetadata()`.
There is no HeadlessChrome substitution in that Chrome metadata callback.
Headless changes the legacy UA through `GetUserAgentInternal()`; it does not independently
rename Chrome's UA-CH browser brand to HeadlessChrome on this path.
Unpatched unbranded new headless has a headless legacy UA but a Chromium + GREASE brand list.

**Verified:** `headless/lib/browser/headless_browser_impl.cc` is different:
`kHeadlessProductName` is `HeadlessChrome`.
`HeadlessBrowser::GetProductNameAndVersion()` returns it with `PRODUCT_VERSION`.
`HeadlessBrowser::Options::Options()` uses that product to build its default UA.
`HeadlessBrowser::GetUserAgentMetadata()` calls the shared helper then regenerates both brand lists
with the HeadlessChrome constant, unless shared metadata was entirely blank.
`HeadlessContentBrowserClient` delegates product/UA/metadata to these headless APIs/options.
Patching only the shared optional browser brand would not remove that standalone override.
This report's minimal patch intentionally targets the requested Chrome embedder.
Do not switch to headless_shell or change Aura/HWND behavior to implement naming.

[Standalone headless implementation](https://chromium.googlesource.com/chromium/src/+/1aa07b638799e59656559ae6c6649d4b197f555e/headless/lib/browser/headless_browser_impl.cc).

## Minimal patch design (not applied)

This is a Windows PRTS fork policy, matching prior art's scope.
If the source tree later builds multiple products, introduce an explicit product build flag
and guard these sites with that flag instead of applying the policy to every Windows embedder.
For the present dedicated fork, the smallest rebased sketch is:

```diff
--- a/components/embedder_support/user_agent_utils.cc
+++ b/components/embedder_support/user_agent_utils.cc
@@ GetUserAgentBrandList(...)
   std::optional<std::string> brand;
-#if !BUILDFLAG(CHROMIUM_BRANDING)
+#if BUILDFLAG(IS_WIN)
+  brand = "Google Chrome";
+#elif !BUILDFLAG(CHROMIUM_BRANDING)
   brand = version_info::GetProductName();
 #endif
@@ GetUserAgentInternal()
   std::string product = GetProductAndVersion();
+#if !BUILDFLAG(IS_WIN)
   if (base::CommandLine::ForCurrentProcess()->HasSwitch(kHeadless)) {
     product.insert(0, "Headless");
   }
+#endif
```

The preprocessor guard matters: prior patch 0031 replaced a constant-false runtime condition
because Chromium diagnosed unreachable code on Windows.
No edits are needed in `GetProductAndVersion`, `GenerateBrandVersionList`, metadata serialization,
Blink JS getters, request-header construction, or Chrome browser client callbacks.
The patch also makes headed Windows PRTS expose the same Chrome web identity.
That consistency is desirable for this product; scope it with a product flag if requirements change.

**Existing test implications (verified):**
`chrome/browser/headless/test/headless_mode_browsertest.cc:328`,
`HeadlessModeUserAgentBrowserTest.UserAgentHasHeadless`, currently requires HeadlessChrome.
Update its Windows expectation with this product policy, retaining upstream behavior on other platforms.
`components/embedder_support/user_agent_utils_unittest.cc:302`, `UserAgentUtilsTest.UserAgentMetadata`,
expects `GetProductName()` as the browser brand; after PRTS naming that would incorrectly expect PRTS.
Adjust its Windows expected browser brand to Google Chrome in major/full and low-entropy checks.
Generator tests with explicit brand inputs should retain their existing semantics.
Run these narrow existing checks if a test build is available; no full suite is warranted for the naming patch.

## Product branding: cheapest route and exact consumers

**Verified:** `base/version_info/version_info.h` defines `GetProductName()` returning `PRODUCT_NAME`.
`base/version_info/version_info_values.h.version` maps that macro from `@PRODUCT_FULLNAME@`.
`base/version_info/BUILD.gn` generates the header using `chrome/VERSION`, `branding_file_path`, LASTCHANGE.
`components/version_info/version_info.h` forwards to this base API; do not search for an obsolete .cc implementation.

`build/config/chrome_build.gni` defines `is_chrome_branded=false` by default.
`build/BUILD.gn` turns that into `CHROMIUM_BRANDING=true`, `GOOGLE_CHROME_BRANDING=false`.
`is_official_build` is a separate concept; do not enable Google branding to fix UA-CH.
This revision also has `enable_src_internal` and `is_internal_chrome_branded`.
The normal unbranded path selects `branding_path_component="chromium"`, `branding_path_product="chromium"`.
The file argument is separately overridable:
`branding_file_path="//chrome/app/theme/$branding_path_component/BRANDING"`.
`build/util/branding.gni` parses it into `chrome_product_full_name`, short name and installer names.

**Verified:** `chrome/app/theme/chromium/BRANDING` contains Chromium product/installer names,
The Chromium Authors company/copyright, and Mac identity values.
Cheap product patch:

```diff
--- a/chrome/app/theme/chromium/BRANDING
+++ b/chrome/app/theme/chromium/BRANDING
@@
-PRODUCT_FULLNAME=Chromium
-PRODUCT_SHORTNAME=Chromium
-PRODUCT_INSTALLER_FULLNAME=Chromium Installer
-PRODUCT_INSTALLER_SHORTNAME=Chromium Installer
+PRODUCT_FULLNAME=PRTS
+PRODUCT_SHORTNAME=PRTS
+PRODUCT_INSTALLER_FULLNAME=PRTS Installer
+PRODUCT_INSTALLER_SHORTNAME=PRTS Installer
```

Keep original company/copyright attribution; do not copy patch 0029's initially changed author strings.
Leave Mac values alone for this Windows-only work.
Alternative with less upstream-file churn: create a small custom BRANDING file and set `branding_file_path`
to it while retaining both existing `branding_path_component` and `branding_path_product` values.
This is a design option supported by the verified independent file argument; not built here.
It avoids needing a complete parallel logo/resource tree just to change names.

A new branding directory selected through `branding_path_component="prts"` is more expensive:
`chrome/BUILD.gn` and theme GRIT reference icons, tiles and other assets under that component.
`branding_path_product` separately selects `chrome/app/${branding_path_product}_strings.grd`.
Do not set it to prts without supplying that GRD and its includes/output conventions.
The obsolete-looking name `BRANDING_PATH` should not be assumed to be a current GN argument;
the verified knobs at this tag are `branding_file_path` and the two `branding_path_*` arguments.

**Access limitation:** at this tag `chrome/app/theme/google_chrome` is a gitlink,
commit `a3f8a0d7fbeb56e64acf5d02b54d0e7c480bde4f`.
`.gitmodules` points it to `https://chrome-internal.googlesource.com/chrome/theme/google_chrome`
with `checkout_src_internal` as its condition.
Its BRANDING file is not present in the public src tree and could not be read here.
A public Gitiles attempt returned 404; no claim is made to have verified its complete values.
The official brand behavior follows the verified public `GetProductName()`/build-flag path,
whose documented product examples include Google Chrome.
PRTS does not need that private BRANDING file or Google assets.

## GRIT strings, About, window titles and resources

**Verified:** `chrome/app/BUILD.gn`, `grit_strings("branded_strings")`, selects
`${branding_path_product}_strings.grd`.
Therefore editing BRANDING does not rewrite Chromium text embedded in messages.
Edit the existing unbranded resource sources instead:

- `chrome/app/chromium_strings.grd`: ordinary `IDS_PRODUCT_NAME`, `IDS_SHORT_PRODUCT_NAME`,
  product descriptions, menu/a11y labels and relevant window-title formats -> PRTS.
- `chrome/app/settings_chromium_strings.grdp`: `IDS_SETTINGS_ABOUT_PROGRAM` -> About PRTS,
  and other user-visible Chromium product references -> PRTS.
- Retain resource IDs, placeholders, platform conditions and Chromium author/copyright notices.
- Rebuild GRIT packs; confirm supported locales rather than assuming English edits cover translations.

`chrome/browser/ui/webui/version/version_ui.cc` maps the application label to `IDS_PRODUCT_NAME`
and separately displays `embedder_support::GetUserAgent()`.
After the patch, PRTS as the page's application label and Chrome in the displayed UA are expected.
`BrowserView::GetWindowTitle()` calls `WindowMetadataController::GetWindowTitleForCurrentTab(true)`.
`WindowMetadataController::GetWindowTitleFromWebContents()` uses `IDS_BROWSER_WINDOW_TITLE_FORMAT`
for ordinary non-Mac browser/popup titles with the app name requested.
`BrowserView` also has channel-specific accessible title IDs that require consistent product text.
The pinned local `chromium_strings.grd:445` uses `not is_chromeos` for the ordinary format;
its non-CfT branch at line 454 is selected on Windows. Change its Chromium suffix to PRTS.
The local blob hash `87499190d7614f211d9b9440a360605d8eda1b92` matches the pinned tree;
commit-addressed Gitiles agrees. An earlier tag-addressed response differed in this condition,
so the local commit and commit-addressed source take precedence.

Existing Chromium icons remain Chromium icons unless changed explicitly.
Name changes do not create PRTS graphics; a complete visual brand pass needs owned icon/PNG/ICO assets.
Theme GRIT and Windows tile paths use `branding_path_component`; retaining chromium minimizes build changes.
This naming work does not require Aura, compositor, input or hidden-HWND modifications.

## Windows filename, profiles, registry and installer consequences

**Verified:** Windows executable naming is not controlled by PRODUCT_FULLNAME.
`chrome/BUILD.gn` emits `initialexe/chrome` and copies to `chrome.exe`; the DLL output is chrome.
`chrome/app/chrome_exe.ver` has `ORIGINAL_FILENAME=chrome.exe`.
`chrome/common/chrome_constants.cc` hardcodes `chrome.exe` for browser/helper executable names and paths.
Keep that filename for the minimal product patch; do not merely rename the distributed binary.
A complete executable rename would require auditing launch, relaunch, child process and packaging consumers.

**Verified:** `chrome/common/chrome_paths_win.cc`, `GetDefaultUserDataDirectory()`, appends
`install_static::GetChromeInstallSubDirectory()` and `chrome::kUserDataDirname` under Local AppData.
`chromium_install_modes.h` separately defines empty `kCompanyPathName` and `kProductPathName=L"Chromium"`.
Thus a BRANDING-only patch leaves the default profile under `%LOCALAPPDATA%\Chromium\User Data`.
Use an explicit per-session absolute `--user-data-dir` in the endpoint launcher.
This provides cheap profile separation without redesigning installed Windows identity.

`install_util.cc` composes `GetRegistryPath()` as Software plus the install subdirectory;
it also derives uninstall and policy paths from install constants.
The unbranded install-mode table still has Chromium base app name/id, ChromiumHTM and ChromiumPDF ProgIDs,
Chromium descriptions, the chromium direct-launch scheme and fixed COM/Active Setup identifiers.
Those do not change because BRANDING says PRTS.
`chrome/process_version_rc_template.gni` supplies `branding_file_path` to Windows resource generation;
`chrome/app/chrome_version.rc.version` maps FileDescription/ProductName to PRODUCT_FULLNAME and
ProductShortName to PRODUCT_SHORTNAME. These become PRTS while shell/install identity remains Chromium.
That distinction must be recorded for packaging; it is not full installed-product coexistence.

**Design:** ship a private portable endpoint-managed build first, explicit session profiles,
without default-browser registration or a system installer.
If PRTS must coexist as an installed desktop product, separately patch the install-mode constants,
ProgIDs/AppUserModel identity, scheme, appropriate GUIDs and installer resources/paths coherently.
Do not reuse Google Chrome installation/update identities.
Changing only `kProductPathName` separates some paths but is insufficient for all shell registrations.
No installer or registry changes were made during this research.

[Windows install constants](https://chromium.googlesource.com/chromium/src/+/1aa07b638799e59656559ae6c6649d4b197f555e/chrome/install_static/chromium_install_modes.h).

## Prior art read and reuse assessment

Prior-art root: `C:\Users\Flysoft\Desktop\chromium`; PIN `caef9db47e2cf3fe8e9f07ddceeff6ddf6957385`.
`docs/prts/mvp-identity.md` describes that build as version 158.0.8111.0.
This task targets Stable 157, so its exact version/GREASE fixtures must not be copied unchanged.

| Patch | Verified content | Reuse decision |
|---|---|---|
| `0029-WIP-brand-PRTS-resources-and-preserve-Chrome-web-ide.patch` | Chromium GRIT names and BRANDING -> PRTS; Windows optional brand -> Google Chrome; omit headless prefix | Rebase the two UA sites and selected naming resources |
| `0030-Preserve-Chromium-copyright-attribution-in-PRTS-bran.patch` | Restores Chromium authors/copyright changed by 0029 | Preserve the corrected attribution from the outset |
| `0031-Use-a-build-guard-for-the-Windows-headless-UA-produc.patch` | Uses `#if !BUILDFLAG(IS_WIN)` instead of a constant-false runtime check | Use this final guard form |
| `0032-Brand-the-standard-Settings-string-part-as-PRTS.patch` | Rebrands `settings_chromium_strings.grdp` | Include the current Settings resource part |

The prior identity document reports HTTP/page JS and dedicated/shared/service-worker JS coverage.
It explicitly records worker HTTP UA-CH absence and warns against requiring page-style hints there.
That is useful test design evidence, not proof of unchanged behavior at Stable 157.
It also reports executable-version/About resource checks separately from production launch checks.
Reuse the coverage shape; independently capture this revision and same-version official Chrome.
Do not import unrelated prior patches that change webdriver, geometry or native window behavior for this task.

## Verification checklist and local echo fixture

**Proposed checks, not executed.** Test official Chrome and PRTS on the same Windows host,
architecture, locale and milestone/full version, using fresh separate profiles.
Avoid UA overrides, emulation and extensions in the baseline run.
Repeat a production-launch run without automation-specific flags after instrumented debugging checks.

A temporary Node fixture can be run from a separate validation workspace when implementation begins:

```js
const http = require('node:http');
http.createServer((req, res) => {
  console.log(req.url, req.headers);
  res.setHeader('Accept-CH', [
    'Sec-CH-UA-Full-Version-List', 'Sec-CH-UA-Full-Version',
    'Sec-CH-UA-Platform-Version', 'Sec-CH-UA-Arch',
    'Sec-CH-UA-Bitness', 'Sec-CH-UA-WoW64', 'Sec-CH-UA-Model',
    'Sec-CH-UA-Form-Factors'
  ].join(', '));
  res.setHeader('Cache-Control', 'no-store');
  res.setHeader('Content-Type', 'text/html; charset=utf-8');
  res.end('<!doctype html><title>Identity probe</title>' +
    '<script>fetch("/echo?after-opt-in").then(r=>r.text()).then(console.log)</script>');
}).listen(8765, '127.0.0.1');
```

Use `http://127.0.0.1:8765/` as a potentially trustworthy local test origin,
or serve the fixture over HTTPS; do not generalize HTTP remote-origin CH behavior.
The log contains the request headers; the response body is only the probe page, not a JSON echo.
Perform a second navigation/reload as well as the fetch after the Accept-CH response.
The optional deprecated Full-Version hint should be recorded if the revision supports/emits it;
Full-Version-List is the required modern full-version assertion.

Run in a page console and in dedicated/shared/service-worker fixtures:

```js
const identity = {
  ua: navigator.userAgent,
  low: navigator.userAgentData?.toJSON(),
  high: await navigator.userAgentData?.getHighEntropyValues([
    'fullVersionList', 'uaFullVersion', 'platformVersion',
    'architecture', 'bitness', 'wow64', 'model', 'formFactors'
  ])
};
console.log(JSON.stringify(identity, null, 2));
```

1. Check legacy HTTP User-Agent and JS navigator.userAgent match the expected normal Chrome UA.
2. Require exactly one Google Chrome entry in both JS lists, retaining Chromium and generated GREASE.
3. Compare order, GREASE punctuation and versions against the 157 expectations above.
4. Check HTTP major/full lists equal the corresponding JS list values after structured-header parsing.
5. Require real full versions to be 157.0.8103.15; major versions to be 157; reduced UA to use 157.0.0.0.
6. Require no HeadlessChrome or PRTS in web UA, brands or fullVersionList.
7. Check mobile/platform/architecture/bitness/model/WoW64/formFactors agree with official Chrome on this host.
8. Check high-entropy behavior with permission allowed and denied; absence is not an identity mismatch by itself.
9. Cover top-level navigation, fetch, iframe, worker script/fetch and worker JS; compare header presence per context.
10. Do not inject missing worker hints merely to make every request resemble a page request.
11. Check headed and headless PRTS expose the same intended web identity.
12. As a negative configuration test, confirm a --user-agent launch hits the documented metadata early-return path.
13. Read chrome://version application label/UA and chrome://settings/help; require PRTS product naming.
14. New headless restricts chrome:// access unless --allow-chrome-scheme-url is supplied; use that only for this inspection.
15. Inspect ordinary, popup and accessibility window titles; preserve page/app titles while replacing product branding.
16. Inspect `(Get-Item .\chrome.exe).VersionInfo` ProductName/FileDescription and the installer labels if packaged.
17. Confirm browser/helper launch and relaunch still work with chrome.exe and separate explicit user-data directories.
18. Repeat identity checks using the final extracted distribution, not only the development output directory.

## Other detectable surfaces noticed (outside this patch)

- **Verified source:** `Navigator::webdriver()` in `navigator.cc` exposes AutomationControlled or instrumentation override.
  Its flag-enablement path was not traced here; do not assume UA changes affect webdriver.
- **Prior-art finding:** inner/outer/screen dimensions and invisible resize borders needed separate work.
- **Runtime checks needed:** GPU/WebGL renderer strings, SwiftShader fallback, WebGPU adapter capabilities,
  PDF plugin/MIME lists, screen/display availability, permission results and fonts can differ by environment.
- **Runtime checks needed:** automation launch switches, locale/languages, clipboard/focus and visibility behavior
  may reveal the managed session even with identical UA metadata.
- UA equality does not imply complete behavioral equivalence with the Google-branded binary:
  proprietary codecs, bundled components, field trials and services can remain different.

## Recommendation

Use the two-site Windows UA patch in the shared helper, keeping standard reduction, GREASE and serialization.
Keep `is_chrome_branded=false`; brand the application through the four PRODUCT values and existing unbranded GRIT parts.
For minimal maintenance, retain the Chromium resource directory and chrome.exe filename,
use explicit per-session user-data directories, and defer installed Windows identity changes until packaging requires them.
Rebase the final behavior of prior patches 0029-0032, preserving 0030's attribution and 0031's compile-time guard.
Build and compare official Chrome at the same revision before claiming exact HTTP/JS equivalence.

## Open questions / spikes to verify

- Build the Stable fork and measure the proposed JS/HTTP matrix; all expected output here is source-derived.
- Confirm rebuilt native/a11y window titles use the verified Windows-selected GRIT definitions.
- Check active locale packs and Settings/About strings after rebuilding; define which locales PRTS supports.
- Verify worker request hint presence at Stable 157 instead of relying on prior 158 observations.
- Decide whether PRTS needs custom icon assets now or only textual naming for the streamed MVP.
- Decide whether a dedicated PRTS build flag is warranted before this fork serves multiple embedders.
- If installer coexistence becomes necessary, inventory and allocate the entire Windows registration identity set.
- The private google_chrome/BRANDING file was inaccessible; acquire authorized internal source only if its exact contents become necessary.


