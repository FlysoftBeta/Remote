#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#pragma allow_unsafe_libc_calls
#endif

#include "re/synthetic_source.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>

#include "base/check.h"
#include "third_party/libyuv/include/libyuv/planar_functions.h"
#include "third_party/webrtc/api/video/i420_buffer.h"
#include "third_party/webrtc/rtc_base/time_utils.h"

namespace re {
SyntheticSource::SyntheticSource(int fps, const Clock& clock)
    : fps_(fps), clock_(clock) {}
SyntheticSource::~SyntheticSource() { Stop(); }
FrameGeometry SyntheticSource::geometry() const { return {}; }
void SyntheticSource::SetInputSequence(uint16_t seq) { input_seq_ = seq; }
void SyntheticSource::Start(Consumer consumer) {
  CHECK(!thread_.joinable());
  stopped_ = false;
  thread_ = std::thread([this, consumer = std::move(consumer)] {
    Run(consumer);
  });
}
void SyntheticSource::Stop() {
  {
    std::lock_guard lock(mutex_);
    stopped_ = true;
  }
  wake_.notify_one();
  if (thread_.joinable())
    thread_.join();
}
void SyntheticSource::Run(Consumer consumer) {
  uint16_t frame_id = 0;
  auto next = std::chrono::steady_clock::now();
  const auto period = std::chrono::microseconds(1000000 / fps_);
  for (;;) {
    {
      std::lock_guard lock(mutex_);
      if (stopped_)
        return;
    }
    auto g = geometry();
    auto buffer = webrtc::I420Buffer::Create(g.width, g.height);
    // Limited-range grayscale keeps marker edges free of chroma artifacts.
    libyuv::I420Rect(buffer->MutableDataY(), buffer->StrideY(),
                    buffer->MutableDataU(), buffer->StrideU(),
                    buffer->MutableDataV(), buffer->StrideV(),
                    0, 0, g.width, g.height, 48, 128, 128);
    int bar = (static_cast<int>(frame_id) * 12) % (g.width - 96);
    for (int y = 0; y < g.height; ++y)
      std::fill_n(buffer->MutableDataY() + y * buffer->StrideY() + bar,
                  96, 180);
    uint32_t ms = static_cast<uint32_t>(static_cast<uint64_t>(clock_.WallMs()));
    uint16_t seq = input_seq_.load();
    uint8_t bytes[9] = {static_cast<uint8_t>(frame_id >> 8),
        static_cast<uint8_t>(frame_id), static_cast<uint8_t>(ms >> 24),
        static_cast<uint8_t>(ms >> 16), static_cast<uint8_t>(ms >> 8),
        static_cast<uint8_t>(ms), static_cast<uint8_t>(seq >> 8),
        static_cast<uint8_t>(seq), 0};
    uint8_t crc = 0;
    for (int i = 0; i < 8; ++i) {
      crc ^= bytes[i];
      for (int bit = 0; bit < 8; ++bit)
        crc = static_cast<uint8_t>((crc << 1) ^ ((crc & 128) ? 7 : 0));
    }
    bytes[8] = crc;
    int cell = std::max(4, (g.width + 80) / 160);
    for (int y = 0; y < cell * 2; ++y) {
      auto* row = buffer->MutableDataY() + y * buffer->StrideY();
      std::fill_n(row, 75 * cell, 16);
      if (y >= cell)
        continue;
      for (int i = 0; i < 74; ++i) {
        bool white = i == 0 || (i >= 2 &&
            ((bytes[(i - 2) / 8] >> (7 - ((i - 2) % 8))) & 1));
        std::fill_n(row + i * cell, cell, white ? 235 : 16);
      }
    }
    auto frame = webrtc::VideoFrame::Builder()
        .set_video_frame_buffer(buffer)
        .set_timestamp_us(webrtc::TimeMicros())
        .set_rotation(webrtc::kVideoRotation_0).build();
    consumer(frame);
    ++frame_id;
    next += period;
    if (next < std::chrono::steady_clock::now())
      next = std::chrono::steady_clock::now() + period;
    std::unique_lock lock(mutex_);
    if (wake_.wait_until(lock, next, [this] { return stopped_; }))
      return;
  }
}
}  // namespace re
