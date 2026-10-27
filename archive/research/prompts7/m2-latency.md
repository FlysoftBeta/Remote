# Task m2-latency (timeout 45 min): per-stage latency breakdown + quick wins (playout delay 0, 60 fps), measured
Task dir C:\src\tmp\m2\latency\. You are the ONLY builder/launcher now. You MAY edit remotekit endpoint/, viewer/, tools/latency/ and, only if needed for the
capture rate, chrome/browser/prts/prts_capture.* (behind a switch, default unchanged) with an incremental `be.sh chrome`. No commits.

Problem: on one machine (loopback, headless Edge viewer, OpenH264, dev build, 1898x1068 @30 fps) capture->display is P50/P95/P99 85.7/101.7/114.3 ms and
input->decoded flip 132.9/147.7/155.7 ms (C:\src\tmp\m2\runtime\NOTES.md; command there). Budget: the product needs P80 < 100 ms end to end with 50 ms network.
1. Breakdown first (no behaviour change): per-frame timestamps on one clock (QueryPerformanceCounter / base::TimeTicks are the same clock on Windows) for
   viz capture (VideoFrame metadata capture_begin/end/reference time as delivered to the endpoint), endpoint receive, copy/convert done, encode start/end,
   handed to libwebrtc; log a 5 s rolling summary (P50/P95 of each stage delta), not a line per frame. Viewer side: from getStats inbound-rtp
   (jitterBufferDelay/jitterBufferEmittedCount, totalDecodeTime, totalProcessingDelay, framesDropped) and requestVideoFrameCallback metadata
   (receiveTime, expectedDisplayTime, presentationTime) -> network+jitter buffer, decode, render waits. Write the table: where do the ~86 ms go?
2. Quick wins, each measured separately with the same 60 s harness run (capture->display and input->flip, n): (a) receiver jitter buffer: set
   RTCRtpReceiver.jitterBufferTarget = 0 (and playoutDelayHint = 0 for Chrome 89) in viewer/ ; (b) sender playout-delay RTP header extension
   (http://www.webrtc.org/experiments/rtp-hdrext/playout-delay) with min=max=0 on the endpoint (find how libwebrtc at the pin negotiates and sets it:
   header extension in the offer + per-frame EncodedImage playout delay or the sender API; cite files); (c) content hint / degradation preference for
   low latency if relevant; (d) 60 fps capture (capturer min capture period and the frame source/encoder settings), check CPU and whether OpenH264 keeps up
   at 1080p60; (e) anything the breakdown shows as an obvious stall (e.g. waiting a whole frame period somewhere, GPU readback sync, Nagle-like batching).
   Keep (a)-(c) if they help and do not break Chrome 89 compatibility; make (d) a switch with the default chosen from the data.
Deliverable: NOTES.md with the breakdown table, a before/after table per change, the kept changes, and the next biggest lever (what M3 oneVPL GPU path
should remove, in ms).
