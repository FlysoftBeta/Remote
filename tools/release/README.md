# Release assembly

From the repository root, using Windows PowerShell 5.1 or later:

```powershell
corepack pnpm install
corepack pnpm typecheck
corepack pnpm test
corepack pnpm build
# Wait for any existing siso.exe build; never clean out/rel or change args.gn.
# In C:\src\chromium\src (local builds can add -offline):
siso ninja -C out\rel chrome re components/resources:about_credits
# Back at the repository root:
./tools/release/package.ps1 -Version 0.2.1 -Out C:\releases
```

No builds are performed. The default browser input is
`C:\src\chromium\src\out\rel`; override `-ChromeOut` and `-ChromiumSrc` together
for another checkout. The endpoint default is `<ChromeOut>/re.exe`. Use the non-component release
build; component builds need their transitive component DLLs. Explicit extra endpoint
DLLs can be passed through `-ReRuntimeDll @('C:\build\runtime.dll')`.
Missing endpoint inputs produce a control-plane-only package with a warning.

The script reads Chromium's mini-installer `chrome.release` runtime patterns,
preserves locales and component subdirectories, adds the enabled Widevine tree,
V8 snapshots and MSVC runtimes, and excludes debugging/linker artifacts. It
copies the existing build's generated third-party credits. The TypeScript CP
ships as control-plane/dist/server.mjs, bundled by esbuild with the locked ws
runtime; the React viewer ships as viewer/dist. There is no runtime npm install
or development node_modules. JavaScript MIT notices and /tools assets are included.
Node is downloaded from nodejs.org with a pinned SHA-256; dependency installation
uses the committed pnpm lockfile before packaging. Runtime machines need neither.

Outputs are a versioned folder, ZIP (including that root folder), and adjacent
`SHA256SUMS.txt`. The folder also has `FILES.sha256` for individual payload files.
An existing version is rejected to avoid stale payloads. Use a new output
directory after an interrupted attempt. C:\src\tmp\remotekit-release-cache contains
downloads and temporary Node extraction trees, outside the release. Each Node zip is verified
against the digest pinned in package.ps1 before fresh extraction. To update Node,
update both the version and its win-x64 ZIP hash from the official release's
`https://nodejs.org/dist/v<VERSION>/SHASUMS256.txt`, then smoke-test again.

ZIP assets of 1.9 GB or larger become numbered binary chunks, with reassembly
instructions and per-part hashes. Publish the archive or parts, instructions if
any, and SHA256SUMS.txt. The portable folder itself is not an upload asset.

Before publishing, run the packaged start.ps1 on a free port with -DataDir under
C:\src\tmp. Register the first admin through POST /api/auth/register, create a
profile and job-isolated instance using its returned Bearer session, start it,
and poll GET /api/instances/:id until running (native endpoint hello received).
Stop the instances and launcher afterwards. The existing input/WebRTC smoke is
node tools/smoke/smoke.mjs; test-login options are documented in docs/control-plane.md.
The packaging script intentionally does not launch services or publish assets.
