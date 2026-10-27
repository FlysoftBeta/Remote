#ifndef RB_LEGACY_ENDPOINT_FRAME_SOURCE_H_
#define RB_LEGACY_ENDPOINT_FRAME_SOURCE_H_

#include <cstdint>
#include <functional>

#include "third_party/webrtc/api/video/video_frame.h"

namespace re {
struct FrameGeometry {
  int width = 1920;
  int height = 1080;
  double dsf = 1;
};
class FrameSource {
 public:
  using Consumer = std::function<void(const webrtc::VideoFrame&)>;
  virtual ~FrameSource() = default;
  // Consumer runs on source's capture thread; Stop joins/drains it. Start/Stop
  // are called on signaling; frames must own their buffers until released.
  virtual void Start(Consumer consumer) = 0;
  virtual void Stop() = 0;
  virtual FrameGeometry geometry() const = 0;
  virtual void SetInputSequence(uint16_t seq) = 0;
};
}  // namespace re
#endif  // RB_LEGACY_ENDPOINT_FRAME_SOURCE_H_
