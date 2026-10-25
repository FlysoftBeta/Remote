# g: H.264 level negotiation with Chrome receivers (Stage-1 orchestrator note)

Source: bounded Sonnet sub-task, 2026-10-25. Corrects `e-video.md` §"Codec negotiation" ("if the answer only permits
Level 3.1, reduce to 720p30"). [V] = read in source, [I] = inference.

- [V] libwebrtc M89 (`e7d9f74b`) `media/base/h264_profile_level_id.cc`, `GenerateProfileLevelIdForAnswer`:
  `answer_level = (asym(local) && asym(remote)) ? local_level : min(local_level, remote_level)`.
  So with level-asymmetry-allowed=1 on both sides, Chrome's answer carries **its own receive level**.
- [V] Chrome advertises receive formats at **Level 3.1**: software `modules/video_coding/codecs/h264/h264.cc`
  `SupportedH264Codecs` (CB/Baseline/Main @3.1, asym=1); hardware `third_party/blink/renderer/platform/peerconnection/
  rtc_video_decoder_factory.cc` `VdcToWebRtcFormat` computes the level from a ~720p30 probe (TODO to query real max).
  => The answer is effectively always 3.1, whatever we offer.
- [V] No level enforcement on receive: `rtc_video_decoder_adapter.cc` has no SPS level/profile check (only
  `IsDecoderConfigSupported` resolution check + software fallback); `internal_decoder_factory.cc` matches profile and
  packetization only.
- [I] Chrome's own OpenH264 sender (`h264_encoder_impl.cc`) never sets level from SDP; the encoder writes the level
  matching the resolution, so Chrome itself sends 1080p with SPS level > negotiated 3.1. Receivers tolerate it.

Decision for the endpoint: offer `42e01f` (packetization-mode=1, level-asymmetry-allowed=1), treat the negotiated 3.1 as
nominal, write the honest SPS level for the actual stream (4.0 for 1080p30, 4.2 for 1080p60). Spike S8 verifies 1080p
decode on Chrome 89 and current Chrome, hardware and `--disable-accelerated-video-decode` software decode.
Not verified: current-main location of the answer generator, FFmpeg decoder internals, M89 hardware decode path.
