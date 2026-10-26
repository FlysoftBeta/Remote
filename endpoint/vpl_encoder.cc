// Copyright 2026 The remotekit Authors
// Use of this source code is governed by a BSD-style license.

#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#pragma allow_unsafe_libc_calls
#endif

#include "prts_endpoint/vpl_encoder.h"

#include <d3d10.h>
#include <dxgi1_2.h>
#include <vpl/mfxdispatcher.h>
#include <vpl/mfxvideo.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <thread>
#include <utility>

#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/libyuv/include/libyuv/convert_from.h"
#include "third_party/webrtc/api/video/encoded_image.h"
#include "third_party/webrtc/api/video/i420_buffer.h"
#include "third_party/webrtc/api/video/video_frame.h"
#include "third_party/webrtc/api/video_codecs/video_codec.h"
#include "third_party/webrtc/api/video_codecs/video_encoder_software_fallback_wrapper.h"
#include "third_party/webrtc/media/engine/internal_encoder_factory.h"
#include "third_party/webrtc/modules/video_coding/include/video_codec_interface.h"
#include "third_party/webrtc/modules/video_coding/include/video_error_codes.h"
#include "third_party/webrtc/rtc_base/logging.h"
#include "third_party/webrtc/rtc_base/time_utils.h"

namespace prts {
namespace {
using Microsoft::WRL::ComPtr;
using SteadyClock = std::chrono::steady_clock;
constexpr char kNativeTag[] = "prts-owned-nv12-v1";
constexpr size_t kMaxBitstreamBytes = 16 * 1024 * 1024;
constexpr auto kBusyTimeout = std::chrono::milliseconds(100);

bool ValidSize(int width, int height) {
  return width > 0 && height > 0 && width <= 4096 && height <= 4096 &&
         !(width & 1) && !(height & 1) &&
         static_cast<int64_t>(width) * height <= 1920 * 1080;
}

// Annex A limits for constrained baseline, <=20 Mbps. Never understate the
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

void SetRateFields(Parameters& params, int kbps, int fps) {
  auto& m = params.p.mfx;
  m.TargetKbps = m.MaxKbps = static_cast<mfxU16>(kbps);
  m.FrameInfo.FrameRateExtN = fps;
  m.FrameInfo.FrameRateExtD = 1;
  // Request one nominal frame of VBV; Query normally raises to driver minimum.
  m.BufferSizeInKB = static_cast<mfxU16>((kbps + 8 * fps - 1) / (8 * fps));
  m.InitialDelayInKB = m.BufferSizeInKB / 2;
  params.co2.MaxFrameSize = static_cast<mfxU32>(kbps * 1000 / (8 * fps));
}

void LogParameters(const char* phase, mfxStatus status, const Parameters& p) {
  RTC_LOG(LS_INFO) << "PRTS VPL " << phase << " status=" << status
                   << " kbps=" << p.p.mfx.TargetKbps << '/' << p.p.mfx.MaxKbps
                   << " fps=" << p.p.mfx.FrameInfo.FrameRateExtN
                   << " VBV_KB=" << p.p.mfx.BufferSizeInKB
                   << " initial_KB=" << p.p.mfx.InitialDelayInKB
                   << " cap_bytes=" << p.co2.MaxFrameSize
                   << " LDB=" << p.co3.LowDelayBRC
                   << " LP=" << p.p.mfx.LowPower;
}

bool Compatible(const Parameters& requested, const Parameters& actual) {
  const auto& a = actual.p.mfx;
  const auto& r = requested.p.mfx;
  return actual.p.AsyncDepth == 1 &&
         actual.p.IOPattern == MFX_IOPATTERN_IN_VIDEO_MEMORY &&
         a.CodecId == MFX_CODEC_AVC && a.CodecProfile == r.CodecProfile &&
         a.CodecLevel == r.CodecLevel && a.LowPower == MFX_CODINGOPTION_ON &&
         a.TargetUsage == 7 && a.GopPicSize == 65535 && a.IdrInterval == 0 &&
         a.RateControlMethod == MFX_RATECONTROL_VBR &&
         a.TargetKbps == r.TargetKbps && a.MaxKbps == r.MaxKbps &&
         a.GopRefDist == 1 && a.NumRefFrame == 1 && a.NumSlice == 1 &&
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
         actual.co3.LowDelayBRC == MFX_CODINGOPTION_ON &&
         actual.co2.AdaptiveI == MFX_CODINGOPTION_OFF &&
         actual.co2.AdaptiveB == MFX_CODINGOPTION_OFF &&
         actual.co2.MBBRC == MFX_CODINGOPTION_OFF &&
         actual.co2.MaxFrameSize == requested.co2.MaxFrameSize &&
         actual.vui.ColourDescriptionPresent == 1 &&
         actual.vui.ColourPrimaries == 1 &&
         actual.vui.TransferCharacteristics == 1 &&
         actual.vui.MatrixCoefficients == 1 && actual.vui.VideoFullRange == 0;
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
    return encoder_->InitEncode(&copy, limited);
  }
  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override {
    callback_ = callback;
    return encoder_->RegisterEncodeCompleteCallback(callback ? this : nullptr);
  }
  int32_t Release() override { return encoder_->Release(); }
  int32_t Encode(const webrtc::VideoFrame& frame,
                 const std::vector<webrtc::VideoFrameType>* types) override {
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
    auto output = image;
    output.SetPlayoutDelay(webrtc::VideoPlayoutDelay::Minimal());
    output.content_type_ = webrtc::VideoContentType::SCREENSHARE;
    return callback_->OnEncodedImage(output, info);
  }
  void OnFrameDropped(uint32_t ts, int spatial, bool end) override {
    callback_->OnFrameDropped(ts, spatial, end);
  }

 private:
  std::unique_ptr<webrtc::VideoEncoder> encoder_;
  webrtc::EncodedImageCallback* callback_ = nullptr;  // Unregistered by owner.
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
bool VplEncoderState::NativeActive(ID3D11Device* device) const {
  std::lock_guard lock(mutex_);
  return device && encoder_device_.Get() == device &&
         capture_device_.Get() == device;
}

VplNativeFrameBuffer::VplNativeFrameBuffer(ComPtr<ID3D11Texture2D> texture,
                                           int width,
                                           int height,
                                           std::shared_ptr<void> lease)
    : texture_(std::move(texture)),
      width_(width),
      height_(height),
      lease_(std::move(lease)) {}
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
VplNativeFrameBuffer::ToI420() {
  RTC_LOG(LS_INFO) << "PRTS native ToI420 slow readback";
  if (!lease_ || !ValidNv12(texture_.Get(), width_, height_))
    return nullptr;
  ComPtr<ID3D11Device> device;
  texture_->GetDevice(&device);
  ComPtr<ID3D11DeviceContext> context;
  device->GetImmediateContext(&context);
  D3D11_TEXTURE2D_DESC desc{};
  texture_->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = desc.MiscFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging)))
    return nullptr;
  context->CopyResource(staging.Get(), texture_.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
    return nullptr;
  if (mapped.RowPitch < desc.Width || mapped.RowPitch > INT_MAX) {
    context->Unmap(staging.Get(), 0);
    return nullptr;
  }
  auto i420 = webrtc::I420Buffer::Create(width_, height_);
  const auto* y = static_cast<const uint8_t*>(mapped.pData);
  int result = libyuv::NV12ToI420(
      y, mapped.RowPitch,
      y + static_cast<size_t>(mapped.RowPitch) * desc.Height, mapped.RowPitch,
      i420->MutableDataY(), i420->StrideY(), i420->MutableDataU(),
      i420->StrideU(), i420->MutableDataV(), i420->StrideV(), width_, height_);
  context->Unmap(staging.Get(), 0);
  if (result != 0)
    return nullptr;
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
    RTC_LOG(LS_INFO) << "PRTS VPL device LUID=" << luid_.HighPart << ':'
                     << luid_.LowPart;
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
      RTC_LOG(LS_INFO) << "PRTS VPL candidate=" << impl->ImplName
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
        RTC_LOG(LS_INFO) << "PRTS VPL runtime="
                         << static_cast<const char*>(runtime_path);
        MFXDispReleaseImplDescription(loader_, runtime_path);
      }
      if (MFXCreateSession(loader_, i, &session_) != MFX_ERR_NONE)
        continue;
      mfxVersion version{};
      if (MFXQueryVersion(session_, &version) != MFX_ERR_NONE)
        return false;
      RTC_LOG(LS_INFO) << "PRTS VPL session API=" << version.Major << '.'
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
    if (!OpenDevice(std::move(supplied)) || !OpenSession())
      return false;
    Parameters requested;
    requested.p.AsyncDepth = 1;
    requested.p.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;
    auto& m = requested.p.mfx;
    m.CodecId = MFX_CODEC_AVC;
    m.CodecProfile = MFX_PROFILE_AVC_CONSTRAINED_BASELINE;
    // Keep the true SPS level despite nominal 42e01f SDP offer.
    m.CodecLevel = AvcLevel(width, height, fps, kbps);
    if (!m.CodecLevel)
      return false;
    m.LowPower = MFX_CODINGOPTION_ON;
    m.TargetUsage = 7;
    m.RateControlMethod = MFX_RATECONTROL_VBR;
    m.GopPicSize = 65535;
    m.GopRefDist = m.NumRefFrame = m.NumSlice = 1;
    m.FrameInfo.FourCC = MFX_FOURCC_NV12;
    m.FrameInfo.Width = static_cast<mfxU16>(Align16(width));
    m.FrameInfo.Height = static_cast<mfxU16>(Align16(height));
    m.FrameInfo.CropW = static_cast<mfxU16>(width);
    m.FrameInfo.CropH = static_cast<mfxU16>(height);
    m.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    m.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    requested.co2.AdaptiveI = requested.co2.AdaptiveB = MFX_CODINGOPTION_OFF;
    requested.co2.MBBRC = MFX_CODINGOPTION_OFF;
    requested.co3.LowDelayBRC = MFX_CODINGOPTION_ON;
    // Input contract is BT.709 limited; repacking I420 does not change color.
    requested.vui.VideoFormat = 5;
    requested.vui.ColourDescriptionPresent = 1;
    requested.vui.ColourPrimaries = requested.vui.TransferCharacteristics =
        requested.vui.MatrixCoefficients = 1;
    SetRateFields(requested, kbps, fps);
    Parameters queried(requested);
    mfxStatus status = MFXVideoENCODE_Query(session_, &requested.p, &queried.p);
    LogParameters("Query", status, queried);
    if (status < 0 || !Compatible(requested, queried))
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
    if (status != MFX_ERR_NONE || !Compatible(requested, actual))
      return false;
    parameters_ = actual;
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
    bitstream_storage_.resize(std::max<size_t>(
        1024 * 1024, static_cast<size_t>(actual.p.mfx.BufferSizeInKB) * 1000));
    force_idr_ = true;
    failed_ = false;
    if (state_)
      state_->SetEncoderDevice(device_);
    RTC_LOG(LS_INFO) << "PRTS encoder active=vpl " << width << 'x' << height
                     << " fps=" << fps << " SPS_level=" << m.CodecLevel;
    return true;
  }

  void Close() {
    if (state_)
      state_->SetEncoderDevice(nullptr);
    // MFX must release its surface references before pool leases and MemIds
    // die.
    if (initialized_)
      MFXVideoENCODE_Close(session_);
    initialized_ = false;
    if (session_)
      MFXClose(session_);
    session_ = nullptr;
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

  bool TryReset(Parameters wanted) {
    mfxExtEncoderResetOption reset{};
    reset.Header = {MFX_EXTBUFF_ENCODER_RESET_OPTION, sizeof(reset)};
    reset.StartNewSequence = MFX_CODINGOPTION_OFF;
    mfxExtBuffer* extensions[] = {wanted.extensions[0], wanted.extensions[1],
                                  wanted.extensions[2],
                                  reinterpret_cast<mfxExtBuffer*>(&reset)};
    wanted.p.ExtParam = extensions;
    wanted.p.NumExtParam = 4;
    Parameters queried(wanted);
    mfxExtEncoderResetOption query_reset = reset;
    mfxExtBuffer* query_extensions[] = {
        queried.extensions[0], queried.extensions[1], queried.extensions[2],
        reinterpret_cast<mfxExtBuffer*>(&query_reset)};
    queried.p.ExtParam = query_extensions;
    queried.p.NumExtParam = 4;
    mfxStatus status = MFXVideoENCODE_Query(session_, &wanted.p, &queried.p);
    if (status < 0 || query_reset.StartNewSequence != MFX_CODINGOPTION_OFF ||
        !Compatible(wanted, queried)) {
      return false;
    }
    status = MFXVideoENCODE_Reset(session_, &queried.p);
    LogParameters("Reset-no-IDR", status, queried);
    Parameters actual;
    if (status < 0 ||
        MFXVideoENCODE_GetVideoParam(session_, &actual.p) != MFX_ERR_NONE ||
        !Compatible(wanted, actual)) {
      return false;
    }
    parameters_ = actual;
    return true;
  }

  bool ResetRate(int kbps, int fps) {
    if (kbps == kbps_ && fps == fps_)
      return true;
    Parameters wanted(parameters_);
    SetRateFields(wanted, kbps, fps);
    wanted.p.mfx.BufferSizeInKB = parameters_.p.mfx.BufferSizeInKB;
    wanted.p.mfx.InitialDelayInKB = parameters_.p.mfx.InitialDelayInKB;
    wanted.p.mfx.CodecLevel = AvcLevel(width_, height_, fps, kbps);
    if (wanted.p.mfx.CodecLevel == parameters_.p.mfx.CodecLevel) {
      // Changing caps/VBV is not guaranteed to support seamless reset. Retry
      // with only TargetKbps/MaxKbps changed before considering a new sequence.
      if (TryReset(wanted)) {
        kbps_ = kbps;
        fps_ = fps;
        return true;
      }
      Parameters bitrate_only(parameters_);
      bitrate_only.p.mfx.TargetKbps = bitrate_only.p.mfx.MaxKbps =
          static_cast<mfxU16>(kbps);
      if (fps == fps_ && TryReset(bitrate_only)) {
        kbps_ = kbps;
        return true;
      }
    }
    RTC_LOG(LS_WARNING) << "PRTS VPL seamless reset unavailable; reinit + IDR"
                        << " kbps=" << kbps << " fps=" << fps;
    return Start(width_, height_, kbps, fps);
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
    if (const auto* native = VplNativeFrameBuffer::From(*buffer)) {
      ComPtr<ID3D11Device> owner;
      native->texture()->GetDevice(&owner);
      if (!ValidNv12(native->texture(), width_, height_) ||
          owner.Get() != device_.Get()) {
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
    auto capture_device = state_ ? state_->capture_device() : nullptr;
    if (const auto* native =
            VplNativeFrameBuffer::From(*frame.video_frame_buffer())) {
      // A frame queued before capture rebind still belongs to its old device.
      // Do not adopt the new device and then reject that valid queued surface.
      native->texture()->GetDevice(capture_device.ReleaseAndGetAddressOf());
    }
    if (frame.width() != width_ || frame.height() != height_ ||
        (capture_device && capture_device.Get() != device_.Get())) {
      // WebRTC normally calls InitEncode on resize. Also handle a direct
      // resize/device generation transition, and force new SPS/PPS + IDR.
      if (!Start(frame.width(), frame.height(), kbps_, fps_, capture_device))
        return Fail("resize/device reinit");
    }
    Slot* slot = Prepare(frame);
    if (!slot)
      return Fail("input surface");
    mfxEncodeCtrl control{};
    if (force_idr_)
      control.FrameType =
          MFX_FRAMETYPE_I | MFX_FRAMETYPE_IDR | MFX_FRAMETYPE_REF;
    mfxBitstream bits{};
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
        return Fail("device busy timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // No B frames and depth one are mandatory. Reject buffering/skips instead
    // of associating a delayed AU with the wrong capture metadata.
    if (status < 0 || !sync)
      return Fail("EncodeFrameAsync", status);
    // Never hold ID3D10Multithread::Enter or an application mutex here.
    status = MFXVideoCORE_SyncOperation(session_, sync, 1000);
    if (status != MFX_ERR_NONE)
      return Fail("SyncOperation", status);
    if (!bits.DataLength || bits.TimeStamp != slot->surface.Data.TimeStamp ||
        bits.DataOffset > bits.MaxLength ||
        bits.DataLength > bits.MaxLength - bits.DataOffset ||
        (bits.FrameType & MFX_FRAMETYPE_B)) {
      return Fail("unexpected buffered/invalid output");
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
        RTC_LOG(LS_INFO) << "PRTS VPL SPS profile=66 constraints="
                         << static_cast<int>(bytes[i + 5])
                         << " level=" << static_cast<int>(bytes[i + 6]);
      }
    }
    if (idr && (!sps || !pps))
      return Fail("IDR without SPS/PPS");
    if (idr)
      force_idr_ = false;
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
    output.SetPlayoutDelay(webrtc::VideoPlayoutDelay::Minimal());
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
    if (bits.DataLength > parameters_.co2.MaxFrameSize)
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
  int fps_ = 0;
  int max_fps_ = 60;
  bool initialized_ = false;
  bool force_idr_ = true;
  bool failed_ = false;
  bool paused_ = false;
  mfxU64 next_timestamp_ = 0;
  mfxU32 next_order_ = 0;
  uint64_t cap_violations_ = 0;
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
  impl_->max_fps_ = codec->maxFramerate;
  int kbps = static_cast<int>(std::clamp(codec->startBitrate, 1u, 20000u));
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
  int fps = impl_->max_fps_;
  if (std::isfinite(parameters.framerate_fps) && parameters.framerate_fps > 0) {
    fps = std::clamp(
        static_cast<int>(std::lround(std::min(parameters.framerate_fps, 60.0))),
        1, impl_->max_fps_);
  }
  // Use the media allocation directly. Physical-link headroom belongs upstream;
  // never reapply a hardcoded 10/12 factor to GCC's already allocated bitrate.
  int kbps =
      static_cast<int>(std::clamp((bps + 999ull) / 1000, 1ull, 20000ull));
  if (!impl_->ResetRate(kbps, fps))
    impl_->Fail("rate reset and rebuild");
}
webrtc::VideoEncoder::EncoderInfo VplEncoder::GetEncoderInfo() const {
  EncoderInfo info;
  info.implementation_name = "PRTS oneVPL D3D11 AVC";
  info.is_hardware_accelerated = true;
  info.supports_native_handle = true;
  info.supports_simulcast = false;
  info.has_trusted_rate_controller = false;  // T1 demonstrates real overshoot.
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
  RTC_LOG(LS_INFO) << "PRTS encoder selected="
                   << (use_vpl_ ? "vpl" : "openh264");
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
}  // namespace prts
