# PT-FIX-A change log

## Scope and evidence
- [V] Task: requested findings 1, 2, 3, 4, 5, 8, 11, 12, 13, 21, 22, 23.
- [V] Read research/pt-review.md and design/06-protocol.md sections 1, 2.4, 3.1, 6.
- [V] Code edits restricted to the five authorized JavaScript files.
- [V] No commits, downloads, or Chromium checkout changes.
- [V] Temporary artifacts are under C:/src/tmp/ptrack.
- [V] Concurrent changes to input/IME/protocol/latency files are left untouched.

## Findings and changes
| Finding | File | Change |
|---|---|---|
| 1 | control-plane/index.js | [V] Per-socket error listener installed before auth; send callback and synchronous send failures terminate and clean up only that connection. |
| 2 | control-plane/endpoint.js | [V] Catch synchronous writer throws and asynchronous rejection; stdin/stdout/spawn errors share idempotent cleanup. |
| 2 | control-plane/framing.js | [V] Recheck stream closure when a queued frame reaches the stream. |
| 3 | viewer/viewer.js | [V] Codes 4000/4400/4401 stop retries and offer status-click reconnect. |
| 3 | viewer/viewer.js | [V] Other closes retry after 1/2/4/8/10 seconds; connected recovery resets backoff. |
| 4 | control-plane/index.js | [V] Replacement sends old-session bye to viewer before new welcome. |
| 4 | viewer/viewer.js | [V] New welcome sid closes PC/channels/clock and clears local session state. |
| 5 | viewer/viewer.js | [V] Session generation and captured PC guard every negotiation continuation; offers serialize. |
| 5 | tools/mock-endpoint/mock.js | [V] Captured session/PC and serialized offer/answer operations discard stale continuations. |
| 8 | viewer/viewer.js | [V] Input waits for endpoint ctl hello v=1/input=1 plus open input channel. |
| 8 | tools/mock-endpoint/mock.js | [V] Input/IME ignored before valid viewer hello; mismatches send bye reason=version. |
| 11 | viewer/viewer.js | [V] Remote ICE queues even before offer; drain follows resolved remote SDP. |
| 11 | tools/mock-endpoint/mock.js | [V] Remote ICE queues until answer; outgoing ICE waits for offer transmission. |
| 11 | both browser peers | [V] Null candidates use addIceCandidate({candidate:''}); errors on this marker are ignored. |
| 11 | tools/mock-endpoint/mock.js | [V] Configure sender before createOffer; guarded post-local-SDP fallback for older senders. |
| 12 | control-plane/index.js | [V] Viewer messages require exact current sid and are never relabeled. |
| 13 | viewer/viewer.js | [V] One restart per session; ten-second failed deadline sends ice-failed bye and closes PC. |
| 21 | tools/mock-endpoint/mock.js | [V] Reset field focus/composition, inputSeq, pointers, imeSeq, counters and stats baseline on open/bye. |
| 22 | viewer/viewer.js | [V] PC uses iceServers captured from welcome. |
| 23 | control-plane/index.js | [V] HTTP request/path decoding exceptions return 400. |

## Session cleanup details
- [V] clearSession calls are guarded by typeof for both RKInput and RKIme.
- [V] Input is disabled before session cleanup.
- [V] Endpoint hello/stats, cursor, video source and viewer stats baseline are cleared.
- [V] Existing measurement callback is retained, paused via null clock offset, and reset for the new clock.
- [V] Old channel callbacks and pending stats promises check the captured session/PC.
- [V] Automated report timers also discard a replaced session.
- [V] Video play and getStats rejections are caught.
- [V] CP session start/end tolerates synchronous cleanup during send failure.
- [V] Status click restores pointer events because stylesheet disables them by default.

## Verification
| Check | Result |
|---|---|
| node --check (five allowed files) | [V] Passed final checks on all five files. |
| node --test control-plane/test/ | [V] Failed: Node 24.22.0 resolves directory as module, MODULE_NOT_FOUND. |
| node --test control-plane/test/*.test.js | [V] Passed: 2 tests, 0 failures. |
| Edge latency, port 8220 | [V] Passed; H.264, 248/248 markers, latency percentiles printed. |
| Chrome 89 latency, port 8221 | [V] Passed; VP8, 230/230 markers, latency percentiles printed. |
| Failure/race fixtures | [V] Four fixture groups passed (see details below). |
| git diff --check | [V] Allowed-file check passed; unrelated concurrent viewer/ime.js whitespace left untouched. |

## Prior art
- [V] Only survey headings were inspected to identify whether a relevant section was required.
- [V] No prior-art implementation code was opened, copied or ported.
- [V] Changes derive from the current protocol and explicit task decisions.
- [I] Prior implementation reuse was unnecessary for these JavaScript lifecycle fixes.

## Limits
- [I] Local browser tests cannot establish real-network ICE restart reliability.
- [I] Android OSK activation and physical touch behavior belong to the parallel input/IME task.
- [V] Both requested browser harness commands completed successfully.

## Measured browser output
| Metric | Edge 156, port 8220 | Chromium 89.0.4389.0, port 8221 |
|---|---|---|
| Codec | [V] H.264 | [V] VP8 (this build advertises no H.264) |
| Resolution/rate | [V] 1920x1080, nominal 30 fps | [V] 1920x1080, nominal 30 fps |
| Frames decoded | [V] 248/248, zero failures | [V] 230/230, zero failures |
| Capture to display P50/P80/P95/P99 ms | [V] 107.7/117.3/126.5/136.1 | [V] 86.3/102.3/103.1/103.4 |
| Input to photon P50/P80/P99 ms | [V] 123.1/150.0/163.0, n=102 | [V] 112.2/128.2/155.5, n=102 |
| Jitter buffer / decode ms | [V] 8.3/3.5 | [V] 38.3/10.5 |
| CSV stem | [V] latency-2026-10-25T15-44-47-003Z | [V] latency-2026-10-25T15-45-53-924Z |

## Failure/race fixture output
| Group | Verified behavior |
|---|---|
| Viewer | [V] Delayed old SRD produces no old answer after new welcome. |
| Viewer | [V] ICE before offer/remote SDP is queued; null is passed as empty candidate. |
| Viewer | [V] ICE configuration comes from welcome; queued offers serialize. |
| Viewer | [V] Answer does not enable input; valid ctl hello enables it; version mismatch ends session. |
| Viewer | [V] Repeated failed events produce one restart; recovery cancels deadline; later unrecovered failure closes after 10s. |
| Viewer | [V] 4000/4400/4401 schedule no retry; status click reconnects; transient backoff is 1/2/4/8/10/10s. |
| Mock | [V] Sender parameters precede offer; outgoing offer precedes ICE and null marker. |
| Mock | [V] Early input/IME ignored; valid viewer hello produces endpoint hello; mismatch ends session. |
| Mock | [V] Remote ICE waits for answer; bye resets focus/composition/seq/counts. |
| Mock | [V] Replaced pending createOffer cannot publish SDP for its old session. |
| Stdio | [V] Oversized encode throw, write callback failure and stdin EPIPE all clean up once. |
| CP | [V] Live WebSocket endpoint replacement delivers bye before new welcome. |
| CP | [V] Old-sid answer/ICE/restart/bye are dropped; current restart is relayed. |
| CP | [V] Malformed escaped HTTP path returns 400; oversized WS disconnect leaves health endpoint alive. |

## Commands and artifacts
- [V] node --check on control-plane/index.js, endpoint.js, framing.js, viewer/viewer.js, tools/mock-endpoint/mock.js.
- [V] node --test control-plane/test/ attempted; directory resolution failed as recorded above.
- [V] node --test control-plane/test/*.test.js: 2 passed, 0 failed.
- [V] node tools/latency/run.mjs --secs 10 --port 8220.
- [V] node tools/latency/run.mjs --secs 10 --viewer-browser C:/src/tmp/chrome89/chrome-win/chrome.exe --old-headless --port 8221.
- [V] node C:/src/tmp/ptrack/fix-a-fixtures.mjs: all four groups passed, repeated after final code polish.
- [V] Logs: fix-a-edge.log, fix-a-m89.log, fix-a-fixtures.log in this report directory.
- [V] Fixtures and browser profiles/CSV/JSON remain only in C:/src/tmp/ptrack.
- [V] Final changes remain uncommitted; only the five authorized code paths were edited by this task.
