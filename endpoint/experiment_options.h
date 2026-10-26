// Copyright 2026 The remotekit Authors
// Use of this source code is governed by a BSD-style license.

#ifndef PRTS_ENDPOINT_EXPERIMENT_OPTIONS_H_
#define PRTS_ENDPOINT_EXPERIMENT_OPTIONS_H_

#include <optional>
#include <string>

namespace prts {

// Parsed once after CommandLine::Init; immutable on all encoder/RTC queues.
struct ExperimentOptions {
  bool valid = true;
  bool cbr = false;
  std::optional<double> max_frame_factor;
  int vbv_frames = 1;
  std::optional<int> vbv_ms;
  int target_usage = 7;
  bool low_power = true;
  int intra_refresh = 0;
  int slices = 1;
  int min_kbps = 2000;
  int start_kbps = 8000;
  int max_kbps = 12000;
  bool playout_extension = true;
  bool playout_metadata = true;
  int playout_min_ms = 0;
  int playout_max_ms = 0;
  std::string fec = "off";
};

const ExperimentOptions& GetExperimentOptions();

}  // namespace prts

#endif  // PRTS_ENDPOINT_EXPERIMENT_OPTIONS_H_
