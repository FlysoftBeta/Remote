# Windows ancestor-directory access audit

Date: 2026-10-29. Source pin: Chromium 157.0.8103.15,
`1aa07b638799e59656559ae6c6649d4b197f555e`, with the local rb patch series.

## Scope and confidence

Static audit of the local checkout: direct Win32 long/short/final/full-path APIs,
their base wrappers and production callers, volume discovery, directory enumeration,
recursive directory creation, and directory watchers. First-party code was searched
outside out/tools/testing; third_party was additionally searched for direct path APIs.
The checkout contains broken Nearby header links, so this is not a guarantee of
complete build-transitive or runtime coverage. Source presence does not establish
that a call is linked into chrome.exe or reached in this configuration. OS DLLs,
COM, GPU drivers and runtime-loaded modules are not covered by a source search.

The initial audit made no Chromium implementation changes. Subsequent launch
experiments and the targeted patch are recorded below. Other candidate replacements
remain proposals; a complete individual replacement API/ACL matrix has not been run.

The observed `Write AppContainer path traversal DACL C:\Users: 0x5` came from
re's unconditional provisioning before browser launch. It demonstrates lack of
broker WRITE_DAC on C:\Users, not that Chromium actually attempted to list it.
The earlier successful launch under C:\src\tmp retained ancestor grants and
therefore cannot establish that those grants are necessary or unnecessary.

## No-ancestor launch experiment (2026-10-29)

Removed GrantPathTraversal and its profile/install call sites from re. Kept
profile/install grants and all Chromium path handling/no-execute checks intact.
Built through the standard `browser/cr.cmd build dev re` workflow (exit 0).

Used a newly generated profile/package SID under the ordinary user's Temp:
`C:/Users/Flysoft/AppData/Local/Temp/rk-no-ancestor-e689d874-705f-4e9e-8bd8-945012d626c4/profile`.
This was not the earlier ancestor-provisioned profile. Collected full DACL SDDL
snapshots for profile and install ancestors before/after both launches; none
changed. Existing host-wide shared grants were not removed, so this is not a
pristine-Windows ACL qualification.

| Check | First launch | Restart |
|---|---|---|
| Token AppContainer=1 LPAC=1, capabilities=5 | pass | pass |
| Browser Hello | pass, 24798 ms | pass, 852 ms |
| Browser remains running | fail, exit 1 | fail, exit 1 |
| Path guard | fatal DCHECK, file_util_win.cc:510 | same |

Both launches report `Cannot get long path for .../profile/Default/LocalStorage`.
This is IsPathSafeToSetAclOn's `DCHECK(!long_path.empty())`, not the best-effort
conversion after temporary-file creation. After the DCHECK, the directory
containment check also needs a meaningful normalized path; simply disabling
DCHECKs does not establish a valid fallback or correct no-execute protection.

Evidence: run-1.log, run-2.log and result.json in the parent of the profile path
above. These are local experiment artifacts, not committed logs. The standalone
runner is `C:/src/tmp/rk-no-ancestor-test.mjs`. No API replacement has been tested.
Conclusion: removing ancestor provisioning alone does not work for this dev
build's user-Temp profile. The no-execute path guard is now a demonstrated target
for a small, semantics-preserving replacement experiment.

### Follow-up: is this guard necessary?

The captured symbolic stack identifies the actual caller chain:
`storage::FilesystemImpl::OpenFile` -> `base::File::DoInitialize` ->
`PreventExecuteMappingInternal` -> `IsPathSafeToSetAclOn`.
FilesystemImpl adds `AddFlagsForPassingToUntrustedProcess` for an untrusted client;
the LocalStorage path is not merely a temporary-file display-name conversion.

Two protections have separate purposes: containment checking prevents accidentally
marking arbitrary host files non-executable; the subsequent deny FILE_EXECUTE ACE
protects writable files passed to an untrusted process. Both should be retained.

`base/files/file_win.cc:498` explicitly ignores PreventExecuteMapping's return
value. In a build without DCHECKs, a failed long-name conversion may therefore
leave the file open without applying the deny-execute ACE. This audit does not
establish exploitability, but release startup success alone cannot validate the
intended protection. The dev/rel GN configurations differ in dcheck_always_on.

The existing PreventExecuteMappingUnchecked API retains the deny-execute write
but omits the path check. Its PassKey currently permits only the child-process
launcher helper (for log files); extending this escape hatch to generic Storage
opens would broaden the contract. FilesystemImpl's lexical path scoping is not
automatically an equivalent validation of filesystem/reparse semantics.

Recommendation: if no ancestor ACL writes is a requirement, change the path-query
mechanism for this guard rather than removing it, disabling DCHECKs, or using the
unchecked entry point. First prove a minimal-rights handle-name query works under
the failing LPAC token. Then apply the same comparison representation to candidate
and allowed roots, preserving 8.3 handling and deliberate reparse semantics.
At this assessment stage, no global MakeLongFilePath replacement or Chromium code
patch had been made; the following section records the subsequent implementation.

### Follow-up patch and smoke result

Implemented `browser/patches/21-rb-noexecute-handle-path.patch`. Preserve the
existing long-name containment comparison; if either long-name conversion
fails, query BOTH the target and allowed root through FILE_READ_ATTRIBUTES
handles, full sharing and BACKUP_SEMANTICS. Compare NORMALIZED VOLUME_NAME_NT
results with a dynamic buffer. Failure to resolve/contain still returns false;
the subsequent deny-execute write remains intact. No global MakeLongFilePath
change, unchecked call, raw input fallback or Chromium feature disable was added.

Standard incremental `browser/cr.cmd build dev re` passed in 16 seconds (2 steps:
file_util_win.obj and base.dll). The complete patch series dry-run check reported
zero conflicts, including patch 21.

Fresh profile at
`C:/Users/Flysoft/AppData/Local/Temp/rk-no-ancestor-c028078a-2693-403e-970b-cbb9bde4d894/profile`:
first launch Hello 23326 ms; restart Hello 830 ms; both token checks passed,
no path-guard error, no timeout, exit code 0 after a controlled stdin EOF.
The browser remained alive for eight seconds after Hello on each run.
All recorded ancestor DACLs were unchanged. LocalStorage retained an explicit
Everyone deny FILE_EXECUTE ACE. Native opens of that directory with
FILE_READ_ATTRIBUTES succeeded (0), and explicit FILE_EXECUTE failed with
ERROR_ACCESS_DENIED (5).

Evidence: run-1.log, run-2.log and result.json beside that profile. This verifies
the demonstrated startup regression and the resulting deny-execute access;
it does not constitute full end-to-end browser feature, reparse-race, SMB or
cross-tenant qualification. Manual browser-function testing is the next step.

## Windows distinctions

* Ordinary file opens: the target DACL controls access. Parent FILE_TRAVERSE
  checking is distinct from FILE_LIST_DIRECTORY and depends on the effective
  token's bypass-traverse privilege. Measure the actual LPAC/restricted token;
  do not assume that an ordinary host token's behavior carries over.
* GetLongPathName: expands 8.3 components using filesystem information. Microsoft
  explicitly documents list/read/attribute permissions on path and parents.
  A path with no tilde is NOT a documented safe shortcut.
* GetShortPathName: reverse name conversion; may query parent directories too.
* GetFinalPathNameByHandle: queries a final name from an already-open object.
  NORMALIZED and OPENED have different contracts. On SMB, NORMALIZED explicitly
  queries path components and can fail for inaccessible components. DOS/GUID
  volume conversion can introduce additional namespace/mount-manager dependencies;
  do not assume all such failures are ancestor-directory failures.
* GetFullPathName / _wfullpath: lexical absolute-path construction, not an
  existence check, 8.3 expansion, or symlink/junction resolution.
* FindFirstFile[Ex] / ReadDirectoryChanges: real directory query/list operations.
  They are not metadata-only alternatives to a file-handle query.
* GetVolumePathName: discovers the actual volume mount point, including nested
  mount points. Taking the first three characters of a DOS path is not equivalent.

## First-party production inventory

Paths below are relative to C:/src/chromium/src. Line numbers refer to the audited
working tree and may move as local patches change.

| Location | API / behavior | Assessment |
|---|---|---|
| base/files/file_util_win.cc:1099 | MakeLongFilePath -> GetLongPathName twice | Central name-expansion wrapper; not safe to replace globally with lexical normalization. |
| base/files/file_util_win.cc:493,509,533 | IsPathSafeToSetAclOn expands candidate and allowed roots | Highest-priority caller for the writable-file no-execute guard. Used by PreventExecuteMappingInternal at 1504; failure returns false even though fatal logging is debug-oriented. Preserve the guard. |
| base/files/file_util_win.cc:823 | Temporary-file long-name conversion | Best-effort already: failure returns the successfully created temporary file and original path. Low priority for startup failure. |
| sandbox/policy/win/sandbox_win.cc:100,121 | GetShortNameModules expands loaded short-name modules | Conditional DLL-name blocklist handling; preserve short-name matching. Not a generic profile-path normalization call. |
| chrome/browser/profiles/profile_shortcut_manager_win.cc:233 | Compares expanded shortcut target and chrome executable | Could use open-handle file identity if the intended question is same object; verify hardlink semantics. Limited relevance to headless launch. |
| chrome/browser/win/conflicts/module_info.cc:50 | ConvertToLongPath for module inspection | Potential best-effort handle-path substitution; verify module key/metadata behavior. |
| base/files/file_util_win.cc:1001,1018 | NormalizeFilePath opens with FLAG_READ and gets NORMALIZED DOS name | Target rights are broader than metadata-only. Candidate for smaller open rights, but callers depend on resolved-path semantics. |
| mojo/public/cpp/platform/platform_handle_security_util_win.cc:40 | GetFinalPathNameByHandle default flags | DCHECK-only diagnostic helper inside handle protections. Do not remove the handle protection to silence name-query failure. |
| content/browser/gpu/gpu_data_manager_impl_private.cc:122,136 | ValidateFileHandle checks link count and final DOS path | Security validation of Intel cache file placement; OPENED/string fallback is not a justified replacement. |
| base/files/file_util_win.cc:1124,1146 | IsLink uses minimal FILE_READ_ATTRIBUTES + OPEN_REPARSE_POINT; FindFirstFile fallback | Existing good pattern for querying target reparse metadata. Fallback can require more directory access than primary path. |
| base/files/file_util_win.cc:942 | CreateDirectoryAndGetError recursively examines/creates parents | Stops at an existing directory. Broker precreation of managed trees can avoid probing outside tenant root. |
| base/files/file_enumerator_win.cc:147 | FindFirstFileEx | Genuine enumeration; scoped to enumerated directory, not inherently every ancestor. |
| base/files/file_path_watcher_win.cc:59,674 | FILE_LIST_DIRECTORY; walk up target parents until a watchable directory is found | Genuine ancestor access when target is unavailable. Precreate target or scope watcher to managed root; preserving missing-target/recreation semantics requires more work. |
| base/files/file_util_win.cc:1400 | GetMaximumPathComponentLength -> GetVolumePathName + GetVolumeInformation | Metadata/mount discovery. Could investigate GetVolumeInformationByHandleW with a suitably opened object; preserve existing whole-path limit calculation. |
| components/storage_monitor/volume_mount_watcher_win.cc:126 | GetVolumePathName | Mount/storage-monitor behavior; inspect whether required by headless deployment before adding a broker path. |
| chrome/app/chrome_crash_reporter_client_win.cc:228 | GetFullPathName | Lexical path handling; not evidence of parent directory reads. |
| chrome/credential_provider/gaiacp/gcp_utils.cc:774 | GetShortPathName | Credential-provider component, not normal browser profile/temp handling. |
| chrome/installer/util/delete_after_reboot_helper.cc:254 | GetShortPathName and callers | Installer/reboot deletion behavior; not a normal LPAC browser startup dependency. |
| chrome/installer/mini_installer/mini_installer.cc:532 | GetVolumePathName | Installer-only category. |
| remoting/host/setup/daemon_controller_delegate_win.cc:78 | GetFinalPathNameByHandle | Remoting host setup category. |

Additional MakeLongFilePath matches in the examined first-party tree are tests.
Tests and API declarations were excluded from the production inventory.

## Third-party coverage

* libc++ filesystem/posix_compat.h:344,348: NORMALIZED DOS final-name query;
  canonical/weakly_canonical-style functionality should not be assumed lexical.
* TFLite/XLA windows_file_system.cc:372: final-name query. Determine target linkage
  and feature reachability before treating this as a browser launch requirement.
* UnRAR filcreat.cpp:67-78: long/short name conversion during short-name collision
  handling. This is not automatically a startup path; preserve extraction semantics.
* SQLite os_win.c and amalgamations, FFmpeg wchar_filename.h, FlatBuffers and
  OpenXR contain GetFullPathName calls; these are lexical conversion matches.
* Perl/Mingw SDK headers contain declarations/emulation and build-tool code;
  these matches do not establish browser runtime use.
* Direct long/short/final-name searches in Blink, ANGLE, Dawn, Crashpad, PDFium
  and Skia produced no matches in the queried C/C++ source files. Indirect
  base-wrapper use, standard-library calls and OS/library internals remain possible.

## Replacement choices

1. **Lexical formatting only:** use existing MakeAbsoluteFilePath or an appropriate
   path helper. Cannot replace code that needs object existence, 8.3 identity,
   canonical case or resolved reparse points. Broker-normalized inputs do not
   prevent Chromium from querying those paths again.
2. **Same existing object:** open both objects with minimum required metadata
   rights and compare volume/file IDs (GetFileInformationByHandleEx FileIdInfo,
   or a supported older identity query). Keep handles alive during comparison.
   File ID equality does not establish directory containment, and deliberately
   treats hardlinks as the same object; assess whether that is intended.
3. **Resolved path of existing object:** introduce a purpose-specific helper.
   Open the actual target with FILE_READ_ATTRIBUTES (or the documented minimum
   required by the selected information class), full read/write/delete sharing,
   and BACKUP_SEMANTICS for directories. Query NORMALIZED final name. Test
   VOLUME_NAME_NT/NONE separately if DOS volume mapping fails. Do not combine
   independently resolved strings into a trusted path without validating volume,
   namespace, reparse and comparison semantics. Use a dynamic result buffer.
4. **Display-only opened spelling:** OPENED or FileNameInfo can be candidates.
   They are not a drop-in security-canonical path or containment proof.
5. **Actual ancestor enumeration/watch:** there is no metadata API substitution
   with identical semantics. Precreate the tenant tree, avoid watches outside it,
   or broker the required operation with a narrow contract.

Replacing MakeLongFilePath with a final-path API changes relative/absolute,
junction/symlink, 8.3 and path-prefix behavior. NormalizeFilePath is also not an
automatic substitute: it currently opens with read access and uses a fixed
MAX_PATH-based buffer. Review each caller rather than relabeling the helper.

## Smallest useful next experiment

* In an experimental build only, omit unconditional ancestor DACL provisioning;
  retain profile/install grants and all token/job/desktop/no-execute safeguards.
  Use a fresh tenant SID and a controlled tree whose ancestors have never been
  granted package access. Reusing a provisioned SID/tree invalidates the test.
* Trace GetLong/Short/FinalPathName, directory CreateFile/NtCreateFile requests,
  directory queries, and error returns with stacks. Record token, requested
  access, exact ancestor, caller and whether failure affects startup.
* First run a small API matrix: open target; GetLongPathName; GetFinalPathName
  NORMALIZED/OPENED with DOS/NT/NONE; FileNameInfo; identity queries. Compare
  actual LPAC tokens against host controls, with ancestors not listable by LPAC.
* Cover local NTFS, Unicode/spaces, actual 8.3 aliases, missing targets, junctions,
  hardlinks, sharing conflicts and long names. Add SMB only if supported.
* Patch only demonstrated blockers, starting with the no-execute path guard if
  reached. Preserve containment checking and deny-execute ACL application.
* Acceptance: profile under user Temp starts without ancestor DACL changes;
  temp/cache/download creation, restart, renderer/GPU launch, video and input
  pass; another tenant's controlled canary remains inaccessible; reparse-point
  and hardlink checks retain their intended rejection behavior.

## Primary documentation

* [File security/access, including traverse checking](https://learn.microsoft.com/en-us/windows/win32/fileio/file-security-and-access-rights)
* [GetLongPathNameW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getlongpathnamew)
* [GetShortPathNameW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getshortpathnamew)
* [GetFullPathNameW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfullpathnamew)
* [GetFinalPathNameByHandleW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew)
* [GetFileInformationByHandleEx](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getfileinformationbyhandleex)
* [GetVolumePathNameW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getvolumepathnamew)
* [FindFirstFileExW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw)
