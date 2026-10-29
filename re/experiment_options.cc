// Copyright 2026 The remotekit Authors
// Use of this source code is governed by a BSD-style license.

#include "re/experiment_options.h"

#include <cmath>
#include <cstdint>

#include "base/command_line.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"

namespace re {
namespace {

ExperimentOptions ParseOptions() {
  const auto& command = *base::CommandLine::ForCurrentProcess();
  ExperimentOptions out;
  auto invalid = [&out](const char* message) {
    LOG(ERROR) << message;
    out.valid = false;
  };
  auto integer = [&](const char* name, int low, int high, int& value) {
    if (command.HasSwitch(name) &&
        (!base::StringToInt(command.GetSwitchValueASCII(name), &value) ||
         value < low || value > high)) {
      LOG(ERROR) << "--" << name << " must be in [" << low << ',' << high
                 << ']';
      out.valid = false;
    }
  };
  if (command.HasSwitch("vpl-rc")) {
    const auto value = command.GetSwitchValueASCII("vpl-rc");
    if (value != "vbr" && value != "cbr")
      invalid("--vpl-rc must be vbr or cbr");
    out.cbr = value == "cbr";
  }
  if (command.HasSwitch("vpl-max-frame-factor")) {
    double factor = 0;
    if (!base::StringToDouble(
            command.GetSwitchValueASCII("vpl-max-frame-factor"), &factor) ||
        !std::isfinite(factor) || factor < 0.1 || factor > 100) {
      invalid("--vpl-max-frame-factor must be finite and in [0.1,100]");
    }
    out.max_frame_factor = factor;
  }
  integer("vpl-vbv-frames", 1, 120, out.vbv_frames);
  if (command.HasSwitch("vpl-vbv-ms")) {
    int ms = 0;
    integer("vpl-vbv-ms", 1, 1000, ms);
    out.vbv_ms = ms;
    if (command.HasSwitch("vpl-vbv-frames"))
      invalid("Use only one of --vpl-vbv-frames and --vpl-vbv-ms");
  }
  integer("vpl-tu", 1, 7, out.target_usage);
  if (command.HasSwitch("vpl-lowpower")) {
    const auto value = command.GetSwitchValueASCII("vpl-lowpower");
    if (value != "on" && value != "off")
      invalid("--vpl-lowpower must be on or off");
    out.low_power = value == "on";
  }
  integer("vpl-intra-refresh", 0, 65535, out.intra_refresh);
  if (out.intra_refresh == 1)
    invalid("--vpl-intra-refresh must be 0 (off) or [2,65535] frames");
  integer("vpl-slices", 1, 256, out.slices);
  integer("nvenc-preset", 1, 3, out.nvenc_preset);
  if (command.HasSwitch("vpl-profile")) {
    const auto value = command.GetSwitchValueASCII("vpl-profile");
    if (value == "high") {
      invalid(
          "--vpl-profile=high gated: pinned OpenH264 fallback does not "
          "advertise High; SDP and fallback must support the same profile");
    } else if (value != "cb") {
      invalid("--vpl-profile must be cb or high (high currently gated)");
    }
  }
  // AVC level 4.2 in this endpoint caps experiments at 50 Mbps. Reject larger
  // inputs before narrowing to mfxU16 (in particular kbps > 65535).
  integer("min-kbps", 1, 50000, out.min_kbps);
  integer("start-kbps", 1, 50000, out.start_kbps);
  integer("max-kbps", 1, 50000, out.max_kbps);
  if (out.min_kbps > out.start_kbps || out.start_kbps > out.max_kbps)
    invalid("Require min-kbps <= start-kbps <= max-kbps");
  int fps = 60;
  integer("fps", 1, 120, fps);
  if (out.valid) {
    const int64_t numerator =
        int64_t{out.max_kbps} * (out.vbv_ms ? *out.vbv_ms : out.vbv_frames);
    const int denominator = out.vbv_ms ? 8000 : 8 * fps;
    if ((numerator + denominator - 1) / denominator > 65535)
      invalid("VBV exceeds mfxU16 KB; reduce VBV or max-kbps");
    if (out.max_frame_factor &&
        *out.max_frame_factor * out.max_kbps * 1000 / (8 * fps) >
            16 * 1024 * 1024) {
      invalid("MaxFrameSize exceeds endpoint 16 MiB bitstream limit");
    }
  }
  // Preserve the older diagnostic switch: it suppressed frame metadata but
  // still negotiated/required the extension. --playout-delay takes precedence.
  out.playout_metadata = !command.HasSwitch("latency-no-playout-delay");
  if (command.HasSwitch("playout-delay")) {
    const auto value = command.GetSwitchValueASCII("playout-delay");
    out.playout_extension = value != "none";
    out.playout_metadata = out.playout_extension;
    if (out.playout_extension) {
      const auto parts = base::SplitString(value, ",", base::TRIM_WHITESPACE,
                                           base::SPLIT_WANT_ALL);
      if (parts.size() != 2 ||
          !base::StringToInt(parts[0], &out.playout_min_ms) ||
          !base::StringToInt(parts[1], &out.playout_max_ms) ||
          out.playout_min_ms < 0 || out.playout_max_ms < out.playout_min_ms ||
          out.playout_max_ms > 40950 || out.playout_min_ms % 10 ||
          out.playout_max_ms % 10) {
        invalid(
            "--playout-delay must be none or min,max in ms: "
            "0 <= min <= max <= 40950, multiples of 10 (RTP units)");
      }
    }
  }
  if (command.HasSwitch("fec")) {
    out.fec = command.GetSwitchValueASCII("fec");
    if (out.fec != "off" && out.fec != "ulpfec" && out.fec != "flexfec")
      invalid("--fec must be off, ulpfec or flexfec");
  }
  LOG(INFO) << "PRTS experiment options"
            << " rc=" << (out.cbr ? "cbr" : "vbr")
            << " max_frame_factor=" << out.max_frame_factor.value_or(1)
            << " cap_basis=" << (out.max_frame_factor ? "target" : "max")
            << " cap_policy=init_only"
            << " vbv_frames=" << out.vbv_frames
            << " vbv_ms=" << out.vbv_ms.value_or(0)
            << " tu=" << out.target_usage
            << " lowpower=" << (out.low_power ? "on" : "off")
            << " intra_refresh=" << out.intra_refresh
            << " slices=" << out.slices
            << " profile=cb min_kbps=" << out.min_kbps
            << " start_kbps=" << out.start_kbps << " max_kbps=" << out.max_kbps
            << " fps=" << fps << " playout_extension=" << out.playout_extension
            << " playout_metadata=" << out.playout_metadata
            << " playout_min_ms=" << out.playout_min_ms
            << " playout_max_ms=" << out.playout_max_ms << " fec=" << out.fec
            << " valid=" << out.valid;
  return out;
}

}  // namespace

const ExperimentOptions& GetExperimentOptions() {
  static const base::NoDestructor<ExperimentOptions> options(ParseOptions());
  return *options;
}

}  // namespace re
