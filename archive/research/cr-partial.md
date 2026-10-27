# Chromium sparse partial checkout

Completed: 2026-10-25 19:00 Asia/Singapore; approximately 13 minutes for clone/setup.
Location: `C:\src\cr-partial`.
Status: usable, clean, shallow, blob-filtered research checkout; all requested sparse directories exist.

## Exact pin and release provenance

| Field | Value |
|---|---|
| Windows Stable tag | `157.0.8103.15` |
| Milestone | 157 |
| Chromium commit | `1aa07b638799e59656559ae6c6649d4b197f555e` |
| Initial repository | `https://chromium.googlesource.com/chromium/src.git` |
| Blob-fetch fallback | `https://github.com/chromium/chromium.git` |
| Final origin | Original googlesource URL, restored after hydration |
| Depth | 1; `rev-parse --is-shallow-repository` returns true |
| Filter | `blob:none`; origin marked promisor |
| HEAD | Detached at exact Stable tag |

Release source: [ChromiumDash Windows Stable JSON](https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Windows&num=1).
The endpoint's `hashes.chromium` matches local `git rev-parse HEAD`.
`git describe --tags --exact-match` returns `157.0.8103.15`.
No gclient, dependency sync, full checkout, Chromium build or source patch was performed.
The prior project under `C:\Users\Flysoft\Desktop\chromium` was not modified.

## Commands and actual fallback

Initial metadata clone succeeded in about nine seconds:

```powershell
git -c core.longpaths=true clone --filter=blob:none --no-checkout --depth 1 --branch 157.0.8103.15 https://chromium.googlesource.com/chromium/src.git C:\src\cr-partial
git -C C:\src\cr-partial config core.longpaths true
git -C C:\src\cr-partial sparse-checkout init --cone
git -C C:\src\cr-partial sparse-checkout set headless ui/aura ui/base/ime ui/base/win ui/events ui/views/widget ui/views/win ui/views/controls/textfield ui/compositor ui/gl ui/gfx/win ui/display/win ui/wm components/viz gpu/ipc media/capture/mojom content/browser/renderer_host content/browser/devtools/protocol content/public/browser components/embedder_support components/version_info chrome/browser/headless chrome/browser/ui/views/frame chrome/app/theme chrome/browser/ui/startup chrome/common build/config third_party/blink/public/common/user_agent sandbox/win/src services/viz ui/ozone/public components/input third_party/blink/renderer/core/frame
git -C C:\src\cr-partial checkout
```

The first checkout's lazy blob fetch stalled and failed:
`RPC failed; curl 56 schannel: server closed abruptly (missing close_notify)`;
`fatal: expected 'packfile'`.
The clone metadata and exact pin survived.
A smaller bootstrap and alternative TLS settings were attempted:

```powershell
git -C C:\src\cr-partial config http.sslBackend openssl
git -C C:\src\cr-partial config http.version HTTP/1.1
git -C C:\src\cr-partial config http.lowSpeedLimit 1
git -C C:\src\cr-partial config http.lowSpeedTime 90
git -C C:\src\cr-partial sparse-checkout set components/embedder_support components/version_info chrome/app/theme/chromium base/version_info
git -C C:\src\cr-partial checkout
```

The retry also stalled. Its own Git process tree was stopped; its stale `.git/index.lock` was removed.
An attempted command-line `-c remote.origin.url=...` did not redirect this fetch in practice;
inspection showed the child still contacting googlesource.
Changing the repository's origin URL explicitly succeeded:

```powershell
git -C C:\src\cr-partial config remote.origin.url https://github.com/chromium/chromium.git
git -C C:\src\cr-partial checkout
git -C C:\src\cr-partial sparse-checkout add headless ui/aura ui/base/ime ui/base/win ui/events ui/views/widget ui/views/win ui/views/controls/textfield ui/compositor ui/gl ui/gfx/win ui/display/win ui/wm components/viz gpu/ipc media/capture/mojom content/browser/renderer_host content/browser/devtools/protocol content/public/browser chrome/browser/headless chrome/browser/ui/views/frame chrome/app/theme chrome/browser/ui/startup chrome/common build/config third_party/blink/public/common/user_agent sandbox/win/src services/viz ui/ozone/public components/input third_party/blink/renderer/core/frame chrome/install_static chrome/browser/ui/window_metadata chrome/browser/ui/webui/version content/browser/client_hints third_party/blink/common/user_agent build/util
git -C C:\src\cr-partial config remote.origin.url https://chromium.googlesource.com/chromium/src.git
```

The smaller bootstrap retains `components/embedder_support`, `components/version_info`, `base/version_info`.
The add command restores every original requested directory and adds narrowly scoped branding research sources.
The mirror supplied Git objects at the already-pinned commit; no branch/version change occurred.
The TLS/HTTP settings above remain repository-local; certificate verification was not disabled.
A Gitiles archive availability probe also returned HTTP 200, but archives were not used to populate this checkout.

## Final sparse coverage

Original requested directories, all present:

```text
headless
ui/aura
ui/base/ime
ui/base/win
ui/events
ui/views/widget
ui/views/win
ui/views/controls/textfield
ui/compositor
ui/gl
ui/gfx/win
ui/display/win
ui/wm
components/viz
gpu/ipc
media/capture/mojom
content/browser/renderer_host
content/browser/devtools/protocol
content/public/browser
components/embedder_support
components/version_info
chrome/browser/headless
chrome/browser/ui/views/frame
chrome/app/theme
chrome/browser/ui/startup
chrome/common
build/config
third_party/blink/public/common/user_agent
sandbox/win/src
services/viz
ui/ozone/public
components/input
third_party/blink/renderer/core/frame
```

Additional directories:

```text
base/version_info
build/util
chrome/install_static
chrome/browser/ui/window_metadata
chrome/browser/ui/webui/version
content/browser/client_hints
third_party/blink/common/user_agent
```

Cone mode includes files directly in ancestor directories as well.
For example `chrome/browser/chrome_content_browser_client.cc` and `chrome/app/chromium_strings.grd`
are available without including every browser or application subtree.

## Size and validation

Measured at 19:00:18 Asia/Singapore, before final report-only edits:

| Measure | Bytes | Approximate MiB |
|---|---:|---:|
| Entire directory, logical file lengths | 235,609,337 | 224.7 |
| `.git` | 126,430,896 | 120.6 |
| Sparse worktree | 109,178,441 | 104.1 |
| Files including `.git` | 10,222 | — |

These are logical file sizes, not NTFS allocated/compressed disk usage.
`git status --porcelain=v1 -uno` produced no entries.
Every path returned by `sparse-checkout list` exists.
`git rev-parse HEAD`, exact-tag describe and shallow-state checks passed.
The requested UA helper and headless source files were successfully read/grepped locally.

## Extending and reading safely

```powershell
git -C C:\src\cr-partial sparse-checkout add path/to/directory
rg -n 'SymbolName' C:\src\cr-partial\path\to\directory
git -C C:\src\cr-partial show HEAD:path/to/file.cc
git -C C:\src\cr-partial sparse-checkout list
```

Use `add`, not `set`, when preserving other tasks' selections.
`git show` may lazily fetch an absent blob without materializing its directory.
`rg` searches available worktree files only; an absent match does not prove absence across all Chromium.
Avoid an unrestricted `git grep HEAD` across missing directories: it can hydrate many blobs.
Do not interpret transient deletion-looking status during initial no-checkout setup as user source changes.
Do not run gclient or full checkout for source-reading tasks.

If new lazy fetches stall against restored googlesource origin, use the tested mirror fallback:

```powershell
git -C C:\src\cr-partial config remote.origin.url https://github.com/chromium/chromium.git
git -C C:\src\cr-partial sparse-checkout add path/to/directory
git -C C:\src\cr-partial config remote.origin.url https://chromium.googlesource.com/chromium/src.git
```

Run these sequentially; coordinate with other readers before changing repository-wide configuration.
Existing fetched blobs remain usable offline.

## Caveats

- Partial clone still downloads selected blobs on checkout; `blob:none` does not mean a blob-free worktree.
- Depth one has no useful history for blame/regression archaeology until history is explicitly fetched.
- Submodules/gitlinks are not populated by sparse-checkout.
- `chrome/app/theme/google_chrome` is an internal gitlink at this tag, not public Google assets.
- Its gitlink commit is `a3f8a0d7fbeb56e64acf5d02b54d0e7c480bde4f`; `.gitmodules` names a chrome-internal repository.
- The repository lacks synced external build dependencies and generated headers; this is not a ready-to-build tree.
- Restoring the original origin favors provenance; future network hydration may require the mirror workaround.

## Recommendation

Use this pinned tree for all related research and expand it with narrow sparse additions.
Keep commit/tag references in reports so later Stable updates cannot silently change conclusions.
Use the explicit mirror URL fallback for hydration if googlesource Git transport stalls again.

## Open questions / spikes to verify

- No setup blocker remains; original requested sparse coverage and clean status were verified.
- Whether googlesource transport instability is transient or specific to large lazy-fetch POST requests is unresolved.
- Buildability is intentionally untested and requires a separate dependency/toolchain setup task.

