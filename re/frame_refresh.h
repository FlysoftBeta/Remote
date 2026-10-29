// Copyright 2026 The remotekit Authors
// Use of this source code is governed by a BSD-style license.
#ifndef RE_FRAME_REFRESH_H_
#define RE_FRAME_REFRESH_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace re {
// Shared by source, encoder and signaling sequences. Times are monotonic us.
// Only requests fresh capture; WebRTC still owns frame dropping and RTP pacing.
class FrameRefresh {
 public:
  void Start(int fps) {
    std::lock_guard guard(lock_);
    state_ = {};
    state_.active = true;
    state_.period = 1000000 / std::clamp(fps, 1, 120);
  }
  void Stop() {
    std::lock_guard guard(lock_);
    state_.active = false;
  }
  void SetFps(int fps) {
    std::lock_guard guard(lock_);
    state_.period = 1000000 / std::clamp(fps, 1, 120);
  }
  void Source(int64_t now) {
    std::lock_guard guard(lock_);
    // A requested refresh is the next capture. Unsolicited damage starts a
    // fresh convergence run, even for isolated edits on an otherwise idle page.
    if (!state_.requested || now - state_.request_at > 250000)
      state_.quiet_outputs = 0;
    state_.requested = false;
    state_.source_at = now;
  }
  void Rates(uint64_t bps) {
    std::lock_guard guard(lock_);
    if (bps > state_.bps + state_.bps / 5 || !bps ||
        (state_.quiet_outputs >= 5 &&
         bps > state_.settled_bps + state_.settled_bps / 5))
      state_.quiet_outputs = 0;
    state_.bps = bps;
  }
  void Encode(int64_t now) {
    std::lock_guard guard(lock_);
    state_.encode_at = now;
    state_.encoding = true;
  }
  void EncodeFinished(int64_t started, int64_t now) {
    std::lock_guard guard(lock_);
    // Backends here complete synchronously, but may return OK after skipping
    // output for rate control. That is not an encoder still busy for 500 ms.
    if (state_.encoding && state_.encode_at == started) {
      state_.encoding = false;
      state_.encode_cost = std::max<int64_t>(now - started, 0);
    }
  }
  void Encoded(int64_t now, int64_t cost, size_t bytes, size_t pixels,
               bool key, int qp, bool trusted_qp) {
    std::lock_guard guard(lock_);
    state_.encoding = false;
    state_.encoded_at = now;
    state_.encode_cost = std::max<int64_t>(cost, 0);
    state_.bytes = bytes;
    state_.drop_until = 0;
    // QP scales differ between H264 and VP8: callers normalize the threshold.
    // Hardware QP is often untrusted; only nearly empty delta frames count as
    // convergence there. Periodic retry also covers missing/invalid QP.
    const bool quiet = !key && (trusted_qp && qp >= 0
        ? qp <= 24 : bytes <= std::max<size_t>(96, pixels / 16000));
    const bool was_settled = state_.quiet_outputs >= 5;
    state_.quiet_outputs = quiet ? std::min(state_.quiet_outputs + 1, 5) : 0;
    if (state_.quiet_outputs == 5 && !was_settled)
      state_.settled_bps = state_.bps;
  }
  void Dropped(int64_t now) {
    std::lock_guard guard(lock_);
    state_.encoding = false;
    state_.drop_until = now + 100000;
    state_.quiet_outputs = 0;
  }
  void SendDelay(int64_t delay_us) {
    std::lock_guard guard(lock_);
    state_.send_delay = std::clamp<int64_t>(delay_us, 0, 1000000);
  }
  bool Request(int64_t now) {
    std::lock_guard guard(lock_);
    if (!state_.active || !state_.bps || !state_.source_at ||
        now < state_.drop_until)
      return false;
    // Give real capture two frame periods to arrive first. Never accumulate
    // refreshes behind a capture or encode already in flight. Retry a stalled
    // capture/encoder after 500 ms so one dropped frame cannot deadlock repair.
    if (now - state_.source_at < std::max<int64_t>(20000, 2 * state_.period) ||
        (state_.requested && now - state_.request_at < 500000) ||
        (state_.encoding && now - state_.encode_at < 500000))
      return false;
    // Charge the previous encoded frame against GCC's allocation, reserving
    // 25% headroom. Encoding cost and measured pacer residence time add bounds.
    const int64_t wire = static_cast<int64_t>(
        static_cast<double>(state_.bytes) * 8 * 1250000 / state_.bps);
    int64_t interval = std::max({state_.period, wire,
                                2 * state_.encode_cost, 2 * state_.send_delay});
    interval = std::max<int64_t>(interval, 20000);
    if (state_.quiet_outputs >= 5)
      interval = std::max<int64_t>(interval, 1000000);
    if (now - std::max(state_.encoded_at, state_.request_at) < interval)
      return false;
    state_.requested = true;
    state_.request_at = now;
    return true;
  }

 private:
  struct State {
    bool active = false, requested = false, encoding = false;
    int quiet_outputs = 0;
    uint64_t bps = 0, settled_bps = 0;
    size_t bytes = 0;
    int64_t period = 16666, source_at = 0, request_at = 0, encode_at = 0,
            encoded_at = 0, encode_cost = 0, drop_until = 0, send_delay = 0;
  } state_;
  std::mutex lock_;
};
}  // namespace re
#endif  // RE_FRAME_REFRESH_H_
