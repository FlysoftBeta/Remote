// Copyright 2026 The remotekit Authors
// Use of this source code is governed by a BSD-style license.

#ifndef RE_ENCODER_PARAMS_H_
#define RE_ENCODER_PARAMS_H_

#include <cstdint>
#include <optional>

#include "re/experiment_options.h"

namespace re {

enum class RateControl { kCbr, kVbr };
// High is gated: the pinned OpenH264 fallback cannot decode-match it.
enum class AvcProfile { kConstrainedBaseline };

// One parameter set for every AVC backend (oneVPL, NVENC, OpenH264). Always
// low latency: no B-frames, no lookahead, no reordering, one reference.
// Backends map the fields they support and ignore the rest (OpenH264 only
// honours resolution, fps and bitrates; it is configured by WebRTC).
struct EncoderParams {
  int width = 0;
  int height = 0;
  int fps = 30;
  int target_kbps = 0;
  int min_kbps = 0;
  int max_kbps = 0;
  RateControl rate_control = RateControl::kVbr;
  // 0 = infinite GOP: an IDR only on demand (oneVPL GopPicSize 65535).
  int gop_frames = 0;
  // Vertical intra refresh cycle in frames; 0 = off.
  int intra_refresh_cycle = 0;
  int slices = 1;
  // VBV size: vbv_ms if set, otherwise vbv_frames average frames at max_kbps.
  int vbv_frames = 1;
  std::optional<int> vbv_ms;
  // Per-frame size cap hint (VBR only): factor x target frame size when set,
  // otherwise one average frame at max_kbps. Never a hard bound on output.
  std::optional<double> max_frame_factor;
  bool low_power = true;  // oneVPL VDEnc
  int target_usage = 7;   // oneVPL 1..7
  int nvenc_preset = 1;   // NVENC P1..P3
  AvcProfile profile = AvcProfile::kConstrainedBaseline;
  int level = 0;  // 0 = smallest level that honestly fits the stream

  // Stream-independent part from the command line (experiment options).
  static EncoderParams FromOptions(const ExperimentOptions& options);
  // Same parameters for a concrete stream.
  EncoderParams ForStream(int width,
                          int height,
                          int fps,
                          int target_kbps,
                          int min_kbps,
                          int max_kbps) const;

  // Decimal KB, rounded up (oneVPL BufferSizeInKB units).
  int64_t VbvKb() const;
  // MaxFrameSize hint in bytes at the current target; 0 for CBR.
  uint32_t FrameCapBytes() const;
  // AVC level_idc (31, 32, 40, 42) or 0 if the stream exceeds level 4.2.
  int ResolvedLevel() const;
};

}  // namespace re

#endif  // RE_ENCODER_PARAMS_H_
