# control-plane

A Node.js (22+) sample in `control-plane/`: static file server (viewer, `tools/`), WebSocket signalling relay, session manager and the
stdio transport to `re`. It is **not** on the media or input path and exists for debugging and acceptance. Decision: [decisions](decisions.md) D13.

- **Run**: `node control-plane [--port 8080]` (`npm --prefix control-plane ci` once). Environment: `CP_TOKEN` (auth placeholder, close
  code 4401), `ENDPOINT_PATH` (spawn `re.exe` instead of waiting for a WS endpoint), `ENDPOINT_ARGS` (space separated) or
  `ENDPOINT_ARGS_JSON` (array, for paths with spaces), `PORT`, `FIELD_REPORT_DIR` / `FIELD_CONFIG_JSON` (field-test kit, see [tools](tools.md#field-test)).
- **Files**: `index.js` (HTTP + WS `/signal`, instance table `instance -> {endpoint, viewer, sid}`, whitelist per direction),
  `endpoint.js` (`spawnEndpoint`: spawns `re.exe --instance=<id> ...`, stdin/stdout framing, stderr inherited as the log),
  `framing.js` (4-byte LE length + JSON, <= 4 MiB; `test/framing.test.js`).
- **Behaviour**: one controlling viewer per instance (a new one replaces the old with `bye reason:"replaced"`); a new endpoint replaces the
  old; `error:no-endpoint` while none is online; a dropped side makes the CP synthesise `bye`; ping 15 s / timeout 30 s; WS messages <= 64 KiB.
  There is no endpoint respawn policy and no supervisor: the CP owns one auto-launched endpoint, and `re` launches its own rb browser.
- Message set and lifecycle: [protocol](protocol.md#1-signalling-websocket-json).
