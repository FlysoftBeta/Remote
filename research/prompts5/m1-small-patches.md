# Task m1-small-patches (timeout 35 min): patches 01, 11, 12, 13 as verified diffs
Output dir: C:\src\tmp\m1\small\  One file per patch: 01-prts-switch.diff, 11-remote-popups-in-root.diff, 12-prts-branding.diff,
13-chrome-web-identity.diff. Each must pass `git -C C:\src\chromium\src apply --check` alone, and they must stack in order 01,11,12,13
(verify stacking by applying into copies of the touched files under your dir).

- 01 prts-switch: ui/base/ui_base_switches.{h,cc}: `switches::kPrtsRemote` ("prts-remote") and `bool ui::IsPrtsRemoteMode()`
  (cached CommandLine check). Must be callable from ui/aura, ui/views, ui/base/ime, chrome/. Check whether components/embedder_support may
  depend on //ui/base (DEPS + BUILD.gn); if not, 13 uses a tiny local check of the same switch string - say so. Keep the footprint tiny.
- 11 remote-popups-in-root (design/01 D4b, 02 row 11): chrome/browser/ui/views/chrome_views_delegate_win.cc CreateNativeWidget: in remote
  mode, widgets that have a parent/context and are not TYPE_WINDOW/TYPE_CONTROL use NativeWidgetAura (like the shadow_elevation branch);
  ui/views/widget/desktop_aura/desktop_native_widget_aura.cc GetDefaultParent: remote mode returns root_window_ like the Fuchsia branch.
  Read the whole functions. In NOTES list which UI elements (app menu, context menus, bubbles, tooltips, omnibox popup, content `<select>`
  popup = RenderWidgetHostViewAura::InitAsPopup, permission prompts, download bubble, tab hover card, browser-modal dialogs, drag images)
  go through which path and whether they end up inside the browser root after the patch. Flag what would still create a new top-level
  DesktopWindowTreeHostWin (TYPE_WINDOW dialogs etc.) and what that implies. If a third hook is clearly needed, add it (<=10 lines) and justify.
- 12 prts-branding: only the product name the user sees becomes "PRTS": chrome/app/theme/chromium/BRANDING (look at its keys) and the
  minimal set of GRIT strings (chrome/app/chromium_strings.grd IDS_PRODUCT_NAME / IDS_SHORT_PRODUCT_NAME and similar core ones, ~5-10).
  Do NOT change anything that affects the web-exposed UA/brands (that is 13). Note other places still showing "Chromium".
- 13 chrome-web-identity (research/d-branding.md, design/02 2.6): components/embedder_support/user_agent_utils.cc: in remote mode the
  UA-CH brand list uses "Google Chrome" (instead of the Chromium-branded name) and the UA string has no "Headless" prefix. Read
  d-branding.md first. Brand logic must stay correct for GREASE ordering and the full-version list.
NOTES.md: per patch files, line counts, citations, risks; expected UA / Sec-CH-UA / navigator.userAgentData.brands values.
