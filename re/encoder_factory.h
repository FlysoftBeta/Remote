// Copyright 2026 The remotekit Authors
// Use of this source code is governed by a BSD-style license.

#ifndef RE_ENCODER_FACTORY_H_
#define RE_ENCODER_FACTORY_H_

#include <memory>
#include <string_view>
#include <vector>

#include "re/video_encoder_backend.h"
#include "third_party/webrtc/api/video/encoded_image.h"
#include "third_party/webrtc/api/video_codecs/video_encoder_factory.h"

namespace re {

// --encoder=auto|vpl|nvenc|openh264. Auto picks by adapter vendor: Intel ->
// oneVPL, NVIDIA -> NVENC, then OpenH264.
enum class EncoderChoice { kAuto, kVpl, kNvenc, kOpenH264 };
bool ParseEncoderChoice(std::string_view value, EncoderChoice* choice);
const char* EncoderChoiceName(EncoderChoice choice);

// Applies the configured playout-delay metadata to an outgoing frame.
void SetPlayoutDelay(webrtc::EncodedImage& output);

// Selects a hardware backend (falling back to OpenH264 with a log line if it
// is unavailable) and wraps every encoder with the low-latency output
// decoration and the stock software fallback.
class EncoderFactory final : public webrtc::VideoEncoderFactory {
 public:
  EncoderFactory(std::shared_ptr<VplEncoderState> state, EncoderChoice choice);
  ~EncoderFactory() override;
  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;
  std::unique_ptr<webrtc::VideoEncoder> Create(
      const webrtc::Environment& env,
      const webrtc::SdpVideoFormat& format) override;

 private:
  std::unique_ptr<VideoEncoderBackend> hardware_;  // null: OpenH264 only
  std::unique_ptr<VideoEncoderBackend> software_;
  EncoderParams base_;
};

}  // namespace re

#endif  // RE_ENCODER_FACTORY_H_
