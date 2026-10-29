// Copyright 2026 The remotekit Authors
// Use of this source code is governed by a BSD-style license.

#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#pragma allow_unsafe_libc_calls
#endif

#include "re/encoder_factory.h"

#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <map>
#include <utility>

#include "base/logging.h"
#include "base/synchronization/lock.h"
#include "base/time/time.h"
#include "re/experiment_options.h"
#include "re/frame_refresh.h"
#include "re/latency_stats.h"
#include "re/vpl_encoder.h"
#include "third_party/webrtc/api/units/time_delta.h"
#include "third_party/webrtc/api/video_codecs/video_codec.h"
#include "third_party/webrtc/api/video_codecs/video_encoder_software_fallback_wrapper.h"
#include "third_party/webrtc/media/engine/internal_encoder_factory.h"
#include "third_party/webrtc/modules/video_coding/include/video_error_codes.h"
#include "third_party/webrtc_overrides/rtc_base/logging.h"

namespace re {
namespace {

webrtc::SdpVideoFormat H264Format() {
  return {"H264",
          {{"profile-level-id", "42e01f"},
           {"packetization-mode", "1"},
           {"level-asymmetry-allowed", "1"}}};
}

// Output decoration also applies after the stock software fallback activates.
class LowDelayOutput final : public webrtc::VideoEncoder,
                             public webrtc::EncodedImageCallback {
 public:
  LowDelayOutput(std::unique_ptr<webrtc::VideoEncoder> encoder,
                 std::shared_ptr<FrameRefresh> refresh,
                 bool h264)
      : encoder_(std::move(encoder)), refresh_(std::move(refresh)), h264_(h264) {}
  int InitEncode(const webrtc::VideoCodec* codec,
                 const Settings& settings) override {
    if (!codec)
      return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    // Generic high-motion fallback uses camera mode. This only affects
    // software; VPL always emits screen content metadata and uses the same VBR
    // settings.
    auto copy = *codec;
    copy.mode = webrtc::VideoCodecMode::kRealtimeVideo;
    auto limited = settings;
    limited.encoder_thread_limit =
        std::clamp(settings.encoder_thread_limit.value_or(4), 1, 4);
    const int result = encoder_->InitEncode(&copy, limited);
    LOG(INFO) << "PRTS encoder InitEncode " << codec->width << "x"
              << codec->height << " max_fps=" << codec->maxFramerate
              << " start_kbps=" << codec->startBitrate
              << " min_kbps=" << codec->minBitrate
              << " max_kbps=" << codec->maxBitrate
              << " cores=" << settings.number_of_cores
              << " impl=" << encoder_->GetEncoderInfo().implementation_name
              << " status=" << result;
    return result;
  }
  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override {
    callback_ = callback;
    return encoder_->RegisterEncodeCompleteCallback(callback ? this : nullptr);
  }
  int32_t Release() override { return encoder_->Release(); }
  int32_t Encode(const webrtc::VideoFrame& frame,
                 const std::vector<webrtc::VideoFrameType>* types) override {
    const auto now = base::TimeTicks::Now();
    refresh_->Encode((now - base::TimeTicks()).InMicroseconds());
    const auto capture =
        base::TimeTicks() + base::Microseconds(frame.timestamp_us());
    LatencyStats::Get().Add("capture_to_encode", capture, now);
    LatencyStats::Get().EncodeStart(frame.timestamp_us(), now);
    if (TraceEnabled()) {  // m3-trace
      LOG(INFO) << "PRTS TR E ce=" << frame.timestamp_us()
                << " rtp=" << frame.rtp_timestamp() << " t0=" << TraceUs(now);
    }
    {
      base::AutoLock lock(timing_lock_);
      timing_[frame.rtp_timestamp()] = {capture, now};
      if (timing_.size() > 240) {
        timing_.erase(std::min_element(
            timing_.begin(), timing_.end(), [](const auto& a, const auto& b) {
              return a.second.second < b.second.second;
            }));
      }
    }
    const int32_t result = encoder_->Encode(frame, types);
    refresh_->EncodeFinished(
        (now - base::TimeTicks()).InMicroseconds(),
        (base::TimeTicks::Now() - base::TimeTicks()).InMicroseconds());
    if (result != WEBRTC_VIDEO_CODEC_OK)
      refresh_->Dropped(
          (base::TimeTicks::Now() - base::TimeTicks()).InMicroseconds());
    return result;
  }
  void SetRates(const RateControlParameters& p) override {
    encoder_->SetRates(p);
    refresh_->Rates(p.bitrate.get_sum_bps());
  }
  void SetFecControllerOverride(webrtc::FecControllerOverride* f) override {
    encoder_->SetFecControllerOverride(f);
  }
  void OnPacketLossRateUpdate(float loss) override {
    encoder_->OnPacketLossRateUpdate(loss);
  }
  void OnRttUpdate(int64_t rtt) override { encoder_->OnRttUpdate(rtt); }
  void OnLossNotification(const LossNotification& n) override {
    encoder_->OnLossNotification(n);
  }
  EncoderInfo GetEncoderInfo() const override {
    return encoder_->GetEncoderInfo();
  }
  Result OnEncodedImage(const webrtc::EncodedImage& image,
                        const webrtc::CodecSpecificInfo* info) override {
    const auto encoded = base::TimeTicks::Now();
    int64_t encode_cost = 0;
    if (TraceEnabled()) {  // m3-trace
      LOG(INFO) << "PRTS TR X rtp=" << image.RtpTimestamp()
                << " t1=" << TraceUs(encoded) << " bytes=" << image.size()
                << " key=" << (image.FrameType() == webrtc::VideoFrameType::kVideoFrameKey);
    }
    {
      base::AutoLock lock(timing_lock_);
      auto it = timing_.find(image.RtpTimestamp());
      if (it != timing_.end()) {
        encode_cost = (encoded - it->second.second).InMicroseconds();
        LatencyStats::Get().Add("encode", it->second.second, encoded);
        LatencyStats::Get().Add("capture_to_encoded", it->second.first,
                                encoded);
        timing_.erase(it);
      }
    }
    const bool trusted = encoder_->GetEncoderInfo().is_qp_trusted.value_or(true);
    // VP8's quantizer range is 0..127; normalize its convergence threshold 32
    // to the H264 threshold 24. Never trust a hardware slice-header QP.
    const int qp = image.qp_ < 0 || h264_ ? image.qp_ : (image.qp_ * 3 + 3) / 4;
    refresh_->Encoded((encoded - base::TimeTicks()).InMicroseconds(),
                      encode_cost, image.size(),
                      size_t{image._encodedWidth} * image._encodedHeight,
                      image.FrameType() == webrtc::VideoFrameType::kVideoFrameKey,
                      qp, trusted);
    auto output = image;  // Encoded buffer is refcounted, no payload copy.
    SetPlayoutDelay(output);
    output.content_type_ = webrtc::VideoContentType::SCREENSHARE;
    const auto handed = base::TimeTicks::Now();
    LatencyStats::Get().Add("encoded_to_handoff", encoded, handed);
    auto result = callback_->OnEncodedImage(output, info);
    LatencyStats::Get().Add("handoff_callback", handed, base::TimeTicks::Now());
    return result;
  }
  void OnFrameDropped(uint32_t ts, int spatial, bool end) override {
    refresh_->Dropped(
        (base::TimeTicks::Now() - base::TimeTicks()).InMicroseconds());
    {
      base::AutoLock lock(timing_lock_);
      timing_.erase(ts);
    }
    callback_->OnFrameDropped(ts, spatial, end);
  }

 private:
  std::unique_ptr<webrtc::VideoEncoder> encoder_;
  std::shared_ptr<FrameRefresh> refresh_;
  const bool h264_;
  webrtc::EncodedImageCallback* callback_ = nullptr;  // Unregistered by owner.
  base::Lock timing_lock_;
  std::map<uint32_t, std::pair<base::TimeTicks, base::TimeTicks>> timing_;
};

// The pinned built-in OpenH264 encoder; always the fallback of the others.
class OpenH264Backend final : public VideoEncoderBackend {
 public:
  const char* name() const override { return "openh264"; }
  const char* label() const override { return "OpenH264"; }
  bool Probe() override {
    webrtc::InternalEncoderFactory builtin;
    for (const auto& format : builtin.GetSupportedFormats()) {
      if (format.name == "H264" && format.parameters == H264Format().parameters)
        return true;
    }
    return false;
  }
  std::unique_ptr<webrtc::VideoEncoder> Create(
      const webrtc::Environment& env,
      const webrtc::SdpVideoFormat& format,
      const EncoderParams&) override {
    webrtc::InternalEncoderFactory builtin;
    return builtin.Create(env, format);
  }
};

struct Vendors {
  bool intel = false;
  bool nvidia = false;
};

Vendors InstalledVendors() {
  Vendors vendors;
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
    return vendors;
  for (UINT index = 0;; ++index) {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND)
      break;
    DXGI_ADAPTER_DESC1 desc{};
    if (!adapter || FAILED(adapter->GetDesc1(&desc)) ||
        (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
      continue;
    vendors.intel |= desc.VendorId == 0x8086;
    vendors.nvidia |= desc.VendorId == 0x10de;
  }
  return vendors;
}

}  // namespace

void SetPlayoutDelay(webrtc::EncodedImage& output) {
  const auto& options = GetExperimentOptions();
  if (options.playout_metadata) {
    output.SetPlayoutDelay(webrtc::VideoPlayoutDelay(
        webrtc::TimeDelta::Millis(options.playout_min_ms),
        webrtc::TimeDelta::Millis(options.playout_max_ms)));
  } else {
    output.SetPlayoutDelay(std::nullopt);
  }
}

bool ParseEncoderChoice(std::string_view value, EncoderChoice* choice) {
  for (auto candidate : {EncoderChoice::kAuto, EncoderChoice::kVpl,
                         EncoderChoice::kNvenc, EncoderChoice::kOpenH264}) {
    if (value == EncoderChoiceName(candidate)) {
      *choice = candidate;
      return true;
    }
  }
  return false;
}

const char* EncoderChoiceName(EncoderChoice choice) {
  switch (choice) {
    case EncoderChoice::kAuto:
      return "auto";
    case EncoderChoice::kVpl:
      return "vpl";
    case EncoderChoice::kNvenc:
      return "nvenc";
    case EncoderChoice::kOpenH264:
      return "openh264";
  }
  return "openh264";
}

std::unique_ptr<VideoEncoderBackend> CreateOpenH264Backend() {
  return std::make_unique<OpenH264Backend>();
}

EncoderFactory::EncoderFactory(std::shared_ptr<VplEncoderState> state,
                               EncoderChoice choice,
                               std::shared_ptr<FrameRefresh> refresh)
    : software_(CreateOpenH264Backend()),
      base_(EncoderParams::FromOptions(GetExperimentOptions())),
      refresh_(std::move(refresh)) {
  std::vector<std::unique_ptr<VideoEncoderBackend>> candidates;
  if (choice == EncoderChoice::kVpl) {
    candidates.push_back(CreateVplBackend(state));
  } else if (choice == EncoderChoice::kNvenc) {
    candidates.push_back(CreateNvencBackend(state));
  } else if (choice == EncoderChoice::kAuto) {
    const Vendors vendors = InstalledVendors();
    if (vendors.intel)
      candidates.push_back(CreateVplBackend(state));
    if (vendors.nvidia)
      candidates.push_back(CreateNvencBackend(state));
  }
  for (auto& candidate : candidates) {
    if (candidate->Probe()) {
      hardware_ = std::move(candidate);
      break;
    }
    RTC_LOG(LS_WARNING) << "PRTS " << candidate->label()
                        << " unavailable; fallback encoder=openh264";
  }
  LOG(INFO) << "PRTS encoder selected="
            << (hardware_ ? hardware_->name() : "openh264");
}
EncoderFactory::~EncoderFactory() = default;
std::vector<webrtc::SdpVideoFormat> EncoderFactory::GetSupportedFormats()
    const {
  std::vector<webrtc::SdpVideoFormat> formats;
  if (software_->Probe())
    formats.push_back(H264Format());
  // Some Android devices and non-proprietary Chromium builds have no H.264.
  // InternalEncoderFactory supplies libvpx VP8 independently of that backend.
  webrtc::InternalEncoderFactory builtin;
  for (const auto& format : builtin.GetSupportedFormats()) {
    if (format.name == "VP8")
      formats.push_back(format);
  }
  return formats;
}
std::unique_ptr<webrtc::VideoEncoder> EncoderFactory::Create(
    const webrtc::Environment& env,
    const webrtc::SdpVideoFormat& format) {
  if (format.name == "VP8") {
    webrtc::InternalEncoderFactory builtin;
    auto encoder = builtin.Create(env, format);
    if (!encoder)
      return nullptr;
    LOG(INFO) << "PRTS codec selected=VP8 encoder=libvpx";
    return std::make_unique<LowDelayOutput>(std::move(encoder), refresh_, false);
  }
  if (format.name != "H264" || format.parameters != H264Format().parameters)
    return nullptr;
  auto software = software_->Create(env, format, base_);
  if (!software)
    return nullptr;
  LOG(INFO) << "PRTS codec selected=H264 encoder="
            << (hardware_ ? hardware_->name() : "openh264");
  if (!hardware_)
    return std::make_unique<LowDelayOutput>(std::move(software), refresh_, true);
  auto encoder = webrtc::CreateVideoEncoderSoftwareFallbackWrapper(
      env, std::move(software), hardware_->Create(env, format, base_), false);
  return std::make_unique<LowDelayOutput>(std::move(encoder), refresh_, true);
}

}  // namespace re
