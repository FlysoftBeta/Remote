#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#pragma allow_unsafe_libc_calls
#endif

#include "rb_legacy_endpoint/signaling.h"

#include <fcntl.h>
#include <io.h>
#include <cstdint>
#include <cstdio>
#include <utility>

#include "base/check.h"
#include "base/check_op.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_util.h"

namespace re {
namespace {
constexpr uint32_t kMaxRecord = 4 * 1024 * 1024;
constexpr size_t kMaxQueue = 8 * 1024 * 1024;
bool ReadExact(void* data, size_t size) {
  return fread(data, 1, size, stdin) == size;
}
}  // namespace

Clock::Clock()
    : ticks_(base::TimeTicks::Now()),
      wall_ms_(base::Time::Now().InMillisecondsFSinceUnixEpochIgnoringNull()) {}
double Clock::WallMs() const {
  return wall_ms_ + (base::TimeTicks::Now() - ticks_).InMillisecondsF();
}

Signaling::Signaling() {
  CHECK_NE(_setmode(_fileno(stdin), _O_BINARY), -1);
  CHECK_NE(_setmode(_fileno(stdout), _O_BINARY), -1);
  writer_ = std::thread([this] { WriteLoop(); });
}
Signaling::~Signaling() {
  CHECK(!reader_.joinable());
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  ready_.notify_one();
  writer_.join();
}
void Signaling::Start(Receiver receiver) {
  reader_ = std::thread([this, receiver = std::move(receiver)] {
    for (;;) {
      uint8_t prefix[4];
      size_t n = fread(prefix, 1, 4, stdin);
      if (n == 0 && feof(stdin)) {
        return;
      }
      if (n != 4) {
        valid_ = false;
        return;
      }
      uint32_t length = uint32_t{prefix[0]} | (uint32_t{prefix[1]} << 8) |
                        (uint32_t{prefix[2]} << 16) |
                        (uint32_t{prefix[3]} << 24);
      if (!length || length > kMaxRecord) {
        valid_ = false;
        return;
      }
      std::string body(length, '\0');
      if (!ReadExact(body.data(), length) ||
          !base::IsStringUTF8AllowingNoncharacters(body)) {
        valid_ = false;
        return;
      }
      auto value = base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
      if (!value) {
        valid_ = false;
        return;
      }
      receiver(std::move(*value));
    }
  });
}
bool Signaling::Wait() {
  reader_.join();
  if (!valid_)
    LOG(ERROR) << "Invalid/truncated stdio record";
  return valid_;
}
void Signaling::Send(base::DictValue message) {
  std::string body;
  CHECK(base::JSONWriter::Write(message, &body));
  CHECK(!body.empty() && body.size() <= kMaxRecord);
  uint32_t n = static_cast<uint32_t>(body.size());
  std::string record(4, '\0');
  for (int i = 0; i < 4; ++i)
    record[i] = static_cast<char>(n >> (i * 8));
  record += body;
  {
    std::lock_guard lock(mutex_);
    CHECK(!stopping_);
    CHECK_LE(queued_bytes_ + record.size(), kMaxQueue);
    queued_bytes_ += record.size();
    queue_.push(std::move(record));
  }
  ready_.notify_one();
}
void Signaling::WriteLoop() {
  for (;;) {
    std::string record;
    {
      std::unique_lock lock(mutex_);
      ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (queue_.empty())
        return;
      record = std::move(queue_.front());
      queue_.pop();
    }
    // A broken pipe or exceeded queue is process-fatal, as in endpoint.js.
    CHECK_EQ(fwrite(record.data(), 1, record.size(), stdout), record.size());
    CHECK_EQ(fflush(stdout), 0);
    std::lock_guard lock(mutex_);
    queued_bytes_ -= record.size();
  }
}
}  // namespace re
