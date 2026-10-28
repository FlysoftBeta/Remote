# PRTS (remotekit) v0.2.2 for Windows

PRTS is a cloud browser: a patched Chromium 157 (rb) runs headless on the
server, a C++ endpoint (re) captures its frames on the GPU, encodes H.264 and
streams it over WebRTC to a React viewer that sends back mouse, keyboard, IME
and touch input. This portable folder contains rb, re, a Node.js control plane
(Node 24.22.1 bundled, no install needed), the viewer, and licenses.

## Requirements

- Windows 10/11 x64.
- Default encoder: Intel GPU with a current driver (oneVPL, low-latency H.264).
- NVIDIA NVENC backend is included but untested on hardware.
- OpenH264 software encoder is the automatic fallback (higher latency/CPU).
- Viewer: Chrome 89+ or Edge (any recent version recommended).

## Quickstart

1. Extract the whole zip to a writable folder (avoid very long paths).
2. Run `start.cmd` (or `powershell -ExecutionPolicy Bypass -File .\start.ps1`).
   On first run it grants read/execute on `rb\` to ALL APPLICATION PACKAGES and
   ALL RESTRICTED APPLICATION PACKAGES (needed by the Chromium sandbox).
3. Open the printed viewer URL, normally <http://localhost:8080/>, and create an
   account. The first account is admin; Administration manages users and all
   instances/profiles. Other users see only their own resources. Your browser
   remembers the session in IndexedDB. Start/connect from the instance list.
4. Press Ctrl+C in the launcher console to stop.

## Main switches

- `-Port 8090` viewer/control-plane port (default 8080).
- `-BindAddress 0.0.0.0` listen on all interfaces (default 127.0.0.1). Configure
  TLS below for remote use and arrange WebRTC network connectivity.
- `-DataDir C:\path\to\data` stores config.json and profiles outside this folder;
  otherwise CP_DATA_DIR or the bundled data directory is used. No database.
- Optional `CP_TOKEN` environment variable (or config.json serverCredential)
  gates account creation only. Default: none, registration open. Login uses a
  password; the CP stores salted scrypt hashes and hashed 30-day session tokens.
  Password reset revokes sessions. Deleting an instance retains its profile;
  delete unused profiles separately. Delete a user's resources before the user.
- `-EndpointArgs @('--fps=30')` extra re switches, e.g. `--fps=30`,
  `--encoder=auto|vpl|nvenc|openh264` (auto: Intel -> vpl, NVIDIA -> nvenc,
  else openh264), `--nvenc-preset=1..3`.
- `-ControlPlaneOnly` serve the viewer without the endpoint.

## Troubleshooting

- "Profile directory is already in use": stop the older remotekit launcher and
  close the browser using the reported profile, then explicitly start the instance.
  CP leaves the instance in `crashed` with an error instead of repeatedly launching.
  Do not delete profile data or a live lockfile. An abandoned writable lockfile
  does not prevent startup.
- Upgrades retain `instances.json` and migrate it once to `config.json`, preserving
  the default profile's legacy `externalPath`. Keep using the same `-DataDir`;
  after migration, `config.json` takes precedence. EndpointArgs are imported only
  when neither configuration file exists.
- "Sandbox cannot access executable": the ACL grant failed. Re-run start.ps1
  after deleting `rb\.acl-granted`, or run once from an elevated prompt.
- Black/no video: check the launcher console for the selected encoder
  (`encoder selected=...`). Update the Intel graphics driver or force
  `--encoder=openh264`.
- Choppy video with several sessions: lower `--fps` (see limitations).
- Viewer cannot connect remotely: allow the port in Windows Firewall and use
  `-BindAddress`.
- The endpoint is restarted automatically by the control plane (1/2/4/8/16/30 s backoff);
  the viewer renegotiates on its own.

## Known limitations

- Input-to-decoded latency P80 is above 100 ms on emulated WAN (122-138 ms).
- NVENC is untested on hardware.
- 3 sessions at 60 fps saturate an Intel UHD 630; use 30 fps for 3 sessions.
  There is no automatic fps back-off.
- A few undecodable frames can follow a GPU-process restart.
- Widevine L3 only, unsigned binary (no VMP); some license servers refuse it.
- Managed instances default to Job isolation. LPAC/AppContainer are experimental.
- English UI strings are auto-branded.

See `BUILD-INFO.json`, `FILES.sha256` and `LICENSES/`. If the archive is split,
concatenate the numbered parts in binary order (see REASSEMBLE.txt).

## HTTPS

```
.\start.ps1 -BindAddress 0.0.0.0 -Port 443 -TlsCert cert.pem -TlsKey key.pem -HttpRedirectPort 80
.\start.ps1 -BindAddress 0.0.0.0 -Port 443 -TlsPfx cert.pfx -TlsPfxPassphrase <pass>
```
Optional `-TlsCa chain.pem`. Certificates are reloaded automatically when the files change.
