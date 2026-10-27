# Task m3-trace (timeout 45 min): frame- and input-correlated latency attribution under d25j5
Task dir C:\src\tmp\m3\trace\. You are the ONLY builder/launcher now. You MAY edit remotekit endpoint/ (instrumentation only, behind a switch or cheap),
viewer/, tools/latency/, tools/netem/ (no commits). Incremental `be.sh`.
Problem (research/m3/matrix2.md): loopback m3-combined gives capture->display P80 ~48 and input->flip P80 ~49 ms, but d25j5-c12 (25 ms +-5 one-way,
i.e. 50 ms RTT) gives ~105-125 and ~140-165. The pure network should add ~25-30 (capture) and ~50-60 (input); ~20-40 ms more is unexplained.
1. Per-frame correlation: tag each encoded frame with an id the viewer can see (RTP timestamp; use rVFC metadata.rtpTimestamp and receiveTime) and log on
   the endpoint per frame: capture_end, encoded, first/last packet handed to the transport (libwebrtc send-side: the transport-sequence or
   OnSentPacket hooks if reachable, else the pacer/transport callbacks you can observe), plus the relay's per-packet dwell (enqueue -> forward)
   per direction keyed by RTP timestamp/sequence (parse RTP inside TURN ChannelData/Data; SRTP keeps the RTP header in clear). Viewer: receiveTime
   (last packet), decode, expectedDisplayTime per rtpTimestamp. All clocks are the same host: use QPC/performance.timeOrigin offsets (measure them).
2. Input path: stamp the input event at viewer send, at endpoint DataChannel receive, at browser injection (chrome log or Mojo ack), then the first frame
   whose capture contains the flip (marker) -> join with (1).
3. Explain the gap with numbers per segment at P50/P80/P95 (viewer send -> endpoint rx -> inject -> next vsync/draw -> capture -> encode -> pacer ->
   relay (queue/jitter/FIFO hold) -> viewer rx (last packet) -> decode -> display), for loopback-relay(no shaping) and d25j5-c12, scenarios marker and S.
   Check specifically: FIFO hold-order artefact of the jitter model (a delayed packet holds later ones: what is the effective added delay vs the
   configured +-5 ms?), relay timer granularity, DataChannel (SCTP) delay for small input messages, frame packet spread at the cap, and display
   phase (viewer vsync) under jitter.
4. Sender render latency measured explicitly: input inject (or rAF for the marker) -> Viz draw of the frame containing it (use the remote browser's
   CDP Tracing (categories viz, gpu, cc, benchmark, input) or Performance timeline), so the table has no unattributed gap.
5. Viewer: presentation -> display in headless (estimate) vs ONE headed Edge run on the real 60 Hz display (rVFC presentationTime/expectedDisplayTime
   and frame-callback phase); say how much is the headless estimate.
Deliverable also: research-ready table research_table.md in the task dir with ALL stages: input -> inject -> render -> viz draw -> copy -> VPP ->
encode -> send -> network -> receive -> jitter buffer -> decode -> present -> display, P50/P80/P95 for loopback and d25j5-c12, each cell marked
[M] measured or [I] inferred.
NOTES.md: method, the segment table, the top 3 contributors with evidence, and concrete fixes (product-side vs measurement artefacts).
