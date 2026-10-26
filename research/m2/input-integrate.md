# m2-input-integrate

Started: 2026-10-26. Integrate cursor/IME/OSK and touch/l10n drafts, incremental build and real-chain validation.

## Work
- Initial inspection in progress.

## Files changed
- Pending.

## Results / evidence
- Pending.

## API citations
- [V] pending pin header review.

## Risks / unverified
- Drafts have not yet been applied or built.

## Integration progress
- Applied all four main patches with git apply -3.
- Resolved Mojo append conflicts retaining SetViewport, ImeHidden, SetTouchCapabilities; controller includes retain cursor + web preference bridges; endpoint hello retains state replay + capability forwarding; viewer teardown retains latency stats reset + cursor reset.
- Existing latency changes preserved. No commits/reset/rebase.
- Viewer JavaScript syntax checks pass.
- Conflict marker removal rerun with Node after discovering WindowsApps python.exe alias did not execute piped script; syntax checks now genuinely pass. First Mojo build failed on markers; corrected before retry.
- [V] Read pin declarations: ui/base/cursor/cursor.h (custom bitmap/hotspot/scale); ui/aura/client/cursor_client.h (GetCursor/IsCursorVisible/GetCursorClient); ui/aura/window_tree_host.h (window/GetWeakPtr); ui/gfx/codec/png_codec.h:119 and base/base64.h:21 (optional PNG + span base64); ui/base/ime/text_input_client.h (weak client/composition/InsertText/caret/delete), virtual_keyboard_controller.h (dismiss); chrome_content_browser_client_parts.h:61 and blink web_preferences.h:129-136 (touch preferences). Runtime behavior still pending.
- Chromium incremental `be.sh chrome`: PASS (The build has finished successfully, 11:24 local). Endpoint matched-contract build running.
- Matched endpoint incremental `be.sh`: PASS. Started real CP+endpoint+PRTS Chrome (9333), Edge viewer (9446), fixture HTTP (8406).

## Validation results (second agent, real chain: CP + prts_endpoint + PRTS chrome, headless Edge viewer via CDP; no fixes needed)
| # | Item | Result | Evidence |
|---|------|--------|----------|
| 1 | Cursor: link->pointer, text field->text, custom `cursor:url(data:png)`->data URL | PASS | desktop-results.json (cursor-#link/#plain/#custom), desktop-wire.json |
| 2 | IME/OSK: focus state logged; insertText "hello" once; preedit "ni" then commit "你" exactly once (value "hello你"); Backspace; stale context rejected; blur hides | PASS | desktop-results.json, ime-hello-chinese.png, desktop-final.png |
| 2b | Touch tap on remote input shows OSK textarea; blur hides it | PASS | touch-results.json (touch-osk-focus, touch-blur-hides), touch-osk.png |
| 3 | Touch: maxTouchPoints=5 and (pointer:coarse) on remote; tap clicks link; one-finger drag scrolls (innerScroll 174); pinch visualViewport.scale 3.03 | PASS | touch-results.json, touch-wire.json, touch-scroll.png, touch-pinch.png |
| 4 | `--cr-lang=zh-CN`: navigator.language zh-CN; settings "关于 PRTS", version page product "PRTS", NTP "自定义 PRTS" | PASS (headless: no native menu; verified via chrome:// pages) | zh-results.json, zh-settings-help.png, zh-version.png, zh-newtab.png, zh-settings-people.png |

Fixes made: none (no files changed in this pass; scripts added: zh.mjs in this dir).
Remaining: leftover "Chrome 中的 AI" / "Chrome 应用商店" strings in settings/NTP (other string tables, not in chromium_strings XTB); native app/profile menu (non-headless) and real device touch not checked; cursor test uses a 32px PNG, hotspot not verified visually.
