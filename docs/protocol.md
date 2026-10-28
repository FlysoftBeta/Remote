# Protocol: Mojo, signalling, session, DataChannels

Version **v1**. Consistent with [decisions](decisions.md) D3, D7-D9, D13 and [video-pipeline](video-pipeline.md). Reference implementations:
`control-plane/`, `viewer/`, `tools/mock-endpoint/`, `tools/latency/`. Shared binary input codec: `viewer/protocol.js` (browser and Node).

## Overview
```text
viewer --WS /signal (JSON)--> control-plane <--WS /signal?role=endpoint (JSON)-- mock endpoint / remote endpoint
                              <--stdio, 4-byte LE length + JSON (same message set)-- re.exe (spawned by the CP)
viewer <======= WebRTC: H.264/VP8 video (re -> viewer) + 3 negotiated DataChannels =======> re
re <--Mojo (rb.mojom)--> rb (chrome.exe)
```
- The control-plane (CP) relays sessions and signalling only; it is not on the media or input path.
- **re is the offerer**: it decides the H.264 fmtp (`profile-level-id=42e01f;packetization-mode=1;level-asymmetry-allowed=1`) and the
  `playout-delay` RTP extension. VP8 follows H.264 as a software fallback. The viewer answers, putting H.264 first via
  `setCodecPreferences` while retaining VP8.
- All DataChannels are `negotiated: true` with fixed ids, created on both sides (no DCEP round trip; Chrome 89 supports it).

## Browser to re (Mojo)
The only definition is Chromium's `chrome/browser/rb/mojom/rb.mojom` (patch 02, extended by later patches); `viz.mojom.RbD3D11FrameFence`
comes from `services/viz/privileged/mojom/compositing/frame_sink_video_capture.mojom`. `re` uses the generated C++ bindings directly.

| Interface, direction | Methods |
|---|---|
| `RbBrowser`, re -> browser | `SetClient(pending_remote<RbBrowserClient>)`, then Hello and window state arrive |
| | `StartCapture(window_id, CaptureParams{max_fps,nv12}, pending_remote<RbFrameSink>)`, `StopCapture`, `RequestRefreshFrame` |
| | `ActivateWindow`, `Focus(focused)`, `NewWindow(url)`, `SetViewport(size_dip, dsf)` |
| | `Pointer(window_id, action/type/pid/x/y/buttons/button/mods/pressure/radius/timestamp)`, `Wheel(...)`, `Key(window_id, down/mods/repeat/key_code/code/key/timestamp)`, `ResetInput()` |
| | `ImeCommit(text,context)`, `ImeCompose(text,cursor,context)`, `ImeDelete(before,after,context)`, `ImeHidden(context)`, `SetTouchCapabilities(points,touch_only)` |
| `RbBrowserClient`, browser -> re | `Hello(version,gpu_luid,software_compositing)` |
| | `WindowAdded` / `WindowChanged` (id, DIP/px size, DSF, title), `WindowRemoved(id)`, `WindowActivated(id)` |
| | `CursorChanged(css)`, `ImeStateChanged(seq,focused,type,mode,flags,caret_norm,reason,composing,context)`, `VirtualKeyboard(show,seq)` |
| `RbFrameSink`, browser -> re | `NewBuffer(buffer_id, handle<platform> texture, coded_size)`: first appearance of a D3D11 shared **NT handle** |
| | `OnFrame(FrameInfo) => (bool consumed)`: buffer id, frame seq, coded/visible/content geometry, capture begin/end/reference TimeTicks, optional ready fence |
| | `Reset()`: ordered pool boundary, also used for Viz/GPU rebuilds (no distinction from initial bind or resize) |

Startup: `re` creates a `PlatformChannel` + `OutgoingInvitation::SendIsolated` and launches `--rb-remote --headless=new` Chrome with the
inherited `--rb-mojo` end; the invitation carries the real Chrome process handle so Windows Mojo can duplicate handles. A running browser
without a pre-seeded invitation cannot be attached. The texture arrives in the browser as a GPU buffer handle, is duplicated locally by
the browser and sent through `NewBuffer` as a Mojo platform handle (numeric HANDLE values are never IPC data). The fence comes from the
per-frame Viz lease `GetRbReadyFence()` and carries token, value, adapter LUID and an optional shared fence NT handle; the browser sends the
OS handle once per writer token, afterwards only token/value/LUID. Without a fence the keyed mutex key 0 applies; with one, `re` must
`OpenSharedFence` on the same adapter and queue `Wait(value)`.

## 1. Signalling (WebSocket, JSON)

### 1.1 Connection and authentication
- viewer: `ws(s)://<cp>/signal?role=viewer&instance=<id>&token=<t>`; endpoint: `...?role=endpoint&instance=<id>&token=<t>`.
- `token` is the session returned by CP login/registration; missing/invalid sessions close with `4401`, ownership failures with `4403`. Both viewer and external WS test endpoints require owner/admin access; admin may use external unlisted IDs. CP_TOKEN is only the user-registration credential. Native stdio registration is unchanged. Tokens are never logged.
- Messages are JSON objects with type field `t`; **the first message per connection is `hello` with `v` (major version, 1)**. Mismatch ->
  `{t:"error",code:"version"}` and close (`4400`). Unknown `t` and unknown fields are ignored.

### 1.2 Messages
| Direction | `t` | Fields | Notes |
|---|---|---|---|
| viewer->CP | `hello` | `v, role:"viewer", caps:{touch:int, dpr:num, osk:bool, codecs:[...]}, ua` | `touch` = `maxTouchPoints`; `osk` = touch-like device (wants the remote soft keyboard) |
| CP->viewer | `welcome` | `v, sid, instance, iceServers:[...]` | `sid` = random 128-bit hex |
| endpoint->CP | `hello` | `v, role:"endpoint", instance, name, caps:{codecs:["H264","VP8"], maxFps}` | registers the instance; a new endpoint replaces the old one |
| CP->endpoint | `session.open` | `sid, viewer:{caps, ua}, iceServers` | endpoint builds the PeerConnection, sends `offer` |
| endpoint->CP->viewer | `offer` | `sid, sdp` | |
| viewer->CP->endpoint | `answer` | `sid, sdp` | |
| both | `ice` | `sid, candidate:{candidate, sdpMid, sdpMLineIndex}` or `null` | trickle; `null` = end of candidates |
| viewer->CP->endpoint | `restart` | `sid` | ICE restart: endpoint re-offers with `iceRestart:true` |
| both | `bye` | `sid, reason` | normal end; the CP synthesises `bye` to the other side when one WS drops |
| CP->any | `error` | `code, message` | `version`, `auth`, `no-endpoint`, `busy` |

### 1.3 Session lifecycle
1. viewer connects, `hello`; with no endpoint online the CP answers `error:no-endpoint` and keeps the connection, continuing automatically
   when an endpoint appears.
2. CP assigns `sid`: `welcome` to the viewer, `session.open` to the endpoint.
3. endpoint creates the PC (video + 3 DCs) and offers; the viewer sets the remote description, applies the receive-latency hints (section
   5) and answers; both trickle ICE.
4. After DTLS/SCTP the viewer sends `hello` on `ctl`, the endpoint replies; input then flows. The CP keeps only the WS (`bye/restart`).
5. **Single controller**: one viewer session per instance. A new viewer sends the old one `bye reason:"replaced"`; the endpoint closes the
   old PC and releases held keys/buttons and touches (D8).
6. End: `bye` from either side, WS drop (CP ping 15 s / timeout 30 s), PC `failed` for more than 10 s (the viewer tries one `restart` first).

### 1.4 stdio transport
When the CP spawns `re.exe` (`ENDPOINT_PATH`, extra args `ENDPOINT_ARGS` / `ENDPOINT_ARGS_JSON`) the same messages travel on stdin/stdout as
4-byte little-endian length + UTF-8 JSON (<= 4 MiB, `control-plane/src/framing.ts`). `--instance=<id>` comes from the command line; stderr is the
log. Instance control messages (`instance.*`) are reserved for later.

## 2. DataChannels
| id | label | options | encoding | use |
|---|---|---|---|---|
| 0 | `input` | ordered, reliable | **binary** | mouse/pointer/touch/wheel/keyboard (hot path) |
| 1 | `ctl` | ordered, reliable | JSON | hello, resize, focus, IME/OSK, cursor, stats, keyframe request |
| 2 | `probe` | **unordered, maxRetransmits=0** | JSON | clock-sync ping/pong (loss must not queue behind retransmits) |

### 2.1 Why binary input, JSON elsewhere
The difference is bytes and queuing, not CPU: a 120 Hz multi-finger move is hundreds of messages per second at 12-20 bytes binary versus
60-120 JSON; SCTP/DTLS adds ~28+ bytes per message, so small binary messages use less uplink airtime and grow `bufferedAmount` slower under
congestion. The C++ endpoint parses fixed little-endian structs without allocation straight into `ui::MouseEvent/TouchEvent/KeyEvent`. Input is
**ordered reliable**: a lost or reordered key/button transition means a stuck key, costing far more than one retransmit (about one RTT, only on
loss); stale pointer moves are handled by sender-side coalescing (2.4). Low-rate messages (IME, state, stats) are JSON for debuggability.

### 2.2 Binary input format (`input`, little endian)
One DataChannel message each; common 8-byte header:

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | `kind`: 1 POINTER, 2 WHEEL, 3 KEY, 4 RESET |
| 1 | u8 | `action` / subtype |
| 2 | u16 | `seq`, +1 per input message (wraps); for input-to-photon and loss detection |
| 4 | u32 | `ts`: viewer clock (`performance.timeOrigin+now()`) in ms x10, low 32 bits (0.1 ms, wraps in ~5 days) |

`mods` (u8 bitmask): 1 Shift, 2 Ctrl, 4 Alt, 8 Meta, 16 CapsLock, 32 NumLock, 64 AltGraph.

**Coordinates**: `x, y` u16, **normalised to the visible video frame**: `round(px / frameW * 65535)`, independent of viewer size, DPR and
letterbox. `re` multiplies by the root's DIP size (`EventInjector` then does DIP->host pixels). Events in the letterbox are clamped (a down
there is not sent, see the viewer).

**POINTER (kind=1, 20 bytes)**

| Offset | Type | Field |
|---|---|---|
| 1 | u8 | action: 0 down, 1 move, 2 up, 3 cancel, 4 leave (mouse left the video) |
| 8 | u16 | x |
| 10 | u16 | y |
| 12 | u8 | `ptype`: 0 mouse, 1 touch, 2 pen |
| 13 | u8 | `pid`: slot 0-15 (mouse always 0; touch/pen: the viewer maps `pointerId` to the lowest free slot 1-15, max 15 contacts) |
| 14 | u8 | `buttons`: DOM bitmask (1 left, 2 right, 4 middle, 8 back, 16 forward) |
| 15 | i8 | `button`: the changed button (DOM `button`, -1 on move) |
| 16 | u8 | mods |
| 17 | u8 | `pressure` x255 |
| 18 | u8 | `rx`: contact radius in video pixels (clamped to 255) |
| 19 | u8 | `ry` |

- Button changes (including chords) are always down/up (one per changed button, carrying the new `buttons`), never move.
- `seq` is assigned at send time (a coalesced move gets the send-time seq), so wire seq is monotonic.
- Multi-touch is one message per pointer distinguished by `pid`, matching Aura's per-event pointer id; `re` tracks active contacts per `pid`
  and on RESET or session end synthesises cancel for contacts and up for held buttons (D8).

**WHEEL (kind=2, 16 bytes)**

| Offset | Type | Field |
|---|---|---|
| 1 | u8 | mode: 0 pixel, 1 line, 2 page (DOM `deltaMode`) |
| 8 | u16 | x |
| 10 | u16 | y |
| 12 | i16 | dx: mode 0 = video-frame pixels x4 (already display-scaled); modes 1/2 = lines/pages x4 |
| 14 | i16 | dy: same |

(mods are not carried: `re` uses the mods of the last KEY/POINTER; Ctrl+wheel zoom relies on it.)

**KEY (kind=3, variable)**

| Offset | Type | Field |
|---|---|---|
| 1 | u8 | action: 0 down, 1 up |
| 8 | u8 | mods (state after the event) |
| 9 | u8 | flags: 1 repeat, 2 location-right, 4 numpad |
| 10 | u16 | `keyCode` (legacy DOM, ~Windows VK; fallback only) |
| 12 | u8 | `codeLen` |
| 13 | u8 | `keyLen` |
| 14 | ... | `code` (UTF-8, e.g. `KeyA`) followed by `key` (UTF-8, e.g. `a`, `Enter`, `Dead`) |

`re` uses `ui::KeycodeConverter::CodeStringToDomCode(code)` and `KeyStringToDomKey(key)` (single character -> `DomKey::FromCharacter`);
`KeyboardCode` is derived from DomCode/DomKey and `keyCode` is used only when `code` is empty. Printable characters come from Aura's normal
char events, no separate char message. Events with `isComposing` or `keyCode==229` are not sent (IME path, 3.3).

**RESET (kind=4, 8 bytes)**: release every held key/button, cancel every contact. Sent on blur, `visibilitychange(hidden)` and lost pointer capture.

### 2.3 Versioning
`ctl` `hello` carries `input:1`. New fields are appended only and receivers tolerate longer messages; unknown `kind` is ignored; a message
shorter than its minimum is dropped and counted.

### 2.4 Sender-side coalescing (viewer)
Pointer moves keep only the latest coordinates per `pid`; sent immediately when `input.bufferedAmount < 4 KiB`, else on the next
`bufferedamountlow`/rAF (latest only). down/up/cancel, KEY and RESET are always immediate and flush that `pid`'s pending move first.
`getCoalescedEvents()` is unnecessary (latest position, not trajectory; a batched variant of `kind=1, action=1` can be added for drawing apps).

## 3. `ctl` channel (JSON)

### 3.1 Handshake and viewport
| Direction | `t` | Fields |
|---|---|---|
| viewer->re | `hello` | `v:1, input:1, caps:{touch, osk, dpr}, view:{w, h, dpr}` |
| re->viewer | `hello` | `v:1, input:1, frame:{w, h}, dsf, name` |
| viewer->re | `resize` | `w, h, dpr`: available area (CSS px) and DPR; re **may** resize the remote window (DIP = w x h, DSF = dpr or policy) or ignore it (fixed 1080p) |
| re->viewer | `frame` | `w, h, dsf`: remote root size changed (video resolution follows; normalised coordinates unaffected) |
| viewer->re | `focus` | `focused:bool`: page focus/visibility; false -> release input and `Deactivate` the active root (D8); true -> `Activate` |
| viewer->re | `keyframe` | request an IDR (debug; normally RTCP PLI/FIR) |
| re->viewer | `cursor` | `css`: CSS cursor name of the remote cursor (`default`, `pointer`, `text`, `wait`, `none`, ...), from the intercepted `::SetCursor` |

### 3.2 IME / remote soft keyboard (re -> viewer)
Source: `RemoteInputMethod` + `InputMethodObserver` (D9).

| `t` | Fields | Meaning |
|---|---|---|
| `ime.state` | `seq, focused:bool, type, mode, flags, rect:{x,y,w,h}, reason, composing:bool` | editable focus or caret box changed; `focused:false` = blur/detach (`type:"none"`) |
| `ime.vk` | `show:bool, seq` | remote decision to show/hide the soft keyboard (`VirtualKeyboardControllerWin` policy via `GetVirtualKeyboardController()`) |

- `type`: `text, password, search, email, number, tel, url, textarea, contenteditable, date, time, datetime, month, week, null, none`.
- `mode`: `default, none, text, tel, url, email, numeric, decimal, search`. With `inputmode=none`, `ime.state focused:true` arrives but `ime.vk` never shows.
- `flags` bitmask: 1 autocomplete-off, 2 autocorrect-off, 4 spellcheck-off, 8 autocapitalize-none, 16 characters, 32 words, 64 sentences.
- `rect`: caret/selection box **normalised to the video frame** (0-65535, same as input); the viewer places the hidden input there and pans when the keyboard covers it.
- `reason`: `mouse, touch, pen, other, none`. `seq` is monotonic; the viewer drops older states.

### 3.3 Text back-fill (viewer -> re)
| `t` | Fields | re action |
|---|---|---|
| `ime.commit` | `text` | `TextInputClient::InsertText` (confirms/replaces composition first) |
| `ime.compose` | `text, cursor` | `SetCompositionText` (`cursor` = UTF-16 index inside the composition); empty text clears |
| `ime.delete` | `before, after` | `ExtendSelectionAndDelete(before, after)` (UTF-16 units; typical for Android backspace) |
| `ime.key` | `key` | function key (`Enter`, `Backspace`, `Tab`, ...) when the viewer only has `keyCode 229`: re synthesises KEY down/up |
| `ime.hidden` | none | user hid the local keyboard; recorded only, remote focus unchanged |

Viewer behaviour (Chrome 89): a hidden 1x1 `opacity:0` `position:fixed` `<textarea>` (attributes from `flags`, `inputmode`/`type` from
`mode`/`type`; password uses `<input type=password>`). On `ime.vk show:true` it calls `focus()`; mobile browsers only allow programmatic
focus within user activation, and the remote decision normally arrives one RTT (< 150 ms) after the touch, inside transient activation.
If it times out (> 1 s or `visualViewport` did not shrink) a floating keyboard button appears. Text capture: `compositionstart/update/end` ->
`ime.compose`/`ime.commit`; plain `input` (`insertText`) -> `ime.commit`; `deleteContentBackward/Forward` -> `ime.delete`; the textarea is
cleared after each step (the remote is the only truth). Physical keys while focused still go as KEY (non-composing, `keyCode != 229`) with
`preventDefault`. `ime.vk show:false` or `ime.state focused:false` -> `blur()`.

## 4. Statistics and latency probe

### 4.1 `probe` (clock sync)
viewer->re `{t:"ping", id, t0}` (viewer wall clock ms, float); re->viewer `{t:"pong", id, t0, t1, t2}` (`t1` receive, `t2` send, endpoint
wall clock ms, QPC-calibrated). With `t3` the receive time: `rtt = (t3-t0)-(t2-t1)`, `offset = ((t1-t0)+(t2-t3))/2`. Keep the last 32
samples and use the offset of the **minimum-RTT** one; 4 Hz, 20 Hz for the first 2 s. Wall clock is used because the marker is drawn by a
test page on the same host as `re`, so one viewer<->re mapping suffices.

### 4.2 Frame marker (`tools/latency/marker.js`)
Top-left row of **2 sync cells + 72 data cells**, cell = `max(4, round(frameW/160))` px (12 px at 1080p; the receiver scales by
`cell x videoWidth / frame.w`), black = 0, white = 1 (50% luma threshold). Data (big-endian bits): `frameId:u16 | t:u32 (sender wall clock
ms, low 32 bits) | inputSeq:u16 (last applied input seq) | crc8` (poly 0x07 over the first 8 bytes) = 9 bytes. Sync cells white, black.
Frames with a bad CRC are dropped. The sender (mock canvas, or the test page in the remote browser) writes on every draw; the receiver draws
the top-left region of each rVFC frame into a small canvas. capture->display = `displayTime - (tUnwrapped - offset)`; input-to-photon =
display time of the first frame with `inputSeq >= s` minus send time of `s`.

### 4.3 Stats messages (`ctl`, 1 Hz)
| Direction | `t` | Fields |
|---|---|---|
| re->viewer | `stats` | `fps, kbps, encMs:{p50,p99}, qp, idr, backend, queue, bweKbps` |
| viewer->re | `vstats` | `fps, kbps, jbMs, decMs, rtt, lost, nack, pli, freeze, lat:{p50,p80,p99}` |

`jbMs` = delta `jitterBufferDelay` / delta `jitterBufferEmittedCount`; `decMs` = delta `totalDecodeTime` / delta `framesDecoded`; `rtt` from
`candidate-pair.currentRoundTripTime`. Chrome 89 reports the jitter-buffer counters on legacy `track` stats, not `inbound-rtp` (the viewer falls back).

## 5. Receiver latency hints (Chrome 89)
| Mechanism | Chrome 89 | Current | Viewer |
|---|---|---|---|
| `RTCRtpReceiver.playoutDelayHint` (s, non-standard) | yes (M79+) | yes | set 0 if present |
| `RTCRtpReceiver.jitterBufferTarget` (ms) | no (Chrome 124+) | yes | prefer it, set 0 |
| RTP `playout-delay` (min=max=0) | **works**: `VCMTiming` render time 0 | works | sent by re (offer extmap); nothing to do |
| `setCodecPreferences` | yes (M76+) | yes | H.264 first (packetization-mode=1 preferred) |
| `requestVideoFrameCallback` | yes (M83+) | yes | measurement; else `timeupdate`, flagged imprecise |

`UpdatePlayoutDelays` takes the maximum of frame/base/sync minima, so a non-zero JS hint cancels the RTP (0,0): set 0 or nothing. Measured on
Chromium 89.0.4389.0 ([s8-chrome89](research/s8-chrome89.md)): the hint acts as a floor, but 0 does not push the buffer below 89's own estimate
(loopback 35-42 ms vs 7-9 ms on 156), so Chrome 89 low latency depends on the endpoint's RTP playout-delay (0,0).

## 6. Security and robustness
- `re` validates length and range for all DataChannel input; coordinates clamped to [0,65535]; `pid >= 16` dropped; KEY strings <= 32 bytes; IME text <= 4096 UTF-16 units.
- The CP limits WS messages to 64 KiB, closes on non-JSON, and forwards a whitelist of types.
- Input is sent/accepted only after both `ctl hello`s (version mismatch closes the session); session end, `focus:false` and `bye` trigger RESET semantics.
