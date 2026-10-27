// Copyright 2026 The remotekit Authors
// Use of this source code is governed by a BSD-style license.

#ifndef RE_VIDEO_ENCODER_BACKEND_H_
#define RE_VIDEO_ENCODER_BACKEND_H_

#include <memory>

#include "re/encoder_params.h"
#include "third_party/webrtc/api/environment/environment.h"
#include "third_party/webrtc/api/video_codecs/sdp_video_format.h"
#include "third_party/webrtc/api/video_codecs/video_encoder.h"

namespace re {

class VplEncoderState;

// One AVC implementation (oneVPL, NVENC, OpenH264). Backends are probed once
// at startup; the first usable one in the selection order creates every
// encoder, and WebRTC's software fallback wrapper still guards a hardware one.
class VideoEncoderBackend {
 public:
  virtual ~VideoEncoderBackend() = default;
  // Lower-case switch value ("vpl") and log label ("VPL").
  virtual const char* name() const = 0;
  virtual const char* label() const = 0;
  // Loads the driver and opens a throwaway session. Must fail cleanly (false
  // plus a log line naming the reason) when the hardware or driver is missing.
  virtual bool Probe() = 0;
  // Per-stream encoder. `base` carries the command-line tuning; the encoder
  // fills in resolution and rates from WebRTC's VideoCodec at InitEncode.
  virtual std::unique_ptr<webrtc::VideoEncoder> Create(
      const webrtc::Environment& env,
      const webrtc::SdpVideoFormat& format,
      const EncoderParams& base) = 0;
};

std::unique_ptr<VideoEncoderBackend> CreateVplBackend(
    std::shared_ptr<VplEncoderState> state);
// NVENC is compile-checked only: UNTESTED ON NVIDIA HARDWARE.
std::unique_ptr<VideoEncoderBackend> CreateNvencBackend(
    std::shared_ptr<VplEncoderState> state);
std::unique_ptr<VideoEncoderBackend> CreateOpenH264Backend();

}  // namespace re

#endif  // RE_VIDEO_ENCODER_BACKEND_H_
