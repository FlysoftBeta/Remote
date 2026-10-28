# viewer

The React + TypeScript viewer is built by Vite into `viewer/dist`, served by the [control-plane](control-plane.md), and targets Chrome 89+. Run root `corepack pnpm install` / `corepack pnpm build`; `corepack pnpm dev` provides live development on port 5173. `probe.html` remains a separate codec probe.

## Accounts

Sign in or create an account. A server credential field appears during registration only when the CP requires it; it is never needed for login. The first user is admin. Passwords go to the CP for salted scrypt hashing; the browser remembers only the returned session token in IndexedDB, scoped to the server. Sign out removes it and revokes the session. Existing sessions survive CP restarts and expire after 30 days; password reset revokes all sessions.

Normal users see and manage their own instances/profiles. The Profiles page creates/deletes unused profiles; deleting an instance preserves its profile. Administration lists/creates/deletes users, resets passwords and manages all instances/profiles. Remove a user's resources before deleting the account; the last admin is retained.

## Connect screen

Open the viewer, pick an instance and choose **Connect**. The screen follows the system light/dark preference and works at phone width.

- **Connection** — the instance list comes from `GET /api/instances` (authenticated with the remembered session) and shows each instance's name, ID, state badge (`running`, `stopped`, `starting`, `backoff`, `crashed`) and whether another viewer is using it. Use the refresh button to reload it; it also reloads when the control plane changes. The **Instance** field stays editable: an ID from the URL or typed by hand is kept even when the server does not list it. Empty means `default`. A rejected session or unreachable server is explained under the list. **Fullscreen on connect** (on by default) also requests landscape orientation where supported. Connecting to a listed instance that is `stopped` or `crashed` starts it first (`POST /api/instances/:id/start`).
- **Manage instances** (sliders button next to refresh) — start, stop, restart or delete the selected instance, and create a new one (ID, optional name, a new or existing profile, isolation mode, start-with-server). Creating posts `/api/profiles` when a new profile is chosen, then `/api/instances`, then starts it. Uses the logged-in session; errors from the API are shown inline. Deleting an instance keeps its profile data.
- **Display & network** — stats overlay, receive delay (lowest = 0 ms, custom milliseconds, or browser default), presenter (Standard / Clean / Canvas) and an alternative control plane.
- **Diagnostics & measurement** (developer) — latency measurement (off, pixel marker, or a timed field run), scenario label, report/warm-up durations, marker row, synthetic input, field test panel, decoder info, relay clock trace and relay URL. These are for testing and are not needed for normal use.

Sections open automatically when the URL or saved settings already use one of their options. Non-secret settings are saved in localStorage. Session tokens live only in IndexedDB/in memory; passwords and server credentials are not saved. URL parameters override saved values. When a session ends with an error the screen shows the reason with **Retry**.

The viewer uses the [control-plane API](control-plane.md) for account, instance and profile management.

## Session

The remote video fills the viewport, letterboxed as needed. A small handle at the top centre opens an auto-hiding toolbar: connection dot and instance ID, **Stats**, **Keyboard** (touch devices; reopens the soft keyboard for a focused remote text field), **Fullscreen** and **Disconnect**. The toolbar also appears briefly after connecting and when the mouse rests at the top edge; it hides after a few seconds or when you touch the remote page. Toolbar buttons never take keyboard focus from the remote page.

A status chip at the bottom shows *Connecting*, *Waiting for the remote browser*, *Starting video*, *Reconnecting* or *Tap to start video* (tap it) and disappears once video plays. The round keyboard button at the bottom right appears when the remote page wants the soft keyboard but the browser could not open it automatically.

The stats overlay (top left) shows fps and resolution, then bitrate, codec/decoder, RTT, jitter buffer, decode time, loss counters, the receiver hint and, when available, clock sync, latency percentiles and sender stats. Toggle it from the toolbar or with **Ctrl+Alt+S**; `stats=1` shows it initially.

## URL parameters

The form edits exactly these query parameters; unrecognised parameters are preserved for external tools.

| Parameter | Values | Control |
|---|---|---|
| `instance` | instance ID, default `default` | Instance list / field |
| `token` | session token for tools; consumed into IndexedDB and removed from the URL | - |
| `cp` | `host:port` | Control plane |
| `relay` | TURN URL, e.g. `turn:host:port` | Relay URL |
| `connect` | `1` connects immediately, skipping the settings screen (used by tools); removed on disconnect | — |
| `stats` | `1` shows the overlay | Show stats overlay |
| `delay` | milliseconds, or `off`; absent = 0 | Receive delay |
| `presenter` | `clean` or `canvas`; absent = standard | Presenter |
| `measure` | `1` pixel marker; `5`–`3600` seconds = field run | Latency measurement |
| `scenario` | label | Scenario |
| `report` | seconds; POSTs a summary to `/api/report` | Report after |
| `warmup` | seconds, default 3 | Warm-up |
| `my` | source pixel row; default auto-detect | Marker row |
| `autoinput` | `1` synthetic mouse moves every 97 ms | Synthetic input |
| `field` | `1` field test panel | Field test panel |
| `hwinfo` | `1` request media permission to expose decoder details | Decoder info |
| `trace` | `1` relay clock trace (needs `relay`) | Relay clock trace |

Switches write `1` when on and remove the parameter when off. The receiver prefers H.264, with VP8 as fallback; there is no codec-preference parameter. The optional [field overlay](tools.md#field-test) provides its own measurement controls when field mode is enabled.

## Files

`src/main.tsx` owns accounts/admin UI and wraps the preserved `src/session-shell.html`. `settings.js` runs the connect screen; `session-ui.js` the toolbar, status chip and stats toggle; `viewer.js` signalling, the peer connection and stats. `input.js` captures mouse, touch, pen, wheel and keyboard input with letterbox-aware coordinate mapping. `ime.js` bridges the remote soft keyboard and IME. `protocol.js` handles binary input; `presenter.js` contains opt-in presentation experiments. Receiver latency hints and codec selection are described in [protocol](protocol.md#5-receiver-latency-hints-chrome-89). Element IDs used by tools (`video`, `stage`, `status`, `overlay`, `kbd`, `ime`, `ime-password`, `settings-form`, `connect`, `disconnect`) are stable.

The latency-sensitive classic scripts are copied unchanged into the Vite output (only signalling token lookup changes). React does not rerender the media/input DOM. `/tools/*`, `?connect=1`, `window.rk`, and `window.viewerFinish` remain available to existing harnesses.
