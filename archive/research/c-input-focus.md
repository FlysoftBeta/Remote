> **SUPERSEDED / INCOMPLETE** (run killed in a cancelled attempt). Use research/b1-frame-path.md, b2-capture.md, b3-pacing.md (frames) and c1-input-layer.md, c2-ime-osk.md (input) instead.

# PRTS input, focus, activation and editable-state research

Research date: 2026-10-25. Scope: Windows unified/new headless, Aura and Views; no implementation or runtime qualification.

## Evidence and source baseline

- **Verified upstream** means implementation read at Chromium **157.0.8103.15**, commit **`1aa07b638799e59656559ae6c6649d4b197f555e`** (M157).
- The [Chromium Dash Windows Stable endpoint](https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Windows&num=1) returned that version and hash during this investigation.
- Source is read without changing `C:/src/cr-partial`; repository HEAD is the same stable commit. Paths in upstream sections are relative to Chromium `src/`.
- **Prior code** means read-only inspection of `C:/Users/Flysoft/Desktop/chromium/patches/` and `docs/prts/`; it does not establish stable compatibility or a fresh runtime pass.
- **Inference / proposal** explicitly identifies design choices and untested behavior.
- Requirements: `design/00-brief.md`; initial context: `research/a-prior-art.md`. Preserve the cloaked stand-in HWND and normal Chrome UI; do not use Ozone headless or OS input synthesis.

## Injection layers and event route

Investigation in progress.

## Focus, activation, capture and cursor

Investigation in progress.

## Keyboard and remote IME

Investigation in progress.

## Editable state and remote keyboard

Investigation in progress.

## Touch and gesture processing

Investigation in progress.

## Prior-art findings and stable differences

Investigation in progress.

## Recommendation

Initial direction: construct `ui::Event` objects on the browser UI sequence and dispatch into the correct Aura host, retaining Views and content routing. Verify existing headless activation before proposing additional fake-activation patches.

## Open questions / spikes to verify

- Stable headless focus, IME and gesture behavior with two instances on one Windows desktop.
- Separate desktop popup hosts, logical capture and coordinate conversion at fractional device scale.
- One editable-state subscription spanning Views textfields and renderer editors.
