# control-plane

A Node.js (22+) sample in `control-plane/`: static file server (viewer, `tools/`), WebSocket signalling relay, session manager and the
stdio transport to `re`. It is **not** on the media or input path and exists for debugging and acceptance. Decision: [decisions](decisions.md) D13.

- **Run**: `node control-plane [--port 8080]` (`npm --prefix control-plane ci` once). Environment: `CP_TOKEN` (auth placeholder, close
  code 4401), `ENDPOINT_PATH` (spawn `re.exe` instead of waiting for a WS endpoint), `ENDPOINT_ARGS` (space separated) or
  `ENDPOINT_ARGS_JSON` (array, for paths with spaces), `CP_DATA_DIR` / `--data-dir` (default `<repo>/.data`), `PORT`,
  `FIELD_REPORT_DIR` / `FIELD_CONFIG_JSON` (field-test kit, see [tools](tools.md#field-test)).
- **Files**: `index.js` (HTTP API + WS `/signal`, signalling table `instance -> {endpoint, viewer, sid}`, whitelist per direction),
  `manager.js` (durable instance/profile configuration, lifecycle and per-instance restart backoff),
  `endpoint.js` (`spawnEndpoint`: spawns `re.exe --instance=<id> ...`, stdin/stdout framing, stderr inherited as the log),
  `framing.js` (4-byte LE length + JSON, <= 4 MiB; `test/framing.test.js`).
- **Behaviour**: one controlling viewer per instance (a new one replaces the old with `bye reason:"replaced"`); a new endpoint replaces the
  old; `error:no-endpoint` while none is online; a dropped side makes the CP synthesise `bye`; ping 15 s / timeout 30 s; WS messages <= 64 KiB.
  The CP supervises one `re` per managed instance; `re` launches its own rb browser. Existing WS endpoint registration remains supported.
- Message set and lifecycle: [protocol](protocol.md#1-signalling-websocket-json).

## Instances and profiles

Clients select an instance with `?instance=<id>`; an omitted parameter still selects `default`.
External WS endpoints and explicit instance URLs continue to work. Viewer/settings UI belongs to the separate viewer redesign task.

Management routes use the same `CP_TOKEN` secret as signalling: supply `Authorization: Bearer <token>` or `?token=<token>`.
With no configured token they are unauthenticated, as is signalling. Mutations reject a mismatched browser `Origin`.
JSON request bodies are limited to 64 KiB. Successful reads/actions/PATCH return 200, creation 201, deletion 204;
invalid input returns 400, missing resources 404, conflicting profile/configuration/state 409, and missing/wrong tokens 401.

| Method | Route | Body / behavior |
|---|---|---|
| GET | `/api/instances` | Array of instance configuration and status objects |
| POST | `/api/instances` | Create configuration; requires `profile`; optional `id`, `name`, fields below. Initially stopped |
| GET | `/api/instances/:id` | Configuration plus current status |
| PATCH | `/api/instances/:id` | Partial configuration; `id` immutable; stop before changing launch settings |
| DELETE | `/api/instances/:id` | Stop and remove configuration; keep profile data |
| POST | `/api/instances/:id/start` | Launch (idempotent while active); requires `ENDPOINT_PATH` |
| POST | `/api/instances/:id/stop` | Cancel restart, flush/close stdin for graceful EOF shutdown, then kill tree after 3 s if needed |
| POST | `/api/instances/:id/restart` | Stop then start |
| GET | `/api/profiles` | Array: `id`, `name`, `userDataDir`, `managed`, referencing `instances` |
| POST | `/api/profiles` | Create directory; optional `id`, `name` (generated ID when omitted) |
| DELETE | `/api/profiles/:id` | Remove profile directory and record; refuse any instance reference, even stopped |

Instance fields:

| Field | Meaning / default |
|---|---|
| `id`, `name` | ID: 1..64 lowercase ASCII letters/digits/underscore/hyphen, starts alphanumeric; generated UUID if omitted. Name defaults to ID |
| `profile` | Existing profile ID. Multiple configurations can reference it, but only one may run or reserve it for crash restart |
| `isolation` | `off`, `job` (new managed instance default), `lpac`, `appcontainer`. Experimental modes retain re's existing restrictions; see [isolation](isolation.md) |
| `jobLimits` | Optional object: `memoryMB` (1..1048576, whole-job committed memory), `cpuRate` (1..10000, 0.01% units of all system CPUs, hard cap), `processCount` (1..65535, whole-job active processes). Omitted values mean no added cap |
| `jobUiLimits` | Comma-separated re UI-limit names, default `none`; see isolation docs for current pipeline limitations. Limits require isolation other than `off` |
| `reArgs` | Array of `--switch`/`--switch=value` arguments, default `[]`. Identity, profile, isolation and job-limit arguments are managed fields |
| `chromeArgs` | Array of Chrome switches, default `[]`; CP converts `--foo=value` to re's `--cr-foo=value`. Profile override forbidden |
| `autostart` | Boolean, default false. Start on CP boot; creation/PATCH alone does not start or stop an instance |

All instances use the runtime `ENDPOINT_PATH`; absent `--chrome` in `reArgs`, CP selects `chrome.exe` beside that endpoint.
`name` and `autostart` can change while running. PATCH replaces each supplied object/array, including the complete `jobLimits` object.
Status adds `userDataDir`, `state` (`stopped`, `starting`, `running`, `crashed`, `backoff`), `pid`, `restarts`, `lastExit`
(`code`, `signal`, `error`, `at`), `nextRestartAt`, and `viewerConnected`. Running means re has sent its valid protocol hello;
viewer connected means a controlling signalling socket is present, not that media is decoding. Runtime status/counters reset with the CP.
`crashed` denotes an instance that could not be started; a retryable process exit immediately moves through it into `backoff`.

For a future settings-screen picker, `GET /api/instances` returns the same full objects as the single-instance GET route.
Use `id` as the selection value and `name` as the display label; `state`, `viewerConnected`, `profile` and `isolation` are available
for status/details. Preserve the current URL's other parameters (especially `token` and `cp`) when changing `instance`.
The route lists managed configurations, including stopped instances; externally registered WS-only IDs are not added to this list.
Keep an explicit current URL selection even if it is absent from the response. No viewer implementation is required by this API.
For example, a list entry includes:

```json
{"id":"work","name":"Work browser","profile":"work","isolation":"job","autostart":true,"jobLimits":{"memoryMB":4096,"cpuRate":5000,"processCount":64},"jobUiLimits":"none","reArgs":[],"chromeArgs":[],"userDataDir":"C:\\remotekit-data\\profiles\\work","state":"running","pid":1234,"restarts":0,"lastExit":null,"nextRestartAt":null,"viewerConnected":false}
```

Each unexpected exit retries indefinitely with exponential delays 1, 2, 4, 8, 16, then 30 s (cap). A 60 s run resets the failure streak.
`restarts` counts automatic retry attempts during the current CP lifetime; explicit restart actions do not increment it.
A 90 s startup timeout kills a stalled endpoint and retries. Stop/delete/shutdown cancel timers before closing the process.
CP SIGINT/SIGTERM stops all owned endpoints; on forced CP death its pipes close, and re exits on EOF/broken pipe.
re's browser job keeps kill-on-close enabled even with resource limits, so endpoint death reclaims its browser tree.

Data layout (set `CP_DATA_DIR` to e.g. `%LOCALAPPDATA%/remotekit` to move it outside the checkout):

```text
<data-root>/instances.json       version:1, profiles:[], instances:[] (configuration only)
<data-root>/profiles/<id>/       Chrome user-data directory, retained after instance deletion
```

Configuration changes write a unique sibling temporary JSON file, flush it, then atomically rename over `instances.json`.
A malformed or unsupported file fails CP startup without overwriting it. Use one control-plane writer per data root; there is no
cross-process lock or multi-host coordination. Profiles in a managed root must not be replaced with junctions/symlinks.

On the first boot of a fresh data root, CP creates `default` and imports `ENDPOINT_ARGS[_JSON]`. It autostarts when `ENDPOINT_PATH` is set;
legacy isolation defaults to `off`, and an explicit old `--user-data-dir` is preserved as an external, `managed:false` default profile.
External profile deletion is refused (remove it manually). Later boots load saved configuration; change default arguments via PATCH,
or select a fresh data root to import a different legacy command. `ENDPOINT_PATH` remains a runtime setting on every boot.
Harness-owned CP launches in `tools/latency` and `tools/smoke` use their own temporary roots so concurrent runs do not share state.

Example request bodies, using the routes above:

```json
{"id":"work","name":"Work profile"}
```

```json
{"id":"work","name":"Work browser","profile":"work","isolation":"job","jobLimits":{"memoryMB":4096,"cpuRate":5000,"processCount":64},"chromeArgs":["--lang=en-US"],"autostart":true}
```

Create the profile, create the instance, then POST `/api/instances/work/start`. To remove both, DELETE the instance before its profile.
Run unit/API tests with `npm --prefix control-plane test`. The short real multi-instance harness is
`node tools/multi/smoke.mjs --out C:/src/tmp/m8/multi/run` (Windows, built dev re/chrome and installed Edge required).
It uses a test receiver under `tools/multi/`, independent of the product viewer UI; it does not take latency measurements.

## TLS

HTTPS and WSS are enabled when a certificate is configured; the viewer switches to `wss://` automatically when the page is served over HTTPS.

| Option | Env | Meaning |
|---|---|---|
| `--tls-cert <pem>` + `--tls-key <pem>` | `TLS_CERT`, `TLS_KEY` | PEM certificate (chain allowed) and private key |
| `--tls-ca <pem>` | `TLS_CA` | optional extra CA/intermediate chain |
| `--tls-pfx <pfx>` | `TLS_PFX`, `TLS_PFX_PASSPHRASE` | PKCS#12 bundle instead of PEM |
| `--http-redirect-port <n>` | `HTTP_REDIRECT_PORT` | also listen on plain HTTP and 301-redirect to HTTPS |

The certificate files are watched and reloaded without a restart when they change (e.g. after renewal); a failed reload keeps the old certificate. Example:

```
node control-plane --port 443 --tls-cert cert.pem --tls-key key.pem --http-redirect-port 80
```

The endpoint's stdio link (`ENDPOINT_PATH`) is unaffected.
