// Copyright 2026 The remotekit Authors
// Use of this source code is governed by a BSD-style license.

#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#pragma allow_unsafe_libc_calls
#endif

#include "rb_legacy_endpoint/vpl_encoder.h"

#include <d3d10.h>
#include <dxgi1_2.h>
#include <vpl/mfxdispatcher.h>
#include <vpl/mfxvideo.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstring>
#include <map>
#include <new>
#include <optional>
#include <span>
#include <thread>
#include <utility>

#include "base/command_line.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/lock.h"
#include "rb_legacy_endpoint/experiment_options.h"
#include "rb_legacy_endpoint/latency_stats.h"
#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/libyuv/include/libyuv/convert_from.h"
#include "third_party/webrtc/api/units/time_delta.h"
#include "third_party/webrtc/api/video/encoded_image.h"
#include "third_party/webrtc/api/video/i420_buffer.h"
#include "third_party/webrtc/api/video/video_frame.h"
#include "third_party/webrtc/api/video_codecs/video_codec.h"
#include "third_party/webrtc/api/video_codecs/video_encoder_software_fallback_wrapper.h"
#include "third_party/webrtc/media/engine/internal_encoder_factory.h"
#include "third_party/webrtc/modules/video_coding/include/video_codec_interface.h"
#include "third_party/webrtc/modules/video_coding/include/video_error_codes.h"
#include "third_party/webrtc/rtc_base/time_utils.h"
#include "third_party/webrtc_overrides/rtc_base/logging.h"

namespace re {
namespace {
using Microsoft::WRL::ComPtr;
using SteadyClock = std::chrono::steady_clock;
constexpr char kNativeTag[] = "rb-owned-nv12-v1";
constexpr size_t kMaxBitstreamBytes = 16 * 1024 * 1024;
constexpr auto kBusyTimeout = std::chrono::milliseconds(100);

bool ValidSize(int width, int height) {
  return width > 0 && height > 0 && width <= 4096 && height <= 4096 &&
         !(width & 1) && !(height & 1) &&
         static_cast<int64_t>(width) * height <= 1920 * 1080;
}

// Annex A limits for constrained baseline. Never understate the
// actual SPS level to match a nominal SDP string. Odd padded aspect ratios may
// exceed MaxFS even if the visible pixel count fits the viewport budget.
mfxU16 AvcLevel(int width, int height, int fps, int kbps) {
  const int mb_width = (width + 15) / 16;
  const int mb_height = (height + 15) / 16;
  const int macroblocks = mb_width * mb_height;
  struct Level {
    mfxU16 id;
    int fs;
    int mbps;
    int kbps;
  };
  constexpr Level levels[] = {{MFX_LEVEL_AVC_31, 3600, 108000, 14000},
                              {MFX_LEVEL_AVC_32, 5120, 216000, 20000},
                              {MFX_LEVEL_AVC_4, 8192, 245760, 20000},
                              {MFX_LEVEL_AVC_42, 8704, 522240, 50000}};
  for (const auto& level : levels) {
    if (macroblocks <= level.fs && macroblocks * fps <= level.mbps &&
        kbps <= level.kbps && mb_width * mb_width <= level.fs * 8 &&
        mb_height * mb_height <= level.fs * 8)
      return level.id;
  }
  return 0;
}

webrtc::SdpVideoFormat H264Format() {
  return {"H264",
          {{"profile-level-id", "42e01f"},
           {"packetization-mode", "1"},
           {"level-asymmetry-allowed", "1"}}};
}

int Align16(int value) {
  return (value + 15) & ~15;
}

bool ValidNv12(ID3D11Texture2D* texture, int width, int height) {
  if (!texture || width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
      static_cast<int64_t>(width) * height > 1920 * 1080 || (width & 1) ||
      (height & 1)) {
    return false;
  }
  D3D11_TEXTURE2D_DESC desc{};
  texture->GetDesc(&desc);
  return desc.Format == DXGI_FORMAT_NV12 && desc.ArraySize == 1 &&
         desc.MipLevels == 1 && desc.SampleDesc.Count == 1 &&
         desc.Width == static_cast<UINT>(Align16(width)) &&
         desc.Height == static_cast<UINT>(Align16(height)) &&
         desc.Usage == D3D11_USAGE_DEFAULT &&
         (desc.BindFlags & D3D11_BIND_RENDER_TARGET);
}

// These pointers are allocator-owned and remain stable until Free. MFX passes
// their address back as MemId; GetHDL must write an mfxHDLPair, not one handle.
struct TextureMemory {
  ComPtr<ID3D11Texture2D> texture;
};

class TextureAllocator {
 public:
  explicit TextureAllocator(ID3D11Device* device) : device_(device) {
    api.pthis = this;
    api.Alloc = Allocate;
    api.Free = Free;
    api.Lock = Lock;
    api.Unlock = Unlock;
    api.GetHDL = GetHandle;
  }

  static mfxStatus MFX_CDECL Allocate(mfxHDL self,
                                      mfxFrameAllocRequest* request,
                                      mfxFrameAllocResponse* response) {
    if (!self || !request || !response)
      return MFX_ERR_NULL_PTR;
    if (request->Info.FourCC != MFX_FOURCC_NV12 ||
        (request->Type & MFX_MEMTYPE_SYSTEM_MEMORY) ||
        !request->NumFrameSuggested || request->NumFrameSuggested > 32) {
      return MFX_ERR_UNSUPPORTED;
    }
    *response = {};
    response->AllocId = request->AllocId;
    response->mids = new (std::nothrow) mfxMemId[request->NumFrameSuggested]{};
    if (!response->mids)
      return MFX_ERR_MEMORY_ALLOC;
    auto* allocator = static_cast<TextureAllocator*>(self);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = request->Info.Width;
    desc.Height = request->Info.Height;
    desc.Format = DXGI_FORMAT_NV12;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (request->Type & MFX_MEMTYPE_INTERNAL_FRAME) {
      desc.BindFlags = D3D11_BIND_DECODER;
      if (request->Type & MFX_MEMTYPE_VIDEO_MEMORY_ENCODER_TARGET)
        desc.BindFlags |= D3D11_BIND_VIDEO_ENCODER;
    }
    for (mfxU16 i = 0; i < request->NumFrameSuggested; ++i) {
      auto memory =
          std::unique_ptr<TextureMemory>(new (std::nothrow) TextureMemory);
      if (!memory || FAILED(allocator->device_->CreateTexture2D(
                         &desc, nullptr, &memory->texture))) {
        Free(self, response);
        return MFX_ERR_MEMORY_ALLOC;
      }
      response->mids[i] = memory.release();
      ++response->NumFrameActual;
    }
    return MFX_ERR_NONE;
  }

  static mfxStatus MFX_CDECL Free(mfxHDL, mfxFrameAllocResponse* response) {
    if (!response)
      return MFX_ERR_NULL_PTR;
    for (mfxU16 i = 0; i < response->NumFrameActual; ++i)
      delete static_cast<TextureMemory*>(response->mids[i]);
    delete[] response->mids;
    *response = {};
    return MFX_ERR_NONE;
  }
  static mfxStatus MFX_CDECL Lock(mfxHDL, mfxMemId, mfxFrameData*) {
    return MFX_ERR_UNSUPPORTED;  // Video memory only; no CPU map requested.
  }
  static mfxStatus MFX_CDECL Unlock(mfxHDL, mfxMemId, mfxFrameData*) {
    return MFX_ERR_NONE;
  }
  static mfxStatus MFX_CDECL GetHandle(mfxHDL, mfxMemId mid, mfxHDL* handle) {
    if (!mid || !handle)
      return MFX_ERR_NULL_PTR;
    auto* pair = reinterpret_cast<mfxHDLPair*>(handle);
    pair->first = static_cast<TextureMemory*>(mid)->texture.Get();
    pair->second = nullptr;  // Subresource zero, non-array texture.
    return MFX_ERR_NONE;
  }
  mfxFrameAllocator api{};

 private:
  ComPtr<ID3D11Device> device_;
};

// Rebind extension pointers after every copy; copying mfxVideoParam alone would
// leave dangling pointers into another stack object.
struct Parameters {
  Parameters() { Bind(); }
  Parameters(const Parameters& other)
      : p(other.p), co2(other.co2), co3(other.co3), vui(other.vui) {
    Bind();
  }
  Parameters& operator=(const Parameters& other) {
    p = other.p;
    co2 = other.co2;
    co3 = other.co3;
    vui = other.vui;
    Bind();
    return *this;
  }
  void Bind() {
    co2.Header = {MFX_EXTBUFF_CODING_OPTION2, sizeof(co2)};
    co3.Header = {MFX_EXTBUFF_CODING_OPTION3, sizeof(co3)};
    vui.Header = {MFX_EXTBUFF_VIDEO_SIGNAL_INFO, sizeof(vui)};
    extensions[0] = reinterpret_cast<mfxExtBuffer*>(&co2);
    extensions[1] = reinterpret_cast<mfxExtBuffer*>(&co3);
    extensions[2] = reinterpret_cast<mfxExtBuffer*>(&vui);
    p.ExtParam = extensions;
    p.NumExtParam = 3;
  }
  mfxVideoParam p{};
  mfxExtCodingOption2 co2{};
  mfxExtCodingOption3 co3{};
  mfxExtVideoSignalInfo vui{};
  mfxExtBuffer* extensions[3]{};
};

void SetRateFields(Parameters& params, int kbps, int max_kbps, int fps) {
  const auto& options = GetExperimentOptions();
  auto& m = params.p.mfx;
  // CLI validation bounds rates and VBV before any mfxU16 narrowing. Treat
  // multiplier=0 as 1 on readback; writes use explicit unscaled units.
  m.BRCParamMultiplier = 1;
  m.TargetKbps = static_cast<mfxU16>(kbps);
  m.MaxKbps = static_cast<mfxU16>(max_kbps);
  m.FrameInfo.FrameRateExtN = fps;
  m.FrameInfo.FrameRateExtD = 1;
  // Allocate once for the negotiated maximum; rate resets never change VBV.
  const int64_t numerator =
      int64_t{max_kbps} *
      (options.vbv_ms ? *options.vbv_ms : options.vbv_frames);
  const int denominator = options.vbv_ms ? 8000 : 8 * fps;
  m.BufferSizeInKB =
      static_cast<mfxU16>((numerator + denominator - 1) / denominator);
  m.InitialDelayInKB = m.BufferSizeInKB / 2;
  // MaxFrameSize is VBR-only. It is not in the seamless-reset field list in
  // mfxExtEncoderResetOption: apply the explicit target-based factor at Init,
  // retain it on Reset. Defaults retain the old max-rate one-frame cap.
  params.co2.MaxFrameSize =
      options.cbr
          ? 0
          : static_cast<mfxU32>(options.max_frame_factor.value_or(1) *
                                (options.max_frame_factor ? kbps : max_kbps) *
                                1000 / (8 * fps));
}

int64_t EffectiveBrc(const mfxInfoMFX& m, mfxU16 value) {
  return int64_t{value} * std::max<mfxU16>(1, m.BRCParamMultiplier);
}

void LogParameters(const char* phase, mfxStatus status, const Parameters& p) {
  const auto& options = GetExperimentOptions();
  LOG(INFO) << "PRTS VPL " << phase << " status=" << status
            << " kbps=" << EffectiveBrc(p.p.mfx, p.p.mfx.TargetKbps) << '/'
            << EffectiveBrc(p.p.mfx, p.p.mfx.MaxKbps)
            << " fps=" << p.p.mfx.FrameInfo.FrameRateExtN
            << " VBV_KB=" << EffectiveBrc(p.p.mfx, p.p.mfx.BufferSizeInKB)
            << " initial_KB=" << EffectiveBrc(p.p.mfx, p.p.mfx.InitialDelayInKB)
            << " brc_multiplier=" << p.p.mfx.BRCParamMultiplier
            << " cap_bytes=" << p.co2.MaxFrameSize
            << " LDB=" << p.co3.LowDelayBRC << " LP=" << p.p.mfx.LowPower
            << " rate_control=" << p.p.mfx.RateControlMethod
            << " AsyncDepth=" << p.p.AsyncDepth
            << " level=" << p.p.mfx.CodecLevel
            << " profile=" << p.p.mfx.CodecProfile << " profile_option=cb"
            << " tu=" << p.p.mfx.TargetUsage << " slices=" << p.p.mfx.NumSlice
            << " intra_refresh_type=" << p.co2.IntRefType
            << " intra_refresh=" << p.co2.IntRefCycleSize
            << " rc=" << (options.cbr ? "cbr" : "vbr")
            << " max_frame_factor=" << options.max_frame_factor.value_or(1)
            << " cap_basis=" << (options.max_frame_factor ? "target" : "max")
            << " cap_policy=init_only"
            << " cap_active=" << !options.cbr
            << " vbv_frames=" << options.vbv_frames
            << " vbv_ms=" << options.vbv_ms.value_or(0)
            << " lowpower=" << (options.low_power ? "on" : "off");
}

bool RoundedField(const char* name,
                  int64_t requested,
                  int64_t actual,
                  int64_t quantum) {
  if (requested == actual)
    return true;
  // Zero must stay zero (disabled/unspecified), never accept a disabled rate
  // or frame cap as rounding. Bound both absolute granularity and 10% drift.
  const int64_t delta =
      actual > requested ? actual - requested : requested - actual;
  if (!requested || !actual || delta > std::max(quantum, requested / 10)) {
    LOG(ERROR) << "PRTS VPL incompatible " << name << " requested=" << requested
               << " actual=" << actual;
    return false;
  }
  LOG(INFO) << "PRTS VPL rounded " << name << " requested=" << requested
            << " actual=" << actual;
  return true;
}

bool Compatible(const Parameters& requested,
                const Parameters& actual,
                bool allow_vbv_minimum = false) {
  const auto& a = actual.p.mfx;
  const auto& r = requested.p.mfx;
  return actual.p.AsyncDepth == 1 &&
         actual.p.IOPattern == MFX_IOPATTERN_IN_VIDEO_MEMORY &&
         a.CodecId == MFX_CODEC_AVC && a.CodecProfile == r.CodecProfile &&
         a.CodecLevel == r.CodecLevel && a.LowPower == r.LowPower &&
         a.TargetUsage == r.TargetUsage && a.GopPicSize == 65535 &&
         a.IdrInterval == 0 && a.RateControlMethod == r.RateControlMethod &&
         RoundedField("TargetKbps", EffectiveBrc(r, r.TargetKbps),
                      EffectiveBrc(a, a.TargetKbps), 16) &&
         RoundedField("MaxKbps", EffectiveBrc(r, r.MaxKbps),
                      EffectiveBrc(a, a.MaxKbps), 16) &&
         // UHD630 M3 readback was 101/50 KB for a 50/25 KB request. Preserve
         // that existing driver minimum, with explicit requested/actual logs.
         RoundedField("BufferSizeInKB", EffectiveBrc(r, r.BufferSizeInKB),
                      EffectiveBrc(a, a.BufferSizeInKB),
                      allow_vbv_minimum ? 64 : 2) &&
         RoundedField("InitialDelayInKB", EffectiveBrc(r, r.InitialDelayInKB),
                      EffectiveBrc(a, a.InitialDelayInKB),
                      allow_vbv_minimum ? 32 : 2) &&
         a.GopRefDist == 1 && a.NumRefFrame == 1 && a.NumSlice == r.NumSlice &&
         a.FrameInfo.FourCC == MFX_FOURCC_NV12 &&
         a.FrameInfo.Width == r.FrameInfo.Width &&
         a.FrameInfo.Height == r.FrameInfo.Height &&
         a.FrameInfo.CropW == r.FrameInfo.CropW &&
         a.FrameInfo.CropH == r.FrameInfo.CropH && a.FrameInfo.CropX == 0 &&
         a.FrameInfo.CropY == 0 &&
         a.FrameInfo.ChromaFormat == MFX_CHROMAFORMAT_YUV420 &&
         a.FrameInfo.PicStruct == MFX_PICSTRUCT_PROGRESSIVE &&
         a.FrameInfo.FrameRateExtN == r.FrameInfo.FrameRateExtN &&
         a.FrameInfo.FrameRateExtD == 1 &&
         (r.RateControlMethod != MFX_RATECONTROL_VBR ||
          actual.co3.LowDelayBRC == requested.co3.LowDelayBRC) &&
         actual.co2.AdaptiveI == MFX_CODINGOPTION_OFF &&
         actual.co2.AdaptiveB == MFX_CODINGOPTION_OFF &&
         actual.co2.MBBRC == MFX_CODINGOPTION_OFF &&
         actual.co2.IntRefType == requested.co2.IntRefType &&
         (!requested.co2.IntRefType ||
          actual.co2.IntRefCycleSize == requested.co2.IntRefCycleSize) &&
         (r.RateControlMethod != MFX_RATECONTROL_VBR ||
          RoundedField("MaxFrameSize", requested.co2.MaxFrameSize,
                       actual.co2.MaxFrameSize, 1024)) &&
         actual.vui.ColourDescriptionPresent == 1 &&
         actual.vui.ColourPrimaries == 1 &&
         actual.vui.TransferCharacteristics == 1 &&
         actual.vui.MatrixCoefficients == 1 && actual.vui.VideoFullRange == 0;
}

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

// Output decoration also applies after the stock software fallback activates.
class LowDelayOutput final : public webrtc::VideoEncoder,
                             public webrtc::EncodedImageCallback {
 public:
  explicit LowDelayOutput(std::unique_ptr<webrtc::VideoEncoder> encoder)
      : encoder_(std::move(encoder)) {}
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
    return encoder_->Encode(frame, types);
  }
  void SetRates(const RateControlParameters& p) override {
    encoder_->SetRates(p);
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
    if (TraceEnabled()) {  // m3-trace
      LOG(INFO) << "PRTS TR X rtp=" << image.RtpTimestamp()
                << " t1=" << TraceUs(encoded) << " bytes=" << image.size()
                << " key=" << (image.FrameType() == webrtc::VideoFrameType::kVideoFrameKey);
    }
    {
      base::AutoLock lock(timing_lock_);
      auto it = timing_.find(image.RtpTimestamp());
      if (it != timing_.end()) {
        LatencyStats::Get().Add("encode", it->second.second, encoded);
        LatencyStats::Get().Add("capture_to_encoded", it->second.first,
                                encoded);
        timing_.erase(it);
      }
    }
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
    {
      base::AutoLock lock(timing_lock_);
      timing_.erase(ts);
    }
    callback_->OnFrameDropped(ts, spatial, end);
  }

 private:
  std::unique_ptr<webrtc::VideoEncoder> encoder_;
  webrtc::EncodedImageCallback* callback_ = nullptr;  // Unregistered by owner.
  base::Lock timing_lock_;
  std::map<uint32_t, std::pair<base::TimeTicks, base::TimeTicks>> timing_;
};
}  // namespace

void VplEncoderState::SetCaptureDevice(ComPtr<ID3D11Device> device) {
  std::lock_guard lock(mutex_);
  capture_device_ = std::move(device);
}
ComPtr<ID3D11Device> VplEncoderState::capture_device() const {
  std::lock_guard lock(mutex_);
  return capture_device_;
}
void VplEncoderState::SetEncoderDevice(ComPtr<ID3D11Device> device) {
  std::lock_guard lock(mutex_);
  encoder_device_ = std::move(device);
}
void VplEncoderState::ClearEncoderDevice(ID3D11Device* device) {
  std::lock_guard lock(mutex_);
  if (encoder_device_.Get() == device)
    encoder_device_.Reset();
}
bool VplEncoderState::NativeActive(ID3D11Device* device) const {
  std::lock_guard lock(mutex_);
  return device && encoder_device_.Get() == device &&
         capture_device_.Get() == device;
}

VplNativeFrameBuffer::VplNativeFrameBuffer(
    ComPtr<ID3D11Texture2D> texture,
    int width,
    int height,
    std::shared_ptr<void> lease,
    ComPtr<ID3D11Query> completion,
    base::TimeTicks submitted,
    std::shared_ptr<std::atomic<bool>> pool_health)
    : texture_(std::move(texture)),
      pool_health_(std::move(pool_health)),
      width_(width),
      height_(height),
      lease_(std::move(lease)),
      completion_(std::move(completion)),
      submitted_(submitted) {}
void VplNativeFrameBuffer::RecordVppCompletion(
    ID3D11DeviceContext* context) const {
  if (!completion_ || submitted_.is_null())
    return;
  BOOL complete = FALSE;
  if (context->GetData(completion_.Get(), &complete, sizeof(complete),
                       D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
      complete) {
    // Upper bound: includes queue handoff and the bitstream synchronization.
    LatencyStats::Get().Add("vpp_complete_observed", submitted_,
                            base::TimeTicks::Now());
  }
}
VplNativeFrameBuffer::~VplNativeFrameBuffer() = default;
webrtc::VideoFrameBuffer::Type VplNativeFrameBuffer::type() const {
  return Type::kNative;
}
int VplNativeFrameBuffer::width() const {
  return width_;
}
int VplNativeFrameBuffer::height() const {
  return height_;
}
std::string VplNativeFrameBuffer::storage_representation() const {
  return kNativeTag;
}
const VplNativeFrameBuffer* VplNativeFrameBuffer::From(
    const webrtc::VideoFrameBuffer& buffer) {
  if (buffer.type() != Type::kNative ||
      buffer.storage_representation() != kNativeTag) {
    return nullptr;
  }
  // The private endpoint source contract reserves this tag for this C++ class.
  auto* native = static_cast<const VplNativeFrameBuffer*>(&buffer);
  return native->lease_ &&
                 ValidNv12(native->texture(), native->width_, native->height_)
             ? native
             : nullptr;
}
webrtc::scoped_refptr<webrtc::I420BufferInterface>
VplNativeFrameBuffer::ReadbackFailed() {
  if (!pool_health_ || pool_health_->exchange(false))
    LOG(WARNING) << "PRTS native ToI420 failed; use capture I420 bridge";
  return nullptr;
}
webrtc::scoped_refptr<webrtc::I420BufferInterface>
VplNativeFrameBuffer::ToI420() {
  std::lock_guard lock(readback_mutex_);
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true))
    LOG(INFO) << "PRTS native ToI420 slow readback";
  if (!lease_ || !ValidNv12(texture_.Get(), width_, height_))
    return ReadbackFailed();
  ComPtr<ID3D11Device> device;
  texture_->GetDevice(&device);
  ComPtr<ID3D11DeviceContext> context;
  device->GetImmediateContext(&context);
  D3D11_TEXTURE2D_DESC desc{};
  texture_->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = desc.MiscFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (!staging_ && FAILED(device->CreateTexture2D(&desc, nullptr, &staging_)))
    return ReadbackFailed();
  context->CopyResource(staging_.Get(), texture_.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
    return ReadbackFailed();
  if (mapped.RowPitch < desc.Width || mapped.RowPitch > INT_MAX) {
    context->Unmap(staging_.Get(), 0);
    return ReadbackFailed();
  }
  auto i420 = webrtc::I420Buffer::Create(width_, height_);
  const auto* y = static_cast<const uint8_t*>(mapped.pData);
  int result = libyuv::NV12ToI420(
      y, mapped.RowPitch,
      y + static_cast<size_t>(mapped.RowPitch) * desc.Height, mapped.RowPitch,
      i420->MutableDataY(), i420->StrideY(), i420->MutableDataU(),
      i420->StrideU(), i420->MutableDataV(), i420->StrideV(), width_, height_);
  context->Unmap(staging_.Get(), 0);
  if (result != 0)
    return ReadbackFailed();
  return i420;
}

webrtc::scoped_refptr<webrtc::VideoFrameBuffer>
VplNativeFrameBuffer::CropAndScale(int x,
                                   int y,
                                   int crop_w,
                                   int crop_h,
                                   int scaled_w,
                                   int scaled_h) {
  if (x == 0 && y == 0 && crop_w == width_ && crop_h == height_ &&
      scaled_w == width_ && scaled_h == height_)
    return webrtc::scoped_refptr<webrtc::VideoFrameBuffer>(this);
  auto i420 = ToI420();
  return i420 ? i420->CropAndScale(x, y, crop_w, crop_h, scaled_w, scaled_h)
              : nullptr;
}
webrtc::scoped_refptr<webrtc::VideoFrameBuffer>
VplNativeFrameBuffer::GetMappedFrameBuffer(std::span<Type> types) {
  if (std::find(types.begin(), types.end(), Type::kI420) == types.end())
    return nullptr;
  return ToI420();
}

class VplEncoder::Impl {
 public:
  explicit Impl(std::shared_ptr<VplEncoderState> state)
      : state_(std::move(state)) {}
  ~Impl() { Close(); }

  bool OpenDevice(ComPtr<ID3D11Device> supplied = nullptr) {
    device_ =
        supplied ? supplied : (state_ ? state_->capture_device() : nullptr);
    // Non-Intel capture uses the I420 bridge to our own Intel encoder device.
    if (device_) {
      ComPtr<IDXGIDevice> dxgi;
      ComPtr<IDXGIAdapter> adapter;
      DXGI_ADAPTER_DESC desc{};
      if (!(device_->GetCreationFlags() & D3D11_CREATE_DEVICE_VIDEO_SUPPORT) ||
          FAILED(device_.As(&dxgi)) || FAILED(dxgi->GetAdapter(&adapter)) ||
          FAILED(adapter->GetDesc(&desc)) || desc.VendorId != 0x8086) {
        device_.Reset();
      }
    }
    if (!device_) {
      ComPtr<IDXGIFactory1> factory;
      if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return false;
      for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> candidate;
        HRESULT hr = factory->EnumAdapters1(index, &candidate);
        if (hr == DXGI_ERROR_NOT_FOUND)
          break;
        if (FAILED(hr))
          return false;
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(candidate->GetDesc1(&desc)))
          return false;
        if (desc.VendorId != 0x8086 ||
            (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
          continue;
        if (FAILED(D3D11CreateDevice(
                candidate.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                D3D11_CREATE_DEVICE_VIDEO_SUPPORT |
                    D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr, 0, D3D11_SDK_VERSION, &device_, nullptr, &context_))) {
          return false;
        }
        break;
      }
    }
    if (!device_ ||
        !(device_->GetCreationFlags() & D3D11_CREATE_DEVICE_VIDEO_SUPPORT)) {
      return false;
    }
    device_->GetImmediateContext(&context_);
    ComPtr<ID3D10Multithread> multithread;
    if (FAILED(context_.As(&multithread)))
      return false;
    multithread->SetMultithreadProtected(TRUE);
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(device_.As(&dxgi)) || FAILED(dxgi->GetAdapter(&adapter)) ||
        FAILED(adapter->GetDesc(&desc)) || desc.VendorId != 0x8086) {
      return false;
    }
    luid_ = desc.AdapterLuid;
    LOG(INFO) << "PRTS VPL adapter=" << base::WideToUTF8(desc.Description)
              << " vendor=" << desc.VendorId << " device=" << desc.DeviceId
              << " LUID=" << luid_.HighPart << ':' << luid_.LowPart;
    return true;
  }

  bool Filter(const char* name, mfxU32 value) {
    mfxConfig config = MFXCreateConfig(loader_);
    if (!config)
      return false;
    mfxVariant variant{};
    variant.Type = MFX_VARIANT_TYPE_U32;
    variant.Data.U32 = value;
    return MFXSetConfigFilterProperty(config,
                                      reinterpret_cast<const mfxU8*>(name),
                                      variant) == MFX_ERR_NONE;
  }

  bool OpenSession() {
    loader_ = MFXLoad();
    if (!loader_ ||
        !Filter("mfxImplDescription.Impl", MFX_IMPL_TYPE_HARDWARE) ||
        !Filter("mfxImplDescription.mfxEncoderDescription.encoder.CodecID",
                MFX_CODEC_AVC) ||
        !Filter("mfxImplDescription.AccelerationMode",
                MFX_ACCEL_MODE_VIA_D3D11)) {
      return false;
    }
    // No API >= 2 filter: UHD 630 is a legacy API 1.35 implementation.
    for (mfxU32 i = 0;; ++i) {
      mfxHDL description = nullptr;
      mfxStatus status = MFXEnumImplementations(
          loader_, i, MFX_IMPLCAPS_IMPLDESCSTRUCTURE, &description);
      if (status == MFX_ERR_NOT_FOUND)
        break;
      if (status != MFX_ERR_NONE || !description)
        return false;
      auto* impl = static_cast<mfxImplDescription*>(description);
      LOG(INFO) << "PRTS VPL candidate=" << impl->ImplName
                << " API=" << impl->ApiVersion.Major << '.'
                << impl->ApiVersion.Minor;
      MFXDispReleaseImplDescription(loader_, description);
      mfxHDL extended = nullptr;
      status = MFXEnumImplementations(
          loader_, i, MFX_IMPLCAPS_DEVICE_ID_EXTENDED, &extended);
      bool matches = false;
      if (status == MFX_ERR_NONE && extended) {
        auto* id = static_cast<mfxExtendedDeviceId*>(extended);
        LUID luid{};
        std::memcpy(&luid, id->DeviceLUID, sizeof(luid));
        matches = id->LUIDValid && luid.HighPart == luid_.HighPart &&
                  luid.LowPart == luid_.LowPart;
        MFXDispReleaseImplDescription(loader_, extended);
      }
      // Fail closed if LUID cannot be verified; never guess VendorImplID as a
      // physical adapter identity on multi-adapter machines.
      if (!matches)
        continue;
      mfxHDL runtime_path = nullptr;
      if (MFXEnumImplementations(loader_, i, MFX_IMPLCAPS_IMPLPATH,
                                 &runtime_path) == MFX_ERR_NONE &&
          runtime_path) {
        LOG(INFO) << "PRTS VPL runtime="
                  << static_cast<const char*>(runtime_path);
        MFXDispReleaseImplDescription(loader_, runtime_path);
      }
      if (MFXCreateSession(loader_, i, &session_) != MFX_ERR_NONE)
        continue;
      mfxVersion version{};
      if (MFXQueryVersion(session_, &version) != MFX_ERR_NONE)
        return false;
      LOG(INFO) << "PRTS VPL session API=" << version.Major << '.'
                << version.Minor;
      allocator_ = std::make_unique<TextureAllocator>(device_.Get());
      return MFXVideoCORE_SetHandle(session_, MFX_HANDLE_D3D11_DEVICE,
                                    device_.Get()) == MFX_ERR_NONE &&
             MFXVideoCORE_SetFrameAllocator(session_, &allocator_->api) ==
                 MFX_ERR_NONE;
    }
    return false;
  }

  bool Start(int width,
             int height,
             int kbps,
             int fps,
             ComPtr<ID3D11Device> supplied = nullptr) {
    Close();
    width_ = width;
    height_ = height;
    kbps_ = kbps;
    fps_ = fps;
    frames_ = 0;
    consecutive_errors_ = 0;
    last_reset_ = base::TimeTicks::Now();
    check_reset_idr_ = false;
    if (!OpenDevice(std::move(supplied)) || !OpenSession())
      return false;
    Parameters requested;
    const auto& options = GetExperimentOptions();
    requested.p.AsyncDepth = 1;
    requested.p.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;
    auto& m = requested.p.mfx;
    m.CodecId = MFX_CODEC_AVC;
    m.CodecProfile = MFX_PROFILE_AVC_CONSTRAINED_BASELINE;
    // Keep the true SPS level despite nominal 42e01f SDP offer.
    m.CodecLevel = AvcLevel(width, height, fps, max_kbps_);
    if (!m.CodecLevel)
      return false;
    m.LowPower = options.low_power ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
    m.TargetUsage = static_cast<mfxU16>(options.target_usage);
    m.RateControlMethod =
        options.cbr ? MFX_RATECONTROL_CBR : MFX_RATECONTROL_VBR;
    m.GopPicSize = 65535;
    m.GopRefDist = m.NumRefFrame = 1;
    m.NumSlice = static_cast<mfxU16>(options.slices);
    m.FrameInfo.FourCC = MFX_FOURCC_NV12;
    m.FrameInfo.Width = static_cast<mfxU16>(Align16(width));
    m.FrameInfo.Height = static_cast<mfxU16>(Align16(height));
    m.FrameInfo.CropW = static_cast<mfxU16>(width);
    m.FrameInfo.CropH = static_cast<mfxU16>(height);
    m.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    m.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    requested.co2.AdaptiveI = requested.co2.AdaptiveB = MFX_CODINGOPTION_OFF;
    requested.co2.MBBRC = MFX_CODINGOPTION_OFF;
    requested.co3.LowDelayBRC =
        options.cbr ? MFX_CODINGOPTION_OFF : MFX_CODINGOPTION_ON;
    if (options.intra_refresh) {
      requested.co2.IntRefType = MFX_REFRESH_VERTICAL;
      requested.co2.IntRefCycleSize =
          static_cast<mfxU16>(options.intra_refresh);
    }
    // Input contract is BT.709 limited; repacking I420 does not change color.
    requested.vui.VideoFormat = 5;
    requested.vui.ColourDescriptionPresent = 1;
    requested.vui.ColourPrimaries = requested.vui.TransferCharacteristics =
        requested.vui.MatrixCoefficients = 1;
    SetRateFields(requested, kbps, max_kbps_, fps);
    Parameters queried(requested);
    mfxStatus status = MFXVideoENCODE_Query(session_, &requested.p, &queried.p);
    LogParameters("Query", status, queried);
    if (status < 0 || !Compatible(requested, queried, true))
      return false;
    parameters_ = queried;
    status = MFXVideoENCODE_Init(session_, &parameters_.p);
    LogParameters("Init", status, parameters_);
    if (status < 0)
      return false;
    initialized_ = true;
    Parameters actual;
    status = MFXVideoENCODE_GetVideoParam(session_, &actual.p);
    LogParameters("GetVideoParam", status, actual);
    if (status != MFX_ERR_NONE || !Compatible(requested, actual, true))
      return false;
    parameters_ = actual;
    kbps_ =
        static_cast<int>(EffectiveBrc(actual.p.mfx, actual.p.mfx.TargetKbps));
    last_rate_request_kbps_ = kbps;
    mfxFrameAllocRequest request{};
    if (MFXVideoENCODE_QueryIOSurf(session_, &parameters_.p, &request) < 0)
      return false;
    // Extra slots avoid premature reuse of a reference-retained input surface.
    request.NumFrameSuggested = std::max<mfxU16>(request.NumFrameSuggested, 4);
    if (request.NumFrameSuggested > 32)
      return false;
    // One in import, one pending, one in WebRTC in addition to MFX retention.
    if (state_)
      state_->native_pool_limit.store(request.NumFrameSuggested + 3);
    request.Type =
        static_cast<mfxU16>((request.Type & ~MFX_MEMTYPE_INTERNAL_FRAME) |
                            MFX_MEMTYPE_EXTERNAL_FRAME);
    if (TextureAllocator::Allocate(allocator_.get(), &request, &inputs_) !=
        MFX_ERR_NONE) {
      return false;
    }
    for (mfxU16 i = 0; i < inputs_.NumFrameActual; ++i) {
      auto slot = std::make_unique<Slot>();
      slot->surface.Info = parameters_.p.mfx.FrameInfo;
      slot->surface.Data.MemId = inputs_.mids[i];
      slot->upload_mid = inputs_.mids[i];
      slots_.push_back(std::move(slot));
    }
    packed_.resize(static_cast<size_t>(Align16(width)) * Align16(height) * 3 /
                   2);
    bitstream_storage_.resize(std::clamp<size_t>(
        EffectiveBrc(actual.p.mfx, actual.p.mfx.BufferSizeInKB) * 1000,
        1024 * 1024, kMaxBitstreamBytes));
    force_idr_ = true;
    failed_ = false;
    if (state_)
      state_->SetEncoderDevice(device_);
    LOG(INFO) << "PRTS encoder active=vpl " << width << 'x' << height
              << " fps=" << fps << " SPS_level=" << m.CodecLevel;
    return true;
  }

  void Close() {
    if (state_)
      state_->ClearEncoderDevice(device_.Get());
    // MFX must release its surface references before pool leases and MemIds
    // die.
    if (initialized_)
      MFXVideoENCODE_Close(session_);
    initialized_ = false;
    if (session_)
      MFXClose(session_);
    session_ = nullptr;
    pending_sync_ = nullptr;
    slots_.clear();
    if (inputs_.mids)
      TextureAllocator::Free(allocator_.get(), &inputs_);
    allocator_.reset();
    if (loader_)
      MFXUnload(loader_);
    loader_ = nullptr;
    context_.Reset();
    device_.Reset();
  }

  bool TryReset(int kbps, bool with_option) {
    Parameters wanted(parameters_);
    // These are the only fields allowed to change during seamless reset.
    const int multiplier = std::max<mfxU16>(1, wanted.p.mfx.BRCParamMultiplier);
    wanted.p.mfx.TargetKbps =
        static_cast<mfxU16>((kbps + multiplier - 1) / multiplier);
    const int ceiling = GetExperimentOptions().cbr ? kbps : max_kbps_;
    wanted.p.mfx.MaxKbps =
        static_cast<mfxU16>((ceiling + multiplier - 1) / multiplier);
    mfxExtEncoderResetOption reset{};
    reset.Header = {MFX_EXTBUFF_ENCODER_RESET_OPTION, sizeof(reset)};
    reset.StartNewSequence = MFX_CODINGOPTION_OFF;
    mfxExtBuffer* extensions[] = {wanted.extensions[0], wanted.extensions[1],
                                  wanted.extensions[2],
                                  reinterpret_cast<mfxExtBuffer*>(&reset)};
    if (with_option) {
      wanted.p.ExtParam = extensions;
      wanted.p.NumExtParam = 4;
    }
    const Parameters expected(wanted);
    ++reset_attempts_;
    const mfxStatus status = MFXVideoENCODE_Reset(session_, &wanted.p);
    LogParameters(with_option ? "Reset-no-IDR" : "Reset-plain", status, wanted);
    Parameters actual;
    const mfxStatus readback =
        MFXVideoENCODE_GetVideoParam(session_, &actual.p);
    LogParameters("Reset-readback", readback, actual);
    if (status < 0)
      return false;
    ++resets_;
    if (readback != MFX_ERR_NONE || !Compatible(expected, actual)) {
      // A successful but incompatible reset cannot safely keep streaming.
      Fail("reset readback mismatch", readback);
      return false;
    }
    parameters_ = actual;
    kbps_ =
        static_cast<int>(EffectiveBrc(actual.p.mfx, actual.p.mfx.TargetKbps));
    last_rate_request_kbps_ = kbps;
    check_reset_idr_ = true;
    return true;
  }

  void ResetRate(int kbps) {
    pending_kbps_ = kbps;
    if (pending_sync_)
      return;
    if (hold_max_)
      kbps = max_kbps_;
    const auto now = base::TimeTicks::Now();
    // Keep the latest request until cooldown expires, including small changes.
    // Large decreases can immediately relieve congestion.
    const bool large_decrease = kbps * 10 <= kbps_ * 7;
    if (kbps == last_rate_request_kbps_ ||
        (!large_decrease && now - last_reset_ < base::Seconds(2))) {
      ++rate_ignored_;
      return;
    }
    last_reset_ = now;
    if (!TryReset(kbps, true) && !failed_ && !TryReset(kbps, false)) {
      // Keep the initialized maximum-rate VBR encoder if the runtime rejects
      // seamless changes. Never rebuild merely because GCC changed its rate.
      hold_max_ = true;
      LOG(WARNING) << "PRTS VPL rate reset rejected; retain rate kbps="
                   << kbps_;
    }
  }

  struct Slot {
    mfxFrameSurface1 surface{};
    mfxMemId upload_mid = nullptr;
    TextureMemory native_memory;
    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> lease;
  };

  Slot* Prepare(const webrtc::VideoFrame& frame) {
    Slot* available = nullptr;
    for (auto& slot : slots_) {
      if (!slot->surface.Data.Locked) {
        slot->lease = nullptr;
        slot->native_memory.texture.Reset();
        if (!available)
          available = slot.get();
      }
    }
    if (!available)
      return nullptr;
    auto buffer = frame.video_frame_buffer();
    const auto* native = VplNativeFrameBuffer::From(*buffer);
    ComPtr<ID3D11Device> owner;
    if (native)
      native->texture()->GetDevice(&owner);
    if (native && owner.Get() == device_.Get()) {
      if (!ValidNv12(native->texture(), width_, height_)) {
        return nullptr;
      }
      available->native_memory.texture = native->texture();
      available->surface.Data.MemId = &available->native_memory;
      available->lease = buffer;
    } else {
      auto i420 = buffer->ToI420();
      if (!i420 || i420->width() != width_ || i420->height() != height_)
        return nullptr;
      int pitch = Align16(width_);
      int padded_height = Align16(height_);
      std::fill(packed_.begin(), packed_.begin() + pitch * padded_height,
                uint8_t{16});
      std::fill(packed_.begin() + pitch * padded_height, packed_.end(),
                uint8_t{128});
      if (libyuv::I420ToNV12(i420->DataY(), i420->StrideY(), i420->DataU(),
                             i420->StrideU(), i420->DataV(), i420->StrideV(),
                             packed_.data(), pitch,
                             packed_.data() + pitch * padded_height, pitch,
                             width_, height_) != 0) {
        return nullptr;
      }
      auto* memory = static_cast<TextureMemory*>(available->upload_mid);
      context_->UpdateSubresource(memory->texture.Get(), 0, nullptr,
                                  packed_.data(), pitch, 0);
      available->surface.Data.MemId = available->upload_mid;
    }
    available->surface.Data.TimeStamp = next_timestamp_;
    next_timestamp_ +=
        90000 / fps_;  // Internal monotonic association, not RTP wrap.
    available->surface.Data.FrameOrder = next_order_++;
    return available;
  }

  int Encode(const webrtc::VideoFrame& frame,
             const std::vector<webrtc::VideoFrameType>* types) {
    if (!initialized_ || failed_)
      return WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE;
    if (!callback_)
      return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    if (types &&
        std::find(types->begin(), types->end(),
                  webrtc::VideoFrameType::kVideoFrameKey) != types->end()) {
      force_idr_ = true;
    }
    if (paused_ || (types && !types->empty() &&
                    types->front() == webrtc::VideoFrameType::kEmptyFrame)) {
      callback_->OnFrameDropped(frame.rtp_timestamp(), 0, true);
      return WEBRTC_VIDEO_CODEC_OK;
    }
    if (!ValidSize(frame.width(), frame.height()) ||
        frame.rotation() != webrtc::kVideoRotation_0) {
      return Fail("unsupported frame geometry");
    }
    // A timed-out operation still owns its bitstream and input surface. Drain
    // it before reset, upload, or buffer reuse; its picture was already dropped.
    if (pending_sync_) {
      const auto status =
          MFXVideoCORE_SyncOperation(session_, pending_sync_, 100);
      if (status != MFX_ERR_NONE)
        return Drop(frame, "pending SyncOperation", status, true);
      pending_sync_ = nullptr;
    }
    auto capture_device = state_ ? state_->capture_device() : nullptr;
    ComPtr<ID3D11Device> native_device;
    if (const auto* native =
            VplNativeFrameBuffer::From(*frame.video_frame_buffer())) {
      native->texture()->GetDevice(&native_device);
    }
    const bool adopt_native = native_device && capture_device &&
                             native_device.Get() == capture_device.Get() &&
                             native_device.Get() != device_.Get();
    if (frame.width() != width_ || frame.height() != height_ || adopt_native) {
      // WebRTC normally calls InitEncode on resize. Also handle a direct
      // resize/device generation transition, and force new SPS/PPS + IDR.
      if (!Start(frame.width(), frame.height(), kbps_, fps_,
                 adopt_native ? native_device : device_))
        return Fail("resize/device reinit");
    }
    if (pending_kbps_ > 0) {
      ResetRate(pending_kbps_);
      if (failed_)
        return WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE;
    }
    Slot* slot = Prepare(frame);
    if (!slot)
      return Drop(frame, "input surface");
    mfxEncodeCtrl control{};
    if (force_idr_)
      control.FrameType =
          MFX_FRAMETYPE_I | MFX_FRAMETYPE_IDR | MFX_FRAMETYPE_REF;
    bits_ = {};
    auto& bits = bits_;
    mfxSyncPoint sync = nullptr;
    auto started = SteadyClock::now();
    int64_t encode_start_ms = webrtc::TimeMillis();
    mfxStatus status;
    for (;;) {
      bits.Data = bitstream_storage_.data();
      bits.MaxLength = static_cast<mfxU32>(bitstream_storage_.size());
      status = MFXVideoENCODE_EncodeFrameAsync(session_, &control,
                                               &slot->surface, &bits, &sync);
      if (status == MFX_ERR_NOT_ENOUGH_BUFFER && !sync &&
          bitstream_storage_.size() < kMaxBitstreamBytes) {
        bitstream_storage_.resize(
            std::min(kMaxBitstreamBytes, bitstream_storage_.size() * 2));
        continue;
      }
      if (status != MFX_WRN_DEVICE_BUSY)
        break;
      if (SteadyClock::now() - started > kBusyTimeout)
        return Drop(frame, "device busy timeout", status);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // No B frames and depth one are mandatory. Reject buffering/skips instead
    // of associating a delayed AU with the wrong capture metadata.
    if (status < 0 || !sync)
      return Drop(frame, "EncodeFrameAsync", status, true);
    // Never hold ID3D10Multithread::Enter or an application mutex here.
    pending_sync_ = sync;
    status = MFXVideoCORE_SyncOperation(session_, sync, 100);
    if (status != MFX_ERR_NONE)
      return Drop(frame, "SyncOperation", status, true);
    pending_sync_ = nullptr;
    if (slot->lease) {
      VplNativeFrameBuffer::From(*slot->lease)
          ->RecordVppCompletion(context_.Get());
    }
    if (!bits.DataLength)
      return Drop(frame, "empty output", MFX_ERR_MORE_DATA, true);
    if (bits.TimeStamp != slot->surface.Data.TimeStamp ||
        bits.DataOffset > bits.MaxLength ||
        bits.DataLength > bits.MaxLength - bits.DataOffset ||
        (bits.FrameType & MFX_FRAMETYPE_B)) {
      return Drop(frame, "unexpected buffered/invalid output",
                  MFX_ERR_UNDEFINED_BEHAVIOR, true);
    }
    auto bytes =
        std::span<const uint8_t>(bits.Data + bits.DataOffset, bits.DataLength);
    bool idr = false;
    bool sps = false, pps = false;
    // Verify IDR and emitted SPS profile/level from actual Annex B NALs.
    for (size_t i = 0; i + 3 < bytes.size(); ++i) {
      if (bytes[i] != 0 || bytes[i + 1] != 0 || bytes[i + 2] != 1)
        continue;
      const int nal_type = bytes[i + 3] & 31;
      idr |= nal_type == 5;
      pps |= nal_type == 8;
      if (nal_type == 7) {
        if (i + 6 >= bytes.size() || bytes[i + 4] != 66 ||
            !(bytes[i + 5] & 0x40) ||
            bytes[i + 6] != parameters_.p.mfx.CodecLevel)
          return Fail("SPS profile/level mismatch");
        sps = true;
        LOG(INFO) << "PRTS VPL SPS profile=66 constraints="
                  << static_cast<int>(bytes[i + 5])
                  << " level=" << static_cast<int>(bytes[i + 6]);
      }
    }
    if (idr && (!sps || !pps))
      return Fail("IDR without SPS/PPS");
    ++frames_;
    consecutive_errors_ = 0;
    encoded_bytes_ += bits.DataLength;
    if (idr)
      ++idrs_;
    if (check_reset_idr_) {
      if (idr && !force_idr_) {
        hold_max_ = true;
        LOG(WARNING)
            << "PRTS VPL runtime inserted IDR after reset; hold max VBR";
      }
      check_reset_idr_ = false;
    }
    const auto stats_now = base::TimeTicks::Now();
    if (stats_now - stats_start_ >= base::Seconds(5)) {
      LOG(INFO) << "PRTS VPL stats5s frames=" << frames_
                << " resets=" << resets_ << " attempts=" << reset_attempts_
                << " IDRs=" << idrs_ << " ignored=" << rate_ignored_
                << " send_kbps="
                << encoded_bytes_ * 8 /
                       (stats_now - stats_start_).InMillisecondsF()
                << " target_kbps=" << kbps_ << " max_kbps=" << max_kbps_
                << " hold_max=" << hold_max_;
      resets_ = reset_attempts_ = idrs_ = rate_ignored_ = encoded_bytes_ = 0;
      stats_start_ = stats_now;
    }
    if (idr) {
      LOG(INFO) << "PRTS VPL IDR " << width_ << "x" << height_
                << " bytes=" << bits.DataLength << " SPS=" << sps
                << " PPS=" << pps;
      force_idr_ = false;
    }
    webrtc::EncodedImage output;
    output.SetEncodedData(
        webrtc::EncodedImageBuffer::Create(bytes.data(), bytes.size()));
    output._encodedWidth = width_;
    output._encodedHeight = height_;
    output.SetRtpTimestamp(frame.rtp_timestamp());
    output.capture_time_ms_ = frame.timestamp_us() / 1000;
    output.ntp_time_ms_ = frame.ntp_time_ms();
    output.rotation_ = frame.rotation();
    output.SetColorSpace(frame.color_space());
    output.SetSimulcastIndex(0);
    output.set_end_of_temporal_unit(true);
    output.set_frame_type(idr ? webrtc::VideoFrameType::kVideoFrameKey
                              : webrtc::VideoFrameType::kVideoFrameDelta);
    output.content_type_ = webrtc::VideoContentType::SCREENSHARE;
    SetPlayoutDelay(output);
    // Slice initial QP is misleading on VDEnc (MB deltas carry actual QP).
    // H264BitstreamParser also lacks RTC_EXPORT for component consumers.
    output.qp_ = -1;
    output.SetEncodeTime(encode_start_ms, webrtc::TimeMillis());
    webrtc::CodecSpecificInfo info;
    info.codecType = webrtc::kVideoCodecH264;
    info.codecSpecific.H264.packetization_mode =
        webrtc::H264PacketizationMode::NonInterleaved;
    info.codecSpecific.H264.temporal_idx = webrtc::kNoTemporalIdx;
    info.codecSpecific.H264.base_layer_sync = false;
    info.codecSpecific.H264.idr_frame = idr;
    if (parameters_.co2.MaxFrameSize &&
        bits.DataLength > parameters_.co2.MaxFrameSize)
      ++cap_violations_;
    RTC_LOG(LS_VERBOSE) << "PRTS VPL AU bytes=" << bits.DataLength
                        << " cap=" << parameters_.co2.MaxFrameSize
                        << " violations=" << cap_violations_ << " idr=" << idr
                        << " input="
                        << (slot->lease ? "native-nv12" : "i420-upload");
    // Keep every valid encoded reference picture, including cap violations.
    auto result = callback_->OnEncodedImage(output, &info);
    return result.error == webrtc::EncodedImageCallback::Result::OK
               ? WEBRTC_VIDEO_CODEC_OK
               : WEBRTC_VIDEO_CODEC_ERROR;
  }

  int Drop(const webrtc::VideoFrame& frame,
           const char* operation,
           mfxStatus status = MFX_ERR_UNKNOWN,
           bool reference_lost = false) {
    if (status == MFX_ERR_DEVICE_LOST || status == MFX_ERR_DEVICE_FAILED ||
        status == MFX_ERR_GPU_HANG || status == MFX_ERR_NOT_INITIALIZED ||
        ++consecutive_errors_ >= 8) {
      return Fail(operation, status);
    }
    force_idr_ |= reference_lost;
    if (consecutive_errors_ == 1)
      LOG(WARNING) << "PRTS VPL transient " << operation << " status=" << status
                   << "; drop raw frame";
    callback_->OnFrameDropped(frame.rtp_timestamp(), 0, true);
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int Fail(const char* operation, mfxStatus status = MFX_ERR_UNKNOWN) {
    RTC_LOG(LS_ERROR) << "PRTS VPL " << operation << " status=" << status
                      << "; fallback encoder=openh264";
    failed_ = true;
    Close();  // Release native leases only after driver releases references.
    return WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE;
  }

  std::shared_ptr<VplEncoderState> state_;
  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  LUID luid_{};
  mfxLoader loader_ = nullptr;
  mfxSession session_ = nullptr;
  std::unique_ptr<TextureAllocator> allocator_;
  mfxFrameAllocResponse inputs_{};
  std::vector<std::unique_ptr<Slot>> slots_;
  Parameters parameters_;
  std::vector<uint8_t> packed_;
  std::vector<uint8_t> bitstream_storage_;
  webrtc::EncodedImageCallback* callback_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  int kbps_ = 0;
  int last_rate_request_kbps_ = 0;
  int pending_kbps_ = 0;
  int consecutive_errors_ = 0;
  int fps_ = 0;
  int max_kbps_ = 12000;
  int min_kbps_ = 2000;
  uint64_t frames_ = 0, resets_ = 0, reset_attempts_ = 0, idrs_ = 0;
  uint64_t rate_ignored_ = 0, encoded_bytes_ = 0;
  bool hold_max_ = false, check_reset_idr_ = false;
  base::TimeTicks last_reset_;
  base::TimeTicks stats_start_ = base::TimeTicks::Now();
  bool initialized_ = false;
  bool force_idr_ = true;
  bool failed_ = false;
  bool paused_ = false;
  mfxU64 next_timestamp_ = 0;
  mfxU32 next_order_ = 0;
  uint64_t cap_violations_ = 0;
  mfxBitstream bits_{};
  mfxSyncPoint pending_sync_ = nullptr;
};

VplEncoder::VplEncoder(std::shared_ptr<VplEncoderState> state)
    : impl_(std::make_unique<Impl>(std::move(state))) {}
VplEncoder::~VplEncoder() = default;
bool VplEncoder::IsAvailable() {
  Impl probe(nullptr);
  return probe.OpenDevice() && probe.OpenSession();
}
int VplEncoder::InitEncode(const webrtc::VideoCodec* codec, const Settings&) {
  impl_->Close();
  if (!codec || codec->codecType != webrtc::kVideoCodecH264 ||
      !ValidSize(codec->width, codec->height) || !codec->maxFramerate ||
      codec->maxFramerate > 60 || codec->numberOfSimulcastStreams > 1 ||
      codec->H264().numberOfTemporalLayers > 1) {
    RTC_LOG(LS_WARNING)
        << "PRTS VPL unsupported configuration; fallback encoder=openh264";
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  impl_->max_kbps_ = std::clamp(
      codec->maxBitrate ? static_cast<int>(codec->maxBitrate)
                        : impl_->state_->max_bitrate_bps.load() / 1000,
      1, 50000);
  impl_->min_kbps_ =
      std::min(static_cast<int>(codec->minBitrate), impl_->max_kbps_);
  // Legacy runtimes size internal BRC resources from the initial target and
  // reject later increases above it. Keep the negotiated ceiling at Init;
  // retain GCC's startup target for a deferred seamless decrease.
  int kbps = impl_->max_kbps_;
  impl_->pending_kbps_ = std::clamp(static_cast<int>(codec->startBitrate),
                                  std::max(1, impl_->min_kbps_), kbps);
  impl_->hold_max_ = impl_->check_reset_idr_ = false;
  impl_->paused_ = codec->startBitrate == 0;
  if (!impl_->Start(codec->width, codec->height, kbps, codec->maxFramerate)) {
    return impl_->Fail("InitEncode");
  }
  return WEBRTC_VIDEO_CODEC_OK;
}
int32_t VplEncoder::RegisterEncodeCompleteCallback(
    webrtc::EncodedImageCallback* callback) {
  impl_->callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}
int32_t VplEncoder::Release() {
  impl_->Close();
  return WEBRTC_VIDEO_CODEC_OK;
}
int32_t VplEncoder::Encode(const webrtc::VideoFrame& frame,
                           const std::vector<webrtc::VideoFrameType>* types) {
  return impl_->Encode(frame, types);
}
void VplEncoder::SetRates(const RateControlParameters& parameters) {
  if (!impl_->initialized_ || impl_->failed_)
    return;
  uint32_t bps = parameters.bitrate.get_sum_bps();
  impl_->paused_ = bps == 0;
  if (impl_->paused_)
    return;  // Zero-rate is pause, never reset an encoder to TargetKbps=0.
  // Keep the initialized frame-rate and buffer contract. GCC's rate updates
  // must not change FrameRateExtN, MaxFrameSize, or SPS level.
  int kbps = static_cast<int>(
      std::clamp((bps + 999ull) / 1000,
                 static_cast<unsigned long long>(std::max(1, impl_->min_kbps_)),
                 static_cast<unsigned long long>(impl_->max_kbps_)));
  impl_->ResetRate(kbps);
}
webrtc::VideoEncoder::EncoderInfo VplEncoder::GetEncoderInfo() const {
  EncoderInfo info;
  info.implementation_name = "PRTS oneVPL D3D11 AVC";
  info.is_hardware_accelerated = true;
  info.supports_native_handle = true;
  info.supports_simulcast = false;
  info.has_trusted_rate_controller = false;  // T1 demonstrates real overshoot.
  // One ceiling entry avoids WebRTC interpolation replacing the minimum
  // with 30 kbps. Untrusted QP still disables QP-based scaling; use the same
  // negotiated bitrate limits as the software path, at every supported size.
  if (impl_->state_) {
    const int min_bps = impl_->state_->min_bitrate_bps.load();
    const int max_bps = impl_->state_->max_bitrate_bps.load();
    info.resolution_bitrate_limits.emplace_back(1920 * 1080, min_bps, min_bps,
                                                max_bps);
  }
  info.is_qp_trusted = false;
  info.scaling_settings = ScalingSettings::kOff;
  info.requested_resolution_alignment = 2;
  info.max_pixels_per_frame = 1920 * 1080;
  info.preferred_pixel_formats = {webrtc::VideoFrameBuffer::Type::kI420,
                                  webrtc::VideoFrameBuffer::Type::kNative};
  return info;
}

VplEncoderFactory::VplEncoderFactory(std::shared_ptr<VplEncoderState> state,
                                     bool prefer_vpl)
    : state_(std::move(state)),
      use_vpl_(prefer_vpl && VplEncoder::IsAvailable()) {
  LOG(INFO) << "PRTS encoder selected=" << (use_vpl_ ? "vpl" : "openh264");
  if (prefer_vpl && !use_vpl_)
    RTC_LOG(LS_WARNING) << "PRTS VPL unavailable; fallback encoder=openh264";
}
VplEncoderFactory::~VplEncoderFactory() = default;
std::vector<webrtc::SdpVideoFormat> VplEncoderFactory::GetSupportedFormats()
    const {
  webrtc::InternalEncoderFactory builtin;
  for (const auto& format : builtin.GetSupportedFormats()) {
    if (format.name == "H264" && format.parameters == H264Format().parameters)
      return {
          H264Format()};  // Require fallback availability just like peer.cc.
  }
  return {};
}
std::unique_ptr<webrtc::VideoEncoder> VplEncoderFactory::Create(
    const webrtc::Environment& env,
    const webrtc::SdpVideoFormat& format) {
  if (format.name != "H264" || format.parameters != H264Format().parameters)
    return nullptr;
  webrtc::InternalEncoderFactory builtin;
  auto software = builtin.Create(env, format);
  if (!software)
    return nullptr;
  if (!use_vpl_)
    return std::make_unique<LowDelayOutput>(std::move(software));
  auto encoder = webrtc::CreateVideoEncoderSoftwareFallbackWrapper(
      env, std::move(software), std::make_unique<VplEncoder>(state_), false);
  return std::make_unique<LowDelayOutput>(std::move(encoder));
}
}  // namespace re
