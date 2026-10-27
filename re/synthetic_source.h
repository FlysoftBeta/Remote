#ifndef RE_SYNTHETIC_SOURCE_H_
#define RE_SYNTHETIC_SOURCE_H_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "re/frame_source.h"
#include "re/signaling.h"

namespace re {
class SyntheticSource : public FrameSource {
 public:
  SyntheticSource(int fps, const Clock& clock);
  ~SyntheticSource() override;
  void Start(Consumer consumer) override;
  void Stop() override;
  FrameGeometry geometry() const override;
  void SetInputSequence(uint16_t seq) override;

 private:
  void Run(Consumer consumer);
  const int fps_;
  const Clock& clock_;
  std::atomic<uint16_t> input_seq_{0};
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable wake_;
  bool stopped_ = true;
};
}  // namespace re
#endif  // RE_SYNTHETIC_SOURCE_H_
