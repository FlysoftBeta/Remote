# PRTS for Windows

PRTS runs a cloud browser and streams its video and input over WebRTC. This
portable folder contains rb (Remotekit Browser), re (Remotekit Endpoint), a
Node.js control plane, and the web viewer. Node.js 24.22.1 LTS is bundled;
no separate Node.js or npm installation is needed to run it.

Requirements: Windows 10/11 x64, a current Intel graphics driver and Intel iGPU
with oneVPL support. NVIDIA NVENC is untested. The endpoint can fall back to
the built-in OpenH264 software encoder. Use Chrome 89+ or Edge for the viewer
(a current browser version is recommended).

## Quickstart

1. Extract the entire archive into a writable directory.
2. Run `start.cmd`, or `powershell -ExecutionPolicy Bypass -File .\start.ps1`.
3. Open the printed URL, normally <http://localhost:8080/>.
4. Press Ctrl+C in the launcher console to stop.

Use `start.cmd -Port 8090` to change the port. PowerShell accepts additional
endpoint arguments, for example `./start.ps1 -EndpointArgs @('--fps=30')`;
supported arguments depend on the endpoint version.

The default listen address is 127.0.0.1. For access from another computer use
`start.cmd -BindAddress 0.0.0.0` and open `http://<server-ip>:8080/` there.
The control plane is a development service with placeholder authentication;
restrict access to trusted networks. Internet deployment needs TLS, access
control and appropriate WebRTC network configuration.

Packages assembled before re is available print a warning and run only the
control plane. `start.cmd -ControlPlaneOnly` explicitly selects that mode.

## Known limitations

- Release qualification and the final limitations list are pending.
- Widevine is bundled under its own terms (see LICENSES). Some services reject
  playback because this browser has no Google VMP signature; software L3 only.
- NVIDIA encoding and streaming from a clean Windows installation are unverified.
- Do not move individual DLLs out of their bundled directories.

See `BUILD-INFO.json`, `FILES.sha256` and `LICENSES/` for build inputs, file
integrity and third-party notices. If an archive is split, concatenate its
numbered parts in binary order before extracting; see the adjacent REASSEMBLE.txt.
