// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RE_LATENCY_STATS_H_
#define RE_LATENCY_STATS_H_

#include <stdlib.h>

#include <algorithm>
#include <deque>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "base/time/time.h"

namespace re {
// m3-trace: per-frame / per-input TimeTicks trace lines ("PRTS TR ...", microseconds) when the environment variable RB_TRACE is set.
inline bool TraceEnabled() {
  static const bool on = [] {
    char* v = nullptr;
    size_t n = 0;
    bool set = _dupenv_s(&v, &n, "RB_TRACE") == 0 && v != nullptr;
    free(v);
    return set;
  }();
  return on;
}
inline int64_t TraceUs(base::TimeTicks t) {
  return (t - base::TimeTicks()).InMicroseconds();
}
// Bounded, per-frame TimeTicks samples; only the last five seconds are logged.
// Windows TimeTicks is consistent across the browser and endpoint processes.
class LatencyStats {
 public:
  static LatencyStats& Get();
  void Add(const char* stage, base::TimeTicks from, base::TimeTicks to,
           bool allow_negative = false) {
    if (from.is_null() || to.is_null() || (!allow_negative && to < from))
      return;
    base::AutoLock lock(lock_);
    auto& samples = stages_[stage];
    samples.push_back({from, to});
    while (samples.size() > 1200 ||
           (!samples.empty() && to - samples.front().to > base::Seconds(5)))
      samples.pop_front();
  }
  void Log() {
    const auto now = base::TimeTicks::Now();
    std::ostringstream output;
    {
      base::AutoLock lock(lock_);
      for (auto& [name, samples] : stages_) {
        while (!samples.empty() && now - samples.front().to > base::Seconds(5))
          samples.pop_front();
        if (samples.empty())
          continue;
        std::vector<double> values;
        for (const auto& sample : samples)
          values.push_back((sample.to - sample.from).InMillisecondsF());
        std::sort(values.begin(), values.end());
        output << " " << name << "=" << values[(values.size() - 1) / 2]
               << "/" << values[(values.size() - 1) * 95 / 100]
               << "(n=" << values.size() << ",last_us="
               << (samples.back().from - base::TimeTicks()).InMicroseconds()
               << ":"
               << (samples.back().to - base::TimeTicks()).InMicroseconds()
               << ")";
      }
    }
    LOG(INFO) << "PRTS latency5s p50/p95_ms" << output.str();
  }
  void SourceReady(int64_t timestamp_us) {
    base::AutoLock lock(lock_);
    source_ready_[timestamp_us] = base::TimeTicks::Now();
    if (source_ready_.size() > 240)
      source_ready_.erase(source_ready_.begin());
  }
  void EncodeStart(int64_t timestamp_us, base::TimeTicks now) {
    base::TimeTicks ready;
    {
      base::AutoLock lock(lock_);
      auto it = source_ready_.find(timestamp_us);
      if (it == source_ready_.end())
        return;
      ready = it->second;
      source_ready_.erase(it);
    }
    Add("source_to_encode", ready, now);
  }

 private:
  struct Sample {
    base::TimeTicks from;
    base::TimeTicks to;
  };
  base::Lock lock_;
  std::map<std::string, std::deque<Sample>> stages_;
  std::map<int64_t, base::TimeTicks> source_ready_;
};
}  // namespace re

#endif  // RE_LATENCY_STATS_H_
