# M1 small patches: 01, 11, 12, 13

## Scope and status

Completed against Chromium **157.0.8103.15**, HEAD **1aa07b638799e59656559ae6c6649d4b197f555e** (verified by `git show`). Read `research/d-branding.md` before drafting identity changes, plus design/01 D4b/D11 and design/02 rows 01/11/12/13 and section 2.6.

All writes are under `C:\src\tmp\m1\small`. No checkout/repository changes, commits, builds, GN, ninja, siso, or prior-art copying. `[V]` means source/header read at the pin; `[I]` means source-derived behavior awaiting a build/runtime test. All source citations below are relative to `C:\src\chromium\src` and refer to unpatched line numbers. API compatibility was reviewed from headers; compilation was not run because the checkout/build is reserved.

## Files and line counts

Counts are additions/deletions from `git apply --numstat`; diff lines include headers/context.

| Patch | Touched file | + / - |
|---|---|---|
| 01-prts-switch.diff (63 diff lines) | ui/base/BUILD.gn | 2 / 0 |
| | ui/base/ui_base_switches.cc (new) | 18 / 0 |
| | ui/base/ui_base_switches.h | 10 / 0 |
| 11-remote-popups-in-root.diff (51 diff lines) | chrome/browser/ui/views/chrome_views_delegate_win.cc | 9 / 0 |
| | ui/views/widget/desktop_aura/desktop_native_widget_aura.cc | 6 / 0 |
| 12-prts-branding.diff (115 diff lines) | chrome/app/chromium_strings.grd | 11 / 11 |
| | chrome/app/settings_chromium_strings.grdp | 1 / 1 |
| | chrome/app/theme/chromium/BRANDING | 4 / 4 |
| 13-chrome-web-identity.diff (141 diff lines) | chrome/browser/headless/test/headless_mode_browsertest.cc | 10 / 1 |
| | components/embedder_support/BUILD.gn | 2 / 0 |
| | components/embedder_support/user_agent_utils.cc | 9 / 1 |
| | components/embedder_support/user_agent_utils_unittest.cc | 22 / 6 |

Totals: 01 +30/-0; 11 +15/-0; 12 +16/-16; 13 +43/-8. There are 12 unique touched paths. Patch 13 changes 9 added/1 replaced lines in the production C++ file; its remaining edits maintain existing test expectations and their dependency.

## 01: switch and API citations

- [V] `ui/base/ui_base_switches.h:13-123` contains inline switch constants and already includes `base/component_export.h`. There is **no upstream ui_base_switches.cc at this pin**. The patch adds that file, registers it next to the header at `ui/base/BUILD.gn:218`, adds inline `switches::kPrtsRemote = "prts-remote"`, and declares `COMPONENT_EXPORT(UI_BASE) bool ui::IsPrtsRemoteMode()`.
- [V] Newly called APIs: `base/command_line.h:129` declares `static CommandLine* ForCurrentProcess()`; `:198-199` declares both `HasSwitch` overloads, including the constant char-array overload. The new implementation uses those exact signatures. Export precedent: `ui/base/ui_base_switches_util.h:12-13`; implementation macro `IS_UI_BASE_IMPL`: `ui/base/BUILD.gn:482`. Existing base dependency: `:491,503`.
- [I] The function-local `static const bool` caches the first command-line lookup once per process in the UI_BASE component. CommandLine must be initialized first, and the switch must be set before the first call. No per-window or mutable runtime toggle is supported. The helper itself is the runtime guard, so its definition is necessarily unconditional.
- [V] Callable from Aura (`ui/aura/BUILD.gn:119`, direct dependency on //ui/base), Views (`ui/views/BUILD.gn:659-667`, public dependency), Chrome Views delegate (`chrome/browser/ui/views/BUILD.gn:1210-1220`, public dependency on Views), and the IME factory (`ui/base/ime/init/BUILD.gn:29`, direct dependency). Core `//ui/base/ime` is a separate target and has no base dependency (`ui/base/ime/BUILD.gn:60-100`); a future core-IME call site must add `//ui/base` to that target. The planned factory hook already has the dependency. Do not add it to ime_types.
- [V] `components/embedder_support/DEPS:21` allows `+ui/base`; `components/embedder_support/BUILD.gn:53-68` gives `:user_agent` a direct `//ui/base` dependency. **13 uses ui::IsPrtsRemoteMode(); no duplicate string/local fallback.**

## 11: routing, API citations and exact hooks

[V] Read both full functions: `ChromeViewsDelegate::CreateNativeWidget` (`chrome/browser/ui/views/chrome_views_delegate_win.cc:124-161`) and `DesktopNativeWidgetAuraWindowParentingClient::GetDefaultParent` (`ui/views/widget/desktop_aura/desktop_native_widget_aura.cc:252-276`).

1. The Chrome hook goes after `use_desktop_widget_override()`, immediately before the factory return. With the switch and parent **or context**, any type except TYPE_WINDOW/TYPE_CONTROL selects kNativeWidgetAura. Putting it last prevents the upstream parent/desktop override from undoing the selection. This deliberately overrides popup software-compositor requests too, as the existing elevation-shadow branch already can.
2. The Aura parenting hook goes at function entry, before the Fuchsia conditional. In remote mode it returns root_window_, avoiding `DesktopNativeWidgetTopLevelHandler::CreateParentWindow`. It covers menu and fullscreen parenting through this client; it does not intercept creation of every desktop widget.

[V] New hook API/type checks: `ui/views/widget/widget.h:239-250` defines TYPE_WINDOW/TYPE_CONTROL/MENU/POPUP/TOOLTIP/BUBBLE/DRAG; `:451` defines parent and `:493` context. `chrome/browser/ui/views/native_widget_factory.h:10-20` defines both NativeWidgetType values and the existing factory signature. `native_widget_factory.cc:29-45` constructs NativeWidgetAura when the chosen type has parent/context. `ui/views/widget/native_widget_aura.cc:240-242` requires parent/context, and `:295-349` preserves transient ownership then parents through the context root. Both hooks only call the new exported helper; no new Aura/Views API is invented.

The following outcomes are [I] from the cited [V] call paths, not measured capture results:

| UI element | Path at the pin | Result with patch 11 |
|---|---|---|
| App menu and submenus | `chrome/browser/ui/views/toolbar/app_menu.cc:1088,1108` MenuRunner; `ui/views/controls/menu/submenu_view.cc:519-540`; `menu_host.cc:147-200` TYPE_MENU with parent/context | NativeWidgetAura in owning browser root; menu parenting cannot allocate another desktop host. |
| Context menus | `chrome/browser/ui/views/renderer_context_menu/render_view_context_menu_views.cc:148-152,434` delegates RunMenuAt; same Views MenuHost path | In root when parent/context is the browser. Unparented/native OS menus are outside this guarantee. |
| Bubbles | `ui/views/bubble/bubble_dialog_delegate_view.cc:173-210` creates TYPE_BUBBLE, deriving parent from parent_window or anchor_widget | Parented bubbles remain in that browser root. A bubble with has_parent=false and no context is not covered. |
| Tooltips | `desktop_window_tree_host_win.cc:348-349` creates TooltipAura; `ui/views/corewm/tooltip_aura.cc:196-214` TYPE_TOOLTIP, context only | Context-only predicate selects NativeWidgetAura in root. A parent-only predicate would miss this. |
| Omnibox popup | `chrome/browser/ui/views/omnibox/omnibox_popup_view_views.cc:115-132` TYPE_POPUP, parent plus context, optional Windows software-compositing request | In root using the root compositor. |
| Content <select> popup (InitAsPopup) | `content/browser/renderer_host/render_widget_host_view_aura.cc:454-540` creates Aura WINDOW_TYPE_MENU; `:502-504` ParentWindowWithContext using parent host view root | Bypasses Chrome Views creation entirely; GetDefaultParent now keeps the raw Aura window in browser root. Existing SetBounds screen-to-parent conversion remains (`:546-563`). |
| Permission prompt bubbles | `chrome/browser/ui/views/permissions/permission_prompt_bubble_base_view.cc:204-209` -> BubbleDialogDelegateView::CreateBubble | Anchored prompt bubbles in root. System/native prompts and dialog variants are not promised. |
| Download bubble | `chrome/browser/ui/views/download/bubble/download_toolbar_ui_controller.cc:647-650,676-679` anchored BubbleDialogDelegate -> CreateBubbleDeprecated | TYPE_BUBBLE follows the bubble path into root. |
| Tab hover card | `chrome/browser/ui/views/tabs/hovercard/tab_hover_card_bubble_view.cc:643-648,707` anchored BubbleDialogDelegateView -> CreateBubble | In owning browser root. |
| Browser-modal dialogs | `components/constrained_window/constrained_window_views.cc:322-342` -> CreateDialogWidget; `ui/views/window/dialog_delegate.cc:145-178` defaults TYPE_WINDOW and only marks web-modal kChild dialogs child | Browser-modal kWindow dialogs normally stay DesktopNativeWidgetAura -> new DesktopWindowTreeHostWin. Child web-modal dialogs can already use NativeWidgetAura via the existing child branch. |
| Drag images | `ui/views/widget/desktop_aura/desktop_drag_drop_client_win.cc:132-136` calls OS DoDragDrop; `ui/base/dragdrop/os_exchange_data_provider_win.cc:968-996` uses IDragSourceHelper/SHDRAGIMAGE | Windows shell drag visuals are outside this root. Generic TYPE_DRAG widgets with parent/context would match the hook, but this does not move OS drag images into Aura. |

### Remaining roots and popup risks

- [V] `chrome/browser/ui/views/chrome_views_delegate.cc:209-222` returns early for supplied native_widget or a successful custom factory. `ui/views/widget/widget.cc:122-135` also honors supplied native widgets. Those explicit desktop paths bypass the Chrome hook. The Aura client hook only affects windows that ask that client for parenting.
- [I] TYPE_WINDOW browser-modal dialogs, unparented popups, separate browser windows, explicit desktop hosts, and native OS dialogs can still create/use independent roots or native surfaces. A single browser-root capturer will not include their pixels, and input injected solely into that root will not reach them. Track/capture and route those roots, or make an additional scoped dialog policy in subsequent work; this patch does not establish complete single-root UI coverage.
- [I] Root-local popups are clipped at the root bounds; use a coherent virtual display/window geometry. Verify shadows, placement at all edges, DSF, z-order, focus, capture dismissal, keyboard navigation, fullscreen content, and GPU/root compositing in S11. force_software_compositing is left in InitParams but does not create a compositor in NativeWidgetAura; there is one root compositor.
- No third hook added: the two hooks reach the requested ordinary popup paths, including context-only tooltips and content InitAsPopup. TYPE_WINDOW exclusion is an explicit requirement, and fixing shell drag visuals or every explicit native widget needs more than a justified small popup hook. Patch 06 separately owns OS drag handling.

## 12: product branding and retained Chromium text

- [V] `chrome/app/theme/chromium/BRANDING:1-10`: change only PRODUCT_FULLNAME/PRODUCT_SHORTNAME to PRTS and both PRODUCT_INSTALLER_* names to PRTS Installer. Preserve company, copyright and Mac identifiers.
- Ten GRIT IDs change, using only their message bodies: IDS_PRODUCT_NAME, IDS_SHORT_PRODUCT_NAME, IDS_APP_MENU_YOUR_CHROME_HEADER, IDS_PRODUCT_DESCRIPTION, IDS_WELCOME_TO_CHROME (`chrome/app/chromium_strings.grd:296-318`); IDS_TASK_MANAGER_TITLE (`:366-368`); IDS_ACCESSIBLE_BROWSER_WINDOW_TITLE_FORMAT (`:493-495`); IDS_ABOUT and IDS_RELAUNCH_TO_UPDATE (desktop title-case and sentence-case alternatives, `:1149-1173`); IDS_SETTINGS_ABOUT_PROGRAM (`chrome/app/settings_chromium_strings.grdp:21-23`). ChromeOS-specific alternatives and Chrome for Testing names are retained. Attributes, placeholders, and message IDs are preserved.
- [V] `base/version_info/version_info_values.h.version:10` derives PRODUCT_NAME from PRODUCT_FULLNAME; `base/version_info/version_info.h:24-26` returns it. [V] UA product helpers independently hardcode Chrome/ (`components/version_info/version_info_with_user_agent.cc:12-18`, declarations in its header `:14,18`). [V] Chromium-branded UA-CH omits the optional product name (`user_agent_utils.cc:161-164`). Thus patch 12 alone does not rename UA/UA-CH in the specified is_chrome_branded=false build.
- This is intentionally **build-time** resource naming, not a runtime C++ hook: UI names become PRTS even without --prts-remote. Static BRANDING/GRIT cannot use ui::IsPrtsRemoteMode(). Runtime behavioral hooks in 11/13 are guarded and have PRTS comments.
- Other Chromium text remains: Settings help button (`settings_chromium_strings.grdp:24-25`), other Settings/default-browser/sync/error prose, help/menu strings not among the ten IDs, beta/dev/canary accessible titles (`chromium_strings.grd:498-505`), legal attribution, logos/icons, ChromeOS/CfT/Mac-specific naming. Windows shell/install identities still use Chromium (`chrome/install_static/chromium_install_modes.h:24,57-65`), so profile/install paths, associations and shell UI are not fully rebranded. These were deliberately not changed.
- [I] GRIT translation fingerprints change with source text; regeneration is required and localized PRTS wording is not validated. Keep generated-resource IDs stable. This minimal patch is not a full localization/installer rebrand.

## 13: web identity, APIs and expected values

- [V] `components/embedder_support/user_agent_utils.cc:153-174`: choose Google Chrome in the optional browser brand when ui::IsPrtsRemoteMode(). Both major (`:180-185`) and full-version (`:202-207`) callers share this function. Upstream GREASE/base Chromium entry/additional-brand handling/shuffle stay unchanged (`:214-255,511-555`).
- [V] `GetUserAgentInternal` (`:447-461`) only inserts Headless when remote mode is false. The normal reduced/full-version selection at `:466-471`, command-line overrides (`:473-491`), platform details and metadata policy are preserved.
- [V] Public API declarations read: `components/embedder_support/user_agent_utils.h:35,39,51-87`; `third_party/blink/public/common/user_agent/user_agent_metadata.h:28-39,50-61` for brand fields/types/serialization; `base/version_info/version_info.h:24-26` for GetProductName used in maintained test expectations. No generator/serialization API changes.
- [V] Existing metadata test (`user_agent_utils_unittest.cc:302-398`) originally expected GetProductName even for a Chromium build. After patch 12 that would incorrectly expect PRTS. It now expects Chromium in nonremote Chromium builds, the actual product in other nonremote builds, and Google Chrome in remote mode, for major/full/low-entropy checks. Remote full/major lists must have three entries. Existing generator tests keep their explicit input semantics.
- Existing HeadlessUserAgent (`user_agent_utils_unittest.cc:698-706`) and HTTP browser test (`chrome/browser/headless/test/headless_mode_browsertest.cc:328-349`) check ordinary HeadlessChrome without the switch and Chrome with no HeadlessChrome in remote mode. No tests append/remove prts-remote after helper initialization.
- [V] Blink copies the common major/full lists (`third_party/blink/renderer/core/frame/navigator_ua.cc:17-26`); brands() returns that major list (`navigator_ua_data.cc:96-100`). [I] JS and HTTP values should therefore agree; confirm on the built browser and workers.

[I] Expected defaults for Windows x64, --headless=new --prts-remote, is_chrome_branded=false, no UA/DevTools override, ordinary UA reduction enabled:

```text
navigator.userAgent / User-Agent:
Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/157.0.0.0 Safari/537.36

Sec-CH-UA:
"Not-A.Brand";v="99", "Google Chrome";v="157", "Chromium";v="157"

Sec-CH-UA-Full-Version-List (when eligible/opted in):
"Not-A.Brand";v="99.0.0.0", "Google Chrome";v="157.0.8103.15", "Chromium";v="157.0.8103.15"

navigator.userAgentData.brands:
[{brand: "Not-A.Brand", version: "99"},
 {brand: "Google Chrome", version: "157"},
 {brand: "Chromium", version: "157"}]
```

[V] GREASE calculation: 157 % 11 = 3 and 158 % 11 = 4 select '-' and '.'; 157 % 3 = 1 selects 99. The three-brand permutation is entry 157 % 6 = 1, destinations {0,2,1}; input GREASE, Chromium, Google Chrome becomes GREASE, Google Chrome, Chromium. Do not hardcode this order in production. If UA reduction is disabled, the UA product is Chrome/157.0.8103.15; both real full-version brands stay 157.0.8103.15. No PRTS web brand is added. Without the switch, the unbranded headless UA remains HeadlessChrome and the two-brand Chromium/GREASE logic is unchanged.

## Integration and validation

1. Apply 01, 11, 12, 13 in that order in a separately authorized integration checkout, after the running build is done. Do not apply them to this reserved checkout during M1. They use distinct touched paths between patches. `apply --check` alone is textual validation: 11/13 require 01 to compile; 12 and 13 belong together for the resource-sensitive metadata test expectation.
2. 01 already contains the necessary `//ui/base:base` sources entry and export. No new library dependency is needed. Include ui/base/ui_base_switches.h at later hooks and ensure their GN targets reach //ui/base.
3. 11 needs no BUILD.gn change: `//chrome/browser/ui/views:chrome_views_delegate` publicly depends on //ui/views (`chrome/browser/ui/views/BUILD.gn:1210-1220`), which publicly exposes //ui/base (`ui/views/BUILD.gn:659-667`); //ui/views itself already has that dependency. No new content-layer dependency is introduced.
4. 12 changes only existing branding/resource inputs. Keep is_chrome_branded=false. Normal resource/version regeneration will pick these up; do not modify generated files by hand.
5. 13 production :user_agent already depends on //ui/base. Its patch adds a direct //ui/base dependency to `components/embedder_support:unit_tests` for the new test include. The headless test target (`chrome/browser/headless/BUILD.gn:100-169`) already reaches //ui/base through its existing test/Views dependencies and includes other headers from that target. No production dependency expansion is needed.
6. All four passed these exact read-only checkout commands, each exit 0:

```powershell
git -C C:\src\chromium\src apply --check C:\src\tmp\m1\small\01-prts-switch.diff
git -C C:\src\chromium\src apply --check C:\src\tmp\m1\small\11-remote-popups-in-root.diff
git -C C:\src\chromium\src apply --check C:\src\tmp\m1\small\12-prts-branding.diff
git -C C:\src\chromium\src apply --check C:\src\tmp\m1\small\13-chrome-web-identity.diff
```

`validate.py` copies only touched original files into `small/stack/`, then runs `git -C C:\src\tmp\m1\small\stack apply --check --whitespace=error <absolute-diff>` and actual `apply --whitespace=error` for each patch in series. All eight stack operations exit 0; all 12 final files match the drafted b copies byte-for-byte. Both GRIT XML files parse. XML inspection confirms exactly ten changed IDs and preserved attributes/placeholders. Exact commands, exit codes, pin and patch SHA256 hashes are in validation.log; per-file stats in stats.txt. `draft.py` retains reproducible copy/edit/diff generation; a/b snapshots remain for review. These scripts perform no build.

## Open risks and follow-up validation

- No C++ compilation, browser run, pixel capture or identity test execution was permitted/performed. apply --check proves patch applicability, not successful compilation or D4b behavior. Header/target review found no missing production API/dependency.
- After integration/build, run existing `UserAgentUtilsTest.UserAgentMetadata` and `UserAgentUtilsTest.HeadlessUserAgent` in fresh components_unittests processes, once without and once with --prts-remote. Also run existing explicit-brand/GREASE generator tests unchanged. Run `HeadlessModeUserAgentBrowserTest.UserAgentHasHeadless` in fresh browser_tests launches with/without --prts-remote. Do not attempt to toggle the cached switch inside a test process.
- Runtime S11 must confirm all popup rows above and enumerate remaining desktop roots. Browser-modal dialogs and native OS surfaces remain a material capture/input gap for a strict single-root-only design.
- Runtime identity validation must compare navigation headers, JS, workers and opt-in full-version hints. Existing --user-agent/DevTools override behavior can bypass the ordinary identity path. Standalone headless_shell overrides metadata separately and is outside this task.
- Nonremote production behavior is preserved for 11/13; patch 12 is intentionally static UI product naming. Google-branded or CfT builds and non-Windows remote builds are not validated. Pin upgrades require checking GREASE expectations and the popup call paths again.
