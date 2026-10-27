# Release assembly

From the repository root, using Windows PowerShell 5.1 or later:

```powershell
./tools/release/package.ps1 -Version 0.1.0 -Out C:\releases `
  -ReExe C:\build\re.exe
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
copies the existing build's generated third-party credits. It installs locked
production npm dependencies into staging with lifecycle scripts disabled, using
the pinned Node distribution's npm. Network access to nodejs.org and the npm
registry is required on the packaging machine. Runtime machines need neither.

Outputs are a versioned folder, ZIP (including that root folder), and adjacent
`SHA256SUMS.txt`. The folder also has `FILES.sha256` for individual payload files.
An existing version is rejected to avoid stale payloads. Use a new output
directory after an interrupted attempt. `.cache` contains downloads and temporary
Node extraction trees and is excluded from the release. Each Node zip is verified
against the digest pinned in package.ps1 before fresh extraction. To update Node,
update both the version and its win-x64 ZIP hash from the official release's
`https://nodejs.org/dist/v<VERSION>/SHASUMS256.txt`, then smoke-test again.

ZIP assets of 1.9 GB or larger become numbered binary chunks, with reassembly
instructions and per-part hashes. Publish the archive or parts, instructions if
any, and SHA256SUMS.txt. The portable folder itself is not an upload asset.

Before publishing, smoke-test staged rb via DevTools `/json/version`, start the
launcher on an unused port, then qualify real re/WebRTC and review licensing.
The packaging script intentionally does not launch services or publish assets.
