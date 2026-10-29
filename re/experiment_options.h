// Copyright 2026 The remotekit Authors
// Use of this source code is governed by a BSD-style license.

#ifndef RE_EXPERIMENT_OPTIONS_H_
#define RE_EXPERIMENT_OPTIONS_H_

#include <optional>
#include <string>

namespace re {

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
  int nvenc_preset = 1;  // NVENC P1..P3
  int min_kbps = 300;
  int start_kbps = 8000;
  int max_kbps = 8000;
  bool playout_extension = true;
  bool playout_metadata = true;
  int playout_min_ms = 0;
  int playout_max_ms = 0;
  std::string fec = "off";
};

const ExperimentOptions& GetExperimentOptions();

}  // namespace re

#endif  // RE_EXPERIMENT_OPTIONS_H_
