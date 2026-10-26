# Windows x64 Widevine staging and later integration

The official updater returned **4.10.3112.0** for Windows x64 / Chrome
157.0.8103.15 on 2026-10-26 (Asia/Singapore). It is staged in `cdm/`.
The archive SHA-256 matches Google's response. Static manifest checks match the
pinned Chromium ABI. No browser build or playback test was run.

All work for this task stayed under `C:\src\tmp\m1\widevine`.
The copy, GN, build, and browser commands below are **future integration steps**;
none were run against the read-only checkout or its active build.

## 1. Provenance and package contents

The exact protocol 3.1 POST body is `update-request.json`. It uses appid
`oimompecagnajdejgnnjijobebaeigek`, `@os=win`, `arch=x64`, `os.platform=win`,
`os.arch=x64`, `prodversion=157.0.8103.15`, `acceptformat=crx3`, and component
version `0.0.0.0` to request the currently offered full package.

POST endpoint: <https://update.googleapis.com/service/update2/json>.
The raw response, including Google's anti-XSSI prefix, is `update-response.raw`;
the parsed body is `update-response.json`. No non-Google binary mirror was used.

Returned HTTPS download URL:
<https://dl.google.com/release2/chrome_component/acomjnhpjyodpde7g3m33u5nkqoa_4.10.3112.0/oimompecagnajdejgnnjijobebaeigek_4.10.3112.0_win64_a5xadnsxzavp7cn4ay5kc6gq6u.crx3>

- CRX3 size: **22,976,546 bytes**.
- Response and actual archive SHA-256:
  `1090c5d6881985c042370d506e15f1db1d0a88bfb7e9602a9202fac421098eb3`.
- DLL SHA-256:
  `7b44e84403fbb3faee15ca2d56eb8432fdf4eb68812c4460abdd20972528e825`.
- DLL PE machine: `0x8664` / AMD64; file/product version `4.10.3112.0`.
- CRX3 magic `Cr24`, format version 3, protobuf header 1,049 bytes; ZIP starts
  at byte `12 + 1049 = 1061`. `unpack-crx3.ps1` stripped the header, checked the
  ZIP signature and extraction paths, and extracted `widevine-payload.zip`.
- Full file hashes: `SHA256SUMS.txt`; evidence: `download-verification.json`,
  `unpack-verification.json`, `compatibility-verification.json`.

| Extracted file under `cdm/` | Bytes |
| --- | ---: |
| `manifest.json` | 1,005 |
| `LICENSE` | 473 |
| `_platform_specific/win_x64/widevinecdm.dll` | 22,970,008 |
| `_platform_specific/win_x64/widevinecdm.dll.sig` | 1,427 |
| `_metadata/verified_contents.json` | 1,550 |

The manifest also describes x86 and arm64 platforms, but this archive contains
only the x64 DLL. References to icon images have no corresponding ZIP entries;
the pinned CDM installer checks the DLL and CDM manifest, not those icons.
Do not modify the downloaded manifest or binary.

To repeat the same HTTP request into a separate response file in staging:

```powershell
$stage = 'C:\src\tmp\m1\widevine'
$ProgressPreference = 'SilentlyContinue'
Invoke-WebRequest -UseBasicParsing `
  -Uri 'https://update.googleapis.com/service/update2/json' `
  -Method Post -ContentType 'application/json' `
  -Body ([System.IO.File]::ReadAllBytes((Join-Path $stage 'update-request.json'))) `
  -OutFile (Join-Path $stage 'update-response-recheck.raw') -TimeoutSec 45
```

A future response can offer a different version. Select the HTTPS
`dl.google.com` (or official `*.gvt1.com`) codebase from that response and verify
its `hash_sha256` and `size` before unpacking. The saved package is the version
offered to this exact request, rather than a guarantee of a universal newest
version across updater cohorts. Archive digest validation was performed;
independent CRX3 signature verification was not performed.

## 2. Copy into the source tree later

Run only after the current build has finished and the checkout is writable for
integration. The pin expects flat source inputs in
`third_party/widevine/cdm/win/x64/`; its output layout is different.

```powershell
$ErrorActionPreference = 'Stop'
$stage = 'C:\src\tmp\m1\widevine'
$chromium = 'C:\src\chromium\src'
$archive = Join-Path $stage 'oimompecagnajdejgnnjijobebaeigek_4.10.3112.0_win64_a5xadnsxzavp7cn4ay5kc6gq6u.crx3'
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne `
    '1090c5d6881985c042370d506e15f1db1d0a88bfb7e9602a9202fac421098eb3') {
  throw 'Staged CRX3 SHA-256 mismatch'
}
$inventory = Get-Content -LiteralPath (Join-Path $stage 'unpack-verification.json') -Raw | ConvertFrom-Json
foreach ($file in $inventory.Files) {
  $path = Join-Path (Join-Path $stage 'cdm') $file.Path
  if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.Sha256) {
    throw "Staged file SHA-256 mismatch: $path"
  }
}
$dest = Join-Path $chromium 'third_party\widevine\cdm\win\x64'
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item -LiteralPath (Join-Path $stage 'cdm\manifest.json') -Destination (Join-Path $dest 'manifest.json')
Copy-Item -LiteralPath (Join-Path $stage 'cdm\_platform_specific\win_x64\widevinecdm.dll') -Destination (Join-Path $dest 'widevinecdm.dll')
Copy-Item -LiteralPath (Join-Path $stage 'cdm\_platform_specific\win_x64\widevinecdm.dll.sig') -Destination (Join-Path $dest 'widevinecdm.dll.sig')
```

The `.sig` is preserved as supplied; the configuration below does not consume it.
There is no placeholder to replace, and no required `.gitignore` or `BUILD.gn`
edit. Inspection found no files or README/.gitignore under `cdm/win/`, and no
`win/x64/` directory. A private local ignore rule can be added separately if
desired; it has no build effect. No diff is supplied because no source edit is
needed. Runtime hook guards do not apply: this task adds no C++ hooks.

Keep `third_party/widevine/LICENSE` as-is. The pin's copy target uses that existing
file (`../LICENSE`), not `cdm/win/x64/LICENSE` or the archive's root LICENSE.
The complete downloaded LICENSE remains in staging. `_metadata` is not an input
to the bundled CDM copy targets.

## 3. GN args for both dev and rel

Add/update these entries in **both** `out/dev/args.gn` and `out/rel/args.gn` when
integration is allowed. Preserve the profiles' other args; do not append duplicate
assignments. `out/dev/args.gn` was inspected; `out/rel/args.gn` does not exist at
this pin's current checkout and must be created from the project's release
profile separately.

```gn
target_cpu = "x64"
enable_widevine = true
enable_library_cdms = true
bundle_widevine_cdm = true
enable_cdm_host_verification = false
```

| Setting | Reason |
| --- | --- |
| `target_cpu = "x64"` | Matches the staged AMD64 binary and selects `win/x64` inputs. |
| `enable_widevine = true` | Unbranded desktop Chromium defaults this to false. |
| `enable_library_cdms = true` | Enables the library CDM utility service; already defaults true on Windows Views. Explicit here to pin the requirement. |
| `bundle_widevine_cdm = true` | Unbranded builds default this to false even after enabling Widevine; turns on the existing copy targets. |
| `enable_cdm_host_verification = false` | Already defaults false for unbranded Windows. Keep it false for this unsigned host configuration; avoids requesting unavailable host verification signatures. |

Keep `is_chrome_branded = false` and `is_chrome_for_testing_branded = false`.
Branding does not need to be changed to bundle Widevine. No `widevine_root`
override is necessary (`"."` is correct).

For Windows x64 with those args, the computed values are:

```text
library_widevine_cdm_available = true
enable_library_widevine_cdm = true
enable_widevine_cdm_component = true
enable_widevine_cdm_host_verification = false
```

These four are derived variables, **not GN args**. In particular, do not add
`enable_widevine_cdm_component` to args.gn. Leave component registration enabled:
Windows uses it to discover and register a preinstalled bundled CDM.

`ignore_missing_widevine_signing_cert` defaults to `!is_official_build`. No
override is needed with host verification disabled, including an official
release configuration. Setting it true suppresses a missing-certificate build
failure; it does not create a valid VMP signature. Enabling host verification
would also reference `third_party/widevine/scripts/signature_generator.py`,
which is absent from this public checkout, so that is not a supported recipe here.

For H.264/AAC streaming sites, keep the existing dev settings in rel too:

```gn
proprietary_codecs = true
ffmpeg_branding = "Chrome"
```

These select playback codec support; they do not change product branding or
provide Widevine by themselves. A VP9/WebM EME probe below avoids requiring
H.264 to test basic key-system availability. `rtc_use_h264` is unrelated to CDM
discovery and does not need a Widevine-specific change.

## 4. Existing targets and output layout

After integration, the normal build owner can regenerate and build the profiles:

```powershell
# Documentation only; not executed by this staging task.
Set-Location -LiteralPath 'C:\src\chromium\src'
gn gen out/dev
autoninja -C out/dev chrome
gn gen out/rel
autoninja -C out/rel chrome
```

`//chrome:chrome` reaches `//chrome:chrome_initial` (`chrome/BUILD.gn:134-141`),
which already has `//third_party/widevine/cdm` in its data deps
(`chrome/BUILD.gn:163,393`). The directory's default `:cdm` group depends on
`:widevine_cdm_binary` and `:widevine_cdm_manifest_and_license`
(`third_party/widevine/cdm/BUILD.gn:54-78`). No new GN dep is required.

The resulting loose dev build contains:

```text
out/dev/chrome.exe
out/dev/WidevineCdm/manifest.json
out/dev/WidevineCdm/LICENSE
out/dev/WidevineCdm/_platform_specific/win_x64/widevinecdm.dll
```

The rel tree has the same paths under `out/rel/`. With host verification false,
the DLL's `.sig` is not copied by these targets. Copy/distribute the entire
`WidevineCdm` output directory with the browser, preserving this layout.
The Windows mini-installer file list already names this layout
(`chrome/installer/mini_installer/chrome.release:95-102`). For an installed
Windows build the bundle belongs alongside the versioned Chrome module/assets,
which can differ from the outer launcher executable directory.

On Windows, `chrome/app/chrome_main_delegate.cc:1589` maps preinstalled
components to `chrome::DIR_COMPONENTS`. That resolves to `base::DIR_ASSETS`
(`chrome/common/chrome_paths.cc:106-122,292`), normally the Chrome module's
directory (`base/base_paths.cc:68-75`). ComponentInstaller reads
`WidevineCdm/manifest.json`, verifies the DLL and ABI, and invokes ComponentReady.
The Widevine installer then registers the DLL dynamically. A higher-versioned
user-data component can supersede the bundle, so use a fresh test profile and
inspect the loaded path when verifying the bundled copy.

## 5. Compatibility and verification

| Manifest field | Downloaded value | Pin accepts | Result |
| --- | --- | --- | --- |
| `version` | `4.10.3112.0` | Valid component version | Matches DLL version |
| `minimum_chrome_version` | `68.0.3430.0` | Running Chrome `157.0.8103.15` | Version floor met |
| `x-cdm-module-versions` | `4` | `CDM_MODULE_VERSION = 4` | Compatible |
| `x-cdm-interface-versions` | `10` | 10 and 11 enabled; 12 implemented but disabled by default | Compatible |
| `x-cdm-host-versions` | `10,11` | 10 through 12 | Compatible |
| `accept_arch` | `x64,x86_64,x86_64h` | Windows x64 | Compatible; PE is AMD64 |

The pin's checks require at least one compatible version in each manifest list
(`components/cdm/common/cdm_manifest.cc:97,287`), rather than requiring every
listed version. CdmWrapper tries enabled interfaces in descending order and
falls back to 10 (`media/cdm/cdm_wrapper.h:319,349`). No interface override switch
or API patch is needed. This is a static compatibility result, not evidence of
successful DLL initialization or license acquisition.

Once the rebuilt browser is available, visit an HTTPS page or localhost page in
the **cloud Chromium browser** and run this in that page's DevTools console (or
through the existing automation bridge). Running it in the viewer's browser
would test the viewer's CDM instead. Use a fresh profile, allow startup component
registration to complete, and retry if called immediately after startup.

```javascript
const access = await navigator.requestMediaKeySystemAccess('com.widevine.alpha', [{
  initDataTypes: ['cenc'],
  videoCapabilities: [{
    contentType: 'video/webm; codecs="vp9"',
    robustness: 'SW_SECURE_CRYPTO'
  }],
  distinctiveIdentifier: 'not-allowed',
  persistentState: 'not-allowed',
  sessionTypes: ['temporary']
}]);
console.log(access.keySystem, access.getConfiguration());
const mediaKeys = await access.createMediaKeys();
console.log('Widevine MediaKeys created', mediaKeys);
```

`requestMediaKeySystemAccess` succeeding confirms that the requested configuration
is advertised. `createMediaKeys` also exercises CDM creation. An actual authorized
encrypted stream and successful license response are still needed to test
decryption/playback. Persistent-license sessions are not supported by this
manifest. HTTPS/localhost and the page/frame's encrypted-media permission policy
matter; avoid probing from an arbitrary insecure or restricted iframe.

Open `chrome://media-internals` in that same browser while exercising an encrypted
stream. Check the player's key system, CDM/decoder events, pipeline errors and
license/decode outcome. For registration/loading diagnostics, launch through the
usual PRTS launcher with these additional logging flags and inspect its log:

```text
--enable-logging --vmodule=widevine_cdm_component_installer=2,component_installer=1,cdm_module=1,cdm_adapter=1
```

Some CDM loading messages use DVLOG and need the DCHECK-enabled dev profile;
process module inspection can confirm the loaded DLL path in rel.
The expected bundled library path ends in
`out/dev/WidevineCdm/_platform_specific/win_x64/widevinecdm.dll` (or rel).
Registration and load logs distinguish that from a user-data update. Do not use
`--disable-component-update` as an offline verification shortcut: the pin skips
`RegisterComponentsForUpdate()` when that switch is present
(`chrome/browser/chrome_browser_main.cc:1957-1960`). Windows still needs that
component registration path. Network isolation of a fresh test profile
can establish whether the already packaged files are sufficient.

**Correction to the task premise:** `chrome://components` is an updater registry
view, not a definitive inventory of CDMs that can load. At this pin there is no
blanket filter hiding bundled components: its handler enumerates component IDs,
and Widevine's updater registration also accepts preinstalled bundles. A bundled
Widevine entry can therefore appear after registration/state publication. Its
presence does not prove the DLL was loaded; an absent entry alone does not prove
the DLL is missing. Use EME, loaded-path logs, and media-internals as above.

## 6. VMP and remaining runtime risks

This custom chrome.exe/chrome.dll has no Google-issued Widevine VMP host
signatures. The archive's `widevinecdm.dll.sig` covers the supplied CDM, not our
browser. Keep `enable_cdm_host_verification = false` for this configuration;
turning verification off avoids the host-signing build path but does not satisfy
a license server's VMP policy or grant a higher robustness level. Some services
can reject licenses or restrict quality. EME discovery can pass while a service
still refuses playback. VMP enforcement and site-specific acceptance remain
untested in this staging task.

Further runtime checks are required for Windows/headless CDM sandbox startup,
codec support, real license delivery, and the PRTS capture path. Protected output
restrictions can affect capture; a successful ordinary playback test alone does
not establish that the remote captured video is available. No CDM runtime binary
was executed during staging.

Brave's own documentation describes redirecting its Widevine component update
checks to Google, and its source registers Widevine and requests installation
after enablement. That supports using Google's updater as the binary source;
the bundle here uses Chromium's existing preinstalled-component machinery.
See [Brave's Widevine integration notes](https://raw.githubusercontent.com/brave/brave-core/master/browser/widevine/README.md)
and [Widevine enablement implementation](https://raw.githubusercontent.com/brave/brave-core/master/browser/widevine/widevine_utils.cc).

See `NOTES.md` for the complete pinned source citation map and staging file list.
