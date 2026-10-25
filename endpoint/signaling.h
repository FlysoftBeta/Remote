#ifndef PRTS_ENDPOINT_SIGNALING_H_
#define PRTS_ENDPOINT_SIGNALING_H_

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "base/time/time.h"
#include "base/values.h"

namespace prts {
// A QPC-derived wall clock shared by probe replies and synthetic markers.
class Clock {
 public:
  Clock();
  double WallMs() const;

 private:
  const base::TimeTicks ticks_;
  const double wall_ms_;
};

// stdout is exclusively framed JSON; diagnostics must use stderr. Send is
// nonblocking and bounded (8 MiB), even when CP stops consuming the pipe.
class Signaling {
 public:
  using Receiver = std::function<void(base::DictValue)>;
  Signaling();
  ~Signaling();
  void Start(Receiver receiver);
  bool Wait();  // Join reader on EOF; false for malformed/truncated records.
  void Send(base::DictValue message);

 private:
  void WriteLoop();
  std::thread reader_;
  std::thread writer_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::queue<std::string> queue_;
  size_t queued_bytes_ = 0;
  bool stopping_ = false;
  bool valid_ = true;  // Read only after joining reader_.
};
}  // namespace prts
#endif  // PRTS_ENDPOINT_SIGNALING_H_
