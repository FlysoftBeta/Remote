// Copyright 2026 The remotekit Authors
// Use of this source code is governed by a BSD-style license.

#include "re/encoder_params.h"

namespace re {
namespace {

// Annex A limits for constrained baseline. Never understate the actual SPS
// level to match a nominal SDP string. Odd padded aspect ratios may exceed
// MaxFS even if the visible pixel count fits the viewport budget.
int AvcLevelFor(int width, int height, int fps, int kbps) {
  const int mb_width = (width + 15) / 16;
  const int mb_height = (height + 15) / 16;
  const int macroblocks = mb_width * mb_height;
  struct Level {
    int id;
    int fs;
    int mbps;
    int kbps;
  };
  constexpr Level levels[] = {{31, 3600, 108000, 14000},
                              {32, 5120, 216000, 20000},
                              {40, 8192, 245760, 20000},
                              {42, 8704, 522240, 50000}};
  for (const auto& level : levels) {
    if (macroblocks <= level.fs && macroblocks * fps <= level.mbps &&
        kbps <= level.kbps && mb_width * mb_width <= level.fs * 8 &&
        mb_height * mb_height <= level.fs * 8)
      return level.id;
  }
  return 0;
}

}  // namespace

EncoderParams EncoderParams::FromOptions(const ExperimentOptions& options) {
  EncoderParams params;
  params.rate_control = options.cbr ? RateControl::kCbr : RateControl::kVbr;
  params.intra_refresh_cycle = options.intra_refresh;
  params.slices = options.slices;
  params.vbv_frames = options.vbv_frames;
  params.vbv_ms = options.vbv_ms;
  params.max_frame_factor = options.max_frame_factor;
  params.low_power = options.low_power;
  params.target_usage = options.target_usage;
  params.nvenc_preset = options.nvenc_preset;
  return params;
}

EncoderParams EncoderParams::ForStream(int stream_width,
                                       int stream_height,
                                       int stream_fps,
                                       int target,
                                       int min,
                                       int max) const {
  EncoderParams params = *this;
  params.width = stream_width;
  params.height = stream_height;
  params.fps = stream_fps;
  params.target_kbps = target;
  params.min_kbps = min;
  params.max_kbps = max;
  return params;
}

int64_t EncoderParams::VbvKb() const {
  const int64_t numerator = int64_t{max_kbps} * (vbv_ms ? *vbv_ms : vbv_frames);
  const int denominator = vbv_ms ? 8000 : 8 * fps;
  return (numerator + denominator - 1) / denominator;
}

uint32_t EncoderParams::FrameCapBytes() const {
  return rate_control == RateControl::kCbr
             ? 0
             : static_cast<uint32_t>(max_frame_factor.value_or(1) *
                                     (max_frame_factor ? target_kbps
                                                       : max_kbps) *
                                     1000 / (8 * fps));
}

int EncoderParams::ResolvedLevel() const {
  return level ? level : AvcLevelFor(width, height, fps, max_kbps);
}

}  // namespace re
