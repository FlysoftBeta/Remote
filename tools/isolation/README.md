# Isolation probes

Mode, provisioning and verified results: [docs/isolation.md](../../docs/isolation.md).
`grant-lpac-acl.ps1` grants shared package RX to immutable browser binaries;
`grant-traversal.ps1` grants directory-only RX without recursive propagation.
Neither helper should grant shared inherited package access to tenant data.

Build the standalone native canary and its no-op hook DLL:

```powershell
.\tools\isolation\build-probe.ps1 -OutDirectory C:\src\tmp\isolation-probe
```

The probe writes its result to `--probe-output=<file>`. Run it through the same
`re --chrome=<probe.exe> --isolation=lpac --job-ui-limits=all --user-data-dir=<A>`
path as the browser; forward probe flags with `--cr-`, for example
`--cr-probe-output=<A>/probe.txt`. It deliberately does not implement Mojo;
short-lived probes exit and `re` reports the expected browser disconnect.

Use only controlled canaries. `--probe-serve=1 --probe-title=<unique title>`
creates a window for 45 seconds and reports its HWND and thread ID. It attempts
to publish a fixed clipboard canary, replacing the clipboard if permitted.
For a second container, pass `--probe-title`, `--probe-hwnd`, `--probe-thread`,
`--probe-desktop` (from the serving result) and `--probe-hook=<probe-hook.dll>`.
It checks FindWindow, PostMessage, a thread-specific no-op WH_GETMESSAGE hook,
desktop access and clipboard reads. `--probe-read=<B>/canary.txt` checks a
dedicated file. First run an ordinary host positive control with the same
arguments to establish valid HWND/thread/hook/file parameters.

`--probe-process=<owned PID>` performs a read-only host-side token inspection.
It reports AppContainer SID and Chromium's observed `WIN://NOALLAPPPKG` opt-out
attribute (UINT64 or string value 1); this is an internal Windows contract.
`--probe-launch=<dev chrome.exe>` reproduces original Chromium desktop/job
failures from a controlled outer job, without the broker-desktop hook. The
child inherits that job and has a 10-second timeout. Desktop failures precede
normal logging; use CDB on CreateDesktopW's return to retain GetLastError.
