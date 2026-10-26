# Receiver jitter-buffer delay analysis (read-only)
Source: third_party/webrtc video/video_stream_buffer_controller.cc (UpdateFrameBufferTimings ~l.335-352, kZeroPlayoutDelayMaxDecodeQueueSize=8 l.54),
video/frame_decode_timing.cc (MaxWaitingTime l.68-90), modules/video_coding/timing/timing.cc (RenderTime l.196-205, UseLowLatencyRendering l.76).

## Mechanism
- jitterBufferDelay per frame = (frame released by VideoStreamBufferController) - (min_receive_time = first packet of the frame received). It therefore INCLUDES
  frame assembly (first->last packet arrival), NACK/RTX waits, reference waits, and decode-queue wait; NOT decode time, NOT render.
- min=max=0: UseLowLatencyRendering true -> RenderTime()=0 -> MaxWaitingTime = 0-now-... < 0 -> clamped to 0: no target-delay wait, no pacing
  (the kZeroPlayoutDelayMinPacing branch needs max>0). jitterBufferTarget only feeds minimum_delay, which is moot here.
- Remaining wait: (a) frame completeness (last packet), (b) decoder_ready_for_new_frame_ (previous decode finished; decode was 0.5 ms in G run, 3 ms in S), (c) NACK/RTX on loss, (d) missing refs.
## Why 10-58 ms with 5 ms jitter
Jitter is ~irrelevant; the delay is the spread of the frame's packets in time. Game: ~45 KB/frame (3.5 Mbps/10 fps), ~38 packets.
Shaper at 12 Mbps serialises 45 KB in ~30 ms; sender pacer (pacing factor ~2.5x of target rate, plus queue/TURN relay hops) adds spread; TURN relay timer/FIFO holds add more.
So first->last packet ~30-60 ms matches G 41-58 ms. V (4 Mbps, 22-29 fps, ~20 KB, keyframe bursts) 14-20 ms; S (2.4 Mbps, 30 fps, 10 KB) 10-12 ms ~ 5 ms jitter + 1-2 serialisations.
Correlation with frame size in data: jitter ~ f(bytes/frame), not fps, and G c16 (58) > c12 (51) > c10 (42) tracks link/pacing, consistent with serialisation+pacer.
Decode queue: not applicable (decode 0.5-3.5 ms << frame interval; IsTooManyFramesQueued needs >8 buffered frames). RTCP timing estimate: unused in low-latency path (RenderTime returns 0 before LocalTime). NACK: only the wifi-loss runs (S 12.7, V 20.8, G 40.0 ~ no worse than clean) -> not dominant.
Note: this delay is not extra latency beyond the physical serialisation; the frame cannot be decoded before its last packet. It is a *measurement of transmit spread*. Real fix = smaller frames / earlier packet delivery, not receiver tuning.
## Applicable to observed data
Frame assembly/serialisation + pacer spread: YES (dominant, esp. G). Netem 12 Mbps cap: 30 ms/frame. Decode queue: no. NACK: minor. Timing estimate: no. Also a keyframe (IDR storm in notes) is several x larger -> 100+ ms spread, inflates means in V.
## Fixes (ranked)
1. Sender pacing: raise pacing factor / disable pacer burst limiting for this stream (field trial WebRTC-Video-Pacing / PacingFactor via
   WebRTC-VideoRateControl... or set RtpTransportControllerSend pacing rate = high; "WebRTC-Pacer-DrainQueue"), so a frame leaves in ~1 burst: -10..-25 ms on G (frame leaves at link rate). Risk: queue loss on real bottleneck. Sender-side, Chrome-agnostic (89 OK).
2. Smaller frames: raise fps for game (10 fps is encoder-bound: 80-86 ms encode, openh264 -> VPL fixes) -> 30 fps => ~15 KB frames => spread ~10 ms: -30..-40 ms. Cap max frame size / lower QP ceiling; avoid IDR (IDR storm fix) : -20..-50 ms on keyframes.
3. Intra refresh/slices: gradual intra refresh removes IDR spikes (only in encoder; VPL supports IntRefType); slices give packets decodable earlier but libwebrtc receiver still waits for the full frame -> no gain at receiver. (low value)
4. Receiver (viewer.js): already at floor. jitterBufferTarget needs Chrome >=114 (Chrome 89: only playoutDelayHint; legacy RTCRtpReceiver.playoutDelayHint, which maps to same min delay). No receiver setting reduces assembly wait. 0 ms gain.
5. Reduce TURN relay hop cost (direct UDP / relay timer P99 up to 48 ms in S-r1): up to -5..-15 ms tails.
6. NACK: enable FlexFEC/ULPFEC only if loss; avoid, adds bitrate. Keep RTX; minor.
Expected total: G 50 ms -> ~10-15 ms with 1+2.
