// Copyright 2026 The remotekit Authors
// Use of this source code is governed by a BSD-style license.

// NVIDIA NVENC (Video Codec SDK 12.1 API) AVC backend.
// UNTESTED ON NVIDIA HARDWARE: this machine has no NVIDIA GPU. It is compiled
// and its failure path is exercised (no adapter / driver DLL -> clean
// fallback), but no frame has ever been encoded with it.

#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#pragma allow_unsafe_libc_calls
#endif

#include <d3d11.h>
#include <d3d10.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/time/time.h"
#include "re/encoder_factory.h"
#include "re/encoder_params.h"
#include "re/third_party/nv-codec-headers/nvEncodeAPI.h"
#include "re/video_encoder_backend.h"
#include "re/vpl_encoder.h"
#include "third_party/libyuv/include/libyuv/convert_from.h"
#include "third_party/webrtc/api/video/encoded_image.h"
#include "third_party/webrtc/api/video/i420_buffer.h"
#include "third_party/webrtc/api/video/video_frame.h"
#include "third_party/webrtc/api/video_codecs/video_codec.h"
#include "third_party/webrtc/modules/video_coding/include/video_codec_interface.h"
#include "third_party/webrtc/modules/video_coding/include/video_error_codes.h"
#include "third_party/webrtc/rtc_base/time_utils.h"
#include "third_party/webrtc_overrides/rtc_base/logging.h"

namespace re {
namespace {
using Microsoft::WRL::ComPtr;
constexpr UINT kNvidiaVendor = 0x10de;
constexpr size_t kMaxBitstreamBytes = 16 * 1024 * 1024;
constexpr size_t kMaxRegistrations = 32;

int Align16(int value) {
  return (value + 15) & ~15;
}

bool ValidSize(int width, int height) {
  return width > 0 && height > 0 && width <= 4096 && height <= 4096 &&
         !(width & 1) && !(height & 1) &&
         static_cast<int64_t>(width) * height <= 1920 * 1080;
}

bool IsNvidia(ID3D11Device* device) {
  ComPtr<IDXGIDevice> dxgi;
  ComPtr<IDXGIAdapter> adapter;
  DXGI_ADAPTER_DESC desc{};
  return device && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi))) &&
         SUCCEEDED(dxgi->GetAdapter(&adapter)) &&
         SUCCEEDED(adapter->GetDesc(&desc)) && desc.VendorId == kNvidiaVendor;
}

// Padded NV12 as produced by the native frame pool.
bool ValidNv12(ID3D11Texture2D* texture, int width, int height) {
  D3D11_TEXTURE2D_DESC desc{};
  texture->GetDesc(&desc);
  return desc.Format == DXGI_FORMAT_NV12 && desc.ArraySize == 1 &&
         desc.MipLevels == 1 && desc.SampleDesc.Count == 1 &&
         desc.Width == static_cast<UINT>(Align16(width)) &&
         desc.Height == static_cast<UINT>(Align16(height)) &&
         desc.Usage == D3D11_USAGE_DEFAULT;
}

// Creates a hardware device on the first NVIDIA adapter; null if none.
ComPtr<ID3D11Device> CreateNvidiaDevice(std::string* error) {
  ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    *error = "DXGI factory unavailable";
    return nullptr;
  }
  for (UINT index = 0;; ++index) {
    ComPtr<IDXGIAdapter1> adapter;
    if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND)
      break;
    DXGI_ADAPTER_DESC1 desc{};
    if (!adapter || FAILED(adapter->GetDesc1(&desc)) ||
        desc.VendorId != kNvidiaVendor ||
        (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
      continue;
    ComPtr<ID3D11Device> device;
    if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN,
                                 nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                 nullptr, 0, D3D11_SDK_VERSION, &device,
                                 nullptr, nullptr))) {
      *error = "D3D11 device creation failed on the NVIDIA adapter";
      return nullptr;
    }
    return device;
  }
  *error = "no NVIDIA adapter present";
  return nullptr;
}

// Thin owner of one NVENC session on one D3D11 device. Methods return false
// and set error() on failure; nothing throws.
class NvencSession {
 public:
  ~NvencSession() {
    Close();
    if (dll_)
      FreeLibrary(dll_);
  }

  // Loads the driver's API from System32 only (no search-path hijacking).
  bool Load() {
    if (dll_)
      return true;
    dll_ = LoadLibraryExW(L"nvEncodeAPI64.dll", nullptr,
                          LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll_) {
      return Fail("nvEncodeAPI64.dll not found in System32 (Win32=" +
                  std::to_string(GetLastError()) +
                  "); needs an NVIDIA driver on an NVENC-capable GPU");
    }
    using CreateFn = NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*);
    using VersionFn = NVENCSTATUS(NVENCAPI*)(uint32_t*);
    auto create = reinterpret_cast<CreateFn>(
        GetProcAddress(dll_, "NvEncodeAPICreateInstance"));
    auto version = reinterpret_cast<VersionFn>(
        GetProcAddress(dll_, "NvEncodeAPIGetMaxSupportedVersion"));
    if (!create || !version)
      return Fail("NVENC driver DLL lacks the required entry points");
    uint32_t supported = 0;
    if (version(&supported) != NV_ENC_SUCCESS ||
        supported < ((NVENCAPI_MAJOR_VERSION << 4) | NVENCAPI_MINOR_VERSION)) {
      return Fail("NVENC driver API older than Video Codec SDK 12.1");
    }
    api_.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    return Check(create(&api_), "NvEncodeAPICreateInstance");
  }

  bool Open(ID3D11Device* device) {
    if (!IsNvidia(device))
      return Fail("the encoder device is not on an NVIDIA adapter");
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open{};
    open.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    open.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
    open.device = device;
    open.apiVersion = NVENCAPI_VERSION;
    return Check(api_.nvEncOpenEncodeSessionEx(&open, &session_),
                 "OpenEncodeSessionEx");
  }

  // P1..P3 + ultra-low-latency tuning, no B-frames, no lookahead, async off.
  bool Configure(const EncoderParams& p) {
    const GUID preset = p.nvenc_preset == 3   ? NV_ENC_PRESET_P3_GUID
                        : p.nvenc_preset == 2 ? NV_ENC_PRESET_P2_GUID
                                              : NV_ENC_PRESET_P1_GUID;
    NV_ENC_PRESET_CONFIG preset_config{};
    preset_config.version = NV_ENC_PRESET_CONFIG_VER;
    preset_config.presetCfg.version = NV_ENC_CONFIG_VER;
    if (!Check(api_.nvEncGetEncodePresetConfigEx(
                   session_, NV_ENC_CODEC_H264_GUID, preset,
                   NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY, &preset_config),
               "GetEncodePresetConfigEx")) {
      return false;
    }
    config_ = preset_config.presetCfg;
    config_.version = NV_ENC_CONFIG_VER;
    config_.profileGUID = NV_ENC_H264_PROFILE_BASELINE_GUID;  // constrained
    config_.gopLength = p.gop_frames ? static_cast<uint32_t>(p.gop_frames)
                                     : NVENC_INFINITE_GOPLENGTH;
    config_.frameIntervalP = 1;  // IPPP: no B-frames
    config_.frameFieldMode = NV_ENC_PARAMS_FRAME_FIELD_MODE_FRAME;
    config_.mvPrecision = NV_ENC_MV_PRECISION_QUARTER_PEL;
    auto& h264 = config_.encodeCodecConfig.h264Config;
    h264.level = static_cast<uint32_t>(p.ResolvedLevel());
    h264.idrPeriod = config_.gopLength;
    h264.maxNumRefFrames = 1;
    h264.useBFramesAsRef = NV_ENC_BFRAME_REF_MODE_DISABLED;
    h264.repeatSPSPPS = 1;
    h264.outputAUD = 1;
    h264.sliceMode = 3;  // fixed number of slices per picture
    h264.sliceModeData = static_cast<uint32_t>(p.slices);
    h264.enableIntraRefresh = p.intra_refresh_cycle != 0;
    h264.intraRefreshPeriod =
        p.intra_refresh_cycle ? p.intra_refresh_cycle + 1 : 0;
    h264.intraRefreshCnt = p.intra_refresh_cycle;
    h264.entropyCodingMode = NV_ENC_H264_ENTROPY_CODING_MODE_CAVLC;
    // BT.709 limited, matching the VPP output and the oneVPL stream.
    auto& vui = h264.h264VUIParameters;
    vui.videoSignalTypePresentFlag = vui.colourDescriptionPresentFlag = 1;
    vui.videoFormat = NV_ENC_VUI_VIDEO_FORMAT_UNSPECIFIED;
    vui.videoFullRangeFlag = 0;
    vui.colourPrimaries = NV_ENC_VUI_COLOR_PRIMARIES_BT709;
    vui.transferCharacteristics = NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709;
    vui.colourMatrix = NV_ENC_VUI_MATRIX_COEFFS_BT709;
    SetRates(config_, p);

    init_ = {};
    init_.version = NV_ENC_INITIALIZE_PARAMS_VER;
    init_.encodeGUID = NV_ENC_CODEC_H264_GUID;
    init_.presetGUID = preset;
    init_.encodeWidth = init_.darWidth = p.width;
    init_.encodeHeight = init_.darHeight = p.height;
    init_.maxEncodeWidth = p.width;
    init_.maxEncodeHeight = p.height;
    init_.frameRateNum = p.fps;
    init_.frameRateDen = 1;
    init_.enableEncodeAsync = 0;  // synchronous, no output queue
    init_.enablePTD = 1;
    init_.tuningInfo = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
    init_.encodeConfig = &config_;
    if (!Check(api_.nvEncInitializeEncoder(session_, &init_),
               "InitializeEncoder")) {
      return false;
    }
    NV_ENC_CREATE_BITSTREAM_BUFFER bitstream{};
    bitstream.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    if (!Check(api_.nvEncCreateBitstreamBuffer(session_, &bitstream),
               "CreateBitstreamBuffer")) {
      return false;
    }
    output_ = bitstream.bitstreamBuffer;
    params_ = p;
    return true;
  }

  // Rate-only reconfigure without IDR. False leaves the old rates active.
  bool Reconfigure(const EncoderParams& p) {
    NV_ENC_CONFIG config = config_;
    SetRates(config, p);
    NV_ENC_RECONFIGURE_PARAMS reset{};
    reset.version = NV_ENC_RECONFIGURE_PARAMS_VER;
    reset.reInitEncodeParams = init_;
    reset.reInitEncodeParams.encodeConfig = &config;
    reset.resetEncoder = 0;
    reset.forceIDR = 0;
    const NVENCSTATUS status = api_.nvEncReconfigureEncoder(session_, &reset);
    if (status != NV_ENC_SUCCESS) {
      return Fail("ReconfigureEncoder rejected, status=" +
                  std::to_string(status));
    }
    config_ = config;
    params_ = p;
    return true;
  }

  struct Output {
    const uint8_t* data = nullptr;
    size_t size = 0;
    bool idr = false;
  };

  // Synchronous: returns after the access unit is available. `texture` must be
  // NV12 on the session device; the caller keeps it alive during the call.
  bool Encode(ID3D11Texture2D* texture,
              uint64_t timestamp,
              bool force_idr,
              Output* out) {
    error_.clear();
    NV_ENC_REGISTERED_PTR registered = Register(texture);
    if (!registered)
      return false;
    NV_ENC_MAP_INPUT_RESOURCE mapped{};
    mapped.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
    mapped.registeredResource = registered;
    if (!Check(api_.nvEncMapInputResource(session_, &mapped),
               "MapInputResource")) {
      return false;
    }
    NV_ENC_PIC_PARAMS picture{};
    picture.version = NV_ENC_PIC_PARAMS_VER;
    picture.inputBuffer = mapped.mappedResource;
    picture.bufferFmt = mapped.mappedBufferFmt;
    picture.inputWidth = params_.width;
    picture.inputHeight = params_.height;
    picture.outputBitstream = output_;
    picture.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    picture.inputTimeStamp = timestamp;
    if (force_idr) {
      picture.encodePicFlags =
          NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
    }
    bool ok = Check(api_.nvEncEncodePicture(session_, &picture),
                    "EncodePicture (buffering is not allowed)");
    if (ok) {
      NV_ENC_LOCK_BITSTREAM locked{};
      locked.version = NV_ENC_LOCK_BITSTREAM_VER;
      locked.outputBitstream = output_;
      locked.doNotWait = 0;
      ok = Check(api_.nvEncLockBitstream(session_, &locked), "LockBitstream");
      if (ok) {
        // No reordering is configured; anything else would mis-associate
        // capture metadata, so treat it as a failed frame.
        const bool valid = locked.outputTimeStamp == timestamp &&
                           locked.pictureType != NV_ENC_PIC_TYPE_B &&
                           locked.bitstreamSizeInBytes &&
                           locked.bitstreamSizeInBytes <= kMaxBitstreamBytes;
        if (valid) {
          const auto* data =
              static_cast<const uint8_t*>(locked.bitstreamBufferPtr);
          bytes_.assign(data, data + locked.bitstreamSizeInBytes);
          out->idr = locked.pictureType == NV_ENC_PIC_TYPE_IDR;
        } else {
          Fail("invalid or reordered access unit");
        }
        ok = Check(api_.nvEncUnlockBitstream(session_, output_),
                   "UnlockBitstream") &&
             valid;
      }
    }
    api_.nvEncUnmapInputResource(session_, mapped.mappedResource);
    if (!ok)
      return false;
    out->data = bytes_.data();
    out->size = bytes_.size();
    return true;
  }

  void Close() {
    if (session_) {
      for (auto& entry : registrations_)
        api_.nvEncUnregisterResource(session_, entry.registered);
      if (output_)
        api_.nvEncDestroyBitstreamBuffer(session_, output_);
      api_.nvEncDestroyEncoder(session_);
    }
    registrations_.clear();
    output_ = nullptr;
    session_ = nullptr;
  }

  const std::string& error() const { return error_; }
  uint32_t vbv_bits() const { return config_.rcParams.vbvBufferSize; }

 private:
  struct Registration {
    ComPtr<ID3D11Texture2D> texture;
    NV_ENC_REGISTERED_PTR registered;
  };

  // The producer pool recycles a small fixed set of textures, so registrations
  // are cached per texture and evicted oldest-first.
  NV_ENC_REGISTERED_PTR Register(ID3D11Texture2D* texture) {
    for (auto& entry : registrations_) {
      if (entry.texture.Get() == texture)
        return entry.registered;
    }
    if (registrations_.size() >= kMaxRegistrations) {
      api_.nvEncUnregisterResource(session_, registrations_.front().registered);
      registrations_.erase(registrations_.begin());
    }
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    NV_ENC_REGISTER_RESOURCE registration{};
    registration.version = NV_ENC_REGISTER_RESOURCE_VER;
    registration.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
    registration.resourceToRegister = texture;
    registration.width = desc.Width;
    registration.height = desc.Height;
    registration.bufferFormat = NV_ENC_BUFFER_FORMAT_NV12;
    registration.bufferUsage = NV_ENC_INPUT_IMAGE;
    if (!Check(api_.nvEncRegisterResource(session_, &registration),
               "RegisterResource")) {
      return nullptr;
    }
    registrations_.push_back({texture, registration.registeredResource});
    return registration.registeredResource;
  }

  static void SetRates(NV_ENC_CONFIG& config, const EncoderParams& p) {
    auto& rc = config.rcParams;
    const bool cbr = p.rate_control == RateControl::kCbr;
    rc.version = NV_ENC_RC_PARAMS_VER;
    rc.rateControlMode = cbr ? NV_ENC_PARAMS_RC_CBR : NV_ENC_PARAMS_RC_VBR;
    rc.averageBitRate = static_cast<uint32_t>(p.target_kbps) * 1000;
    rc.maxBitRate =
        static_cast<uint32_t>(cbr ? p.target_kbps : p.max_kbps) * 1000;
    rc.vbvBufferSize = static_cast<uint32_t>(p.VbvKb()) * 8000;
    // SDK 12.1 has no AVC MaxFrameSize. The frame cap is only a VBV hint, not
    // a byte guarantee; valid reference pictures are never dropped for size.
    if (const uint32_t cap = p.FrameCapBytes())
      rc.vbvBufferSize = std::min(rc.vbvBufferSize, cap * 8);
    rc.vbvInitialDelay = rc.vbvBufferSize / 2;
    rc.enableAQ = rc.enableTemporalAQ = rc.enableLookahead = 0;
    rc.disableIadapt = rc.disableBadapt = 1;
    rc.lookaheadDepth = 0;
    rc.zeroReorderDelay = 1;
    rc.multiPass = NV_ENC_MULTI_PASS_DISABLED;
    rc.lowDelayKeyFrameScale = 1;
  }

  bool Check(NVENCSTATUS status, const char* operation) {
    if (status == NV_ENC_SUCCESS)
      return true;
    std::string detail;
    if (session_ && api_.nvEncGetLastErrorString) {
      if (const char* text = api_.nvEncGetLastErrorString(session_))
        detail = text;
    }
    return Fail(std::string(operation) +
                " failed, status=" + std::to_string(status) + " " + detail);
  }

  bool Fail(std::string message) {
    error_ = std::move(message);
    return false;
  }

  HMODULE dll_ = nullptr;
  NV_ENCODE_API_FUNCTION_LIST api_{};
  void* session_ = nullptr;
  NV_ENC_CONFIG config_{};
  NV_ENC_INITIALIZE_PARAMS init_{};
  NV_ENC_OUTPUT_PTR output_ = nullptr;
  EncoderParams params_;
  std::vector<Registration> registrations_;
  std::vector<uint8_t> bytes_;
  std::string error_;
};

// webrtc::VideoEncoder around NvencSession; mirrors VplEncoder's contract:
// every method runs on WebRTC's encoder queue, synchronous, no frame queue.
class NvencEncoder final : public webrtc::VideoEncoder {
 public:
  NvencEncoder(std::shared_ptr<VplEncoderState> state,
               const EncoderParams& base)
      : state_(std::move(state)), base_(base) {}
  ~NvencEncoder() override { Close(); }

  int InitEncode(const webrtc::VideoCodec* codec, const Settings&) override {
    Close();
    if (!codec || codec->codecType != webrtc::kVideoCodecH264 ||
        !ValidSize(codec->width, codec->height) || !codec->maxFramerate ||
        codec->maxFramerate > 60 || codec->numberOfSimulcastStreams > 1 ||
        codec->H264().numberOfTemporalLayers > 1) {
      RTC_LOG(LS_WARNING)
          << "PRTS NVENC unsupported configuration; fallback encoder=openh264";
      return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }
    max_kbps_ = std::clamp(
        codec->maxBitrate ? static_cast<int>(codec->maxBitrate)
                          : state_->max_bitrate_bps.load() / 1000,
        1, 50000);
    min_kbps_ = std::min(static_cast<int>(codec->minBitrate), max_kbps_);
    // Initialize at the negotiated ceiling like the oneVPL path; GCC's start
    // target arrives as the first (seamless) reconfigure.
    pending_kbps_ = std::clamp(static_cast<int>(codec->startBitrate),
                               std::max(1, min_kbps_), max_kbps_);
    paused_ = codec->startBitrate == 0;
    fps_ = codec->maxFramerate;
    if (!Start(codec->width, codec->height, nullptr))
      return Fail(nullptr);
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override {
    callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Release() override {
    Close();
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Encode(const webrtc::VideoFrame& frame,
                 const std::vector<webrtc::VideoFrameType>* types) override {
    if (!initialized_ || failed_)
      return WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE;
    if (!callback_)
      return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    if (types && std::find(types->begin(), types->end(),
                           webrtc::VideoFrameType::kVideoFrameKey) !=
                     types->end()) {
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
    auto buffer = frame.video_frame_buffer();
    const auto* native = VplNativeFrameBuffer::From(*buffer);
    ComPtr<ID3D11Device> native_device;
    if (native)
      native->texture()->GetDevice(&native_device);
    // Zero-copy only when the frame pool lives on this NVIDIA device. When the
    // capture device is NVIDIA but not ours yet, adopt it (reinit + IDR).
    ComPtr<ID3D11Device> capture = state_->capture_device();
    const bool adopt = native_device && native_device == capture &&
                       native_device != device_ &&
                       IsNvidia(native_device.Get());
    const int target_fps = state_->target_fps.load();
    if (frame.width() != width_ || frame.height() != height_ || adopt ||
        target_fps != fps_) {
      fps_ = target_fps;
      if (!Start(frame.width(), frame.height(),
                 adopt ? native_device.Get() : nullptr)) {
        return Fail(nullptr);
      }
    }
    if (pending_kbps_ > 0)
      ApplyRate(pending_kbps_);

    ID3D11Texture2D* input = nullptr;
    if (native && native_device == device_ &&
        ValidNv12(native->texture(), width_, height_)) {
      input = native->texture();
    } else {
      input = Upload(buffer);  // I420 bridge: capture on another adapter
    }
    if (!input)
      return Drop(frame, "input surface", false);
    // The frame pool's VPP work was submitted on the (shared) immediate
    // context by another thread; make sure it is queued before NVENC reads it.
    context_->Flush();

    const uint64_t timestamp = next_timestamp_++;
    const int64_t encode_start_ms = webrtc::TimeMillis();
    NvencSession::Output au;
    if (!session_.Encode(input, timestamp, force_idr_, &au))
      return Drop(frame, session_.error().c_str(), true);
    if (native)
      native->RecordVppCompletion(context_.Get());
    ++frames_;
    encoded_bytes_ += au.size;
    if (au.idr) {
      LOG(INFO) << "PRTS NVENC IDR " << width_ << "x" << height_
                << " bytes=" << au.size;
      force_idr_ = false;
    }
    LogStats();
    consecutive_errors_ = 0;

    webrtc::EncodedImage output;
    output.SetEncodedData(webrtc::EncodedImageBuffer::Create(au.data, au.size));
    output._encodedWidth = width_;
    output._encodedHeight = height_;
    output.SetRtpTimestamp(frame.rtp_timestamp());
    output.capture_time_ms_ = frame.timestamp_us() / 1000;
    output.ntp_time_ms_ = frame.ntp_time_ms();
    output.rotation_ = frame.rotation();
    output.SetColorSpace(frame.color_space());
    output.SetSimulcastIndex(0);
    output.set_end_of_temporal_unit(true);
    output.set_frame_type(au.idr ? webrtc::VideoFrameType::kVideoFrameKey
                                 : webrtc::VideoFrameType::kVideoFrameDelta);
    output.content_type_ = webrtc::VideoContentType::SCREENSHARE;
    SetPlayoutDelay(output);
    output.qp_ = -1;
    output.SetEncodeTime(encode_start_ms, webrtc::TimeMillis());
    webrtc::CodecSpecificInfo info;
    info.codecType = webrtc::kVideoCodecH264;
    info.codecSpecific.H264.packetization_mode =
        webrtc::H264PacketizationMode::NonInterleaved;
    info.codecSpecific.H264.temporal_idx = webrtc::kNoTemporalIdx;
    info.codecSpecific.H264.base_layer_sync = false;
    info.codecSpecific.H264.idr_frame = au.idr;
    auto result = callback_->OnEncodedImage(output, &info);
    return result.error == webrtc::EncodedImageCallback::Result::OK
               ? WEBRTC_VIDEO_CODEC_OK
               : WEBRTC_VIDEO_CODEC_ERROR;
  }

  void SetRates(const RateControlParameters& parameters) override {
    if (!initialized_ || failed_)
      return;
    const uint32_t bps = parameters.bitrate.get_sum_bps();
    paused_ = bps == 0;
    if (paused_)
      return;  // Zero rate pauses; never configure a zero target.
    const uint64_t kbps = (bps + 999ull) / 1000;
    ApplyRate(static_cast<int>(
        std::clamp<uint64_t>(kbps, std::max(1, min_kbps_), max_kbps_)));
  }

  EncoderInfo GetEncoderInfo() const override {
    EncoderInfo info;
    info.implementation_name = "PRTS NVENC D3D11 AVC";
    info.is_hardware_accelerated = true;
    info.supports_native_handle = true;
    info.supports_simulcast = false;
    info.has_trusted_rate_controller = false;
    const int min_bps = state_->min_bitrate_bps.load();
    const int max_bps = state_->max_bitrate_bps.load();
    info.resolution_bitrate_limits.emplace_back(1920 * 1080, min_bps, min_bps,
                                                max_bps);
    info.is_qp_trusted = false;
    info.scaling_settings = ScalingSettings::kOff;
    info.requested_resolution_alignment = 2;
    info.max_pixels_per_frame = 1920 * 1080;
    info.preferred_pixel_formats = {webrtc::VideoFrameBuffer::Type::kI420,
                                    webrtc::VideoFrameBuffer::Type::kNative};
    return info;
  }

 private:
  bool Start(int width, int height, ID3D11Device* adopted) {
    Close();
    width_ = width;
    height_ = height;
    if (adopted) {
      device_ = adopted;
    } else {
      // Prefer the capture device when it already is an NVIDIA device.
      ComPtr<ID3D11Device> capture = state_->capture_device();
      if (capture && IsNvidia(capture.Get())) {
        device_ = capture;
      } else {
        device_ = CreateNvidiaDevice(&last_error_);
        if (!device_)
          return false;
      }
    }
    device_->GetImmediateContext(&context_);
    ComPtr<ID3D10Multithread> multithread;
    if (FAILED(context_.As(&multithread))) {
      last_error_ = "D3D11 multithread interface unavailable";
      return false;
    }
    multithread->SetMultithreadProtected(TRUE);
    const EncoderParams params =
        base_.ForStream(width, height, fps_, max_kbps_, min_kbps_, max_kbps_);
    if (!session_.Load() || !session_.Open(device_.Get()) ||
        !session_.Configure(params)) {
      last_error_ = session_.error();
      return false;
    }
    current_kbps_ = max_kbps_;
    force_idr_ = true;
    failed_ = false;
    initialized_ = true;
    packed_.resize(static_cast<size_t>(Align16(width)) * Align16(height) * 3 /
                   2);
    state_->native_pool_limit.store(8);
    state_->SetEncoderDevice(device_);
    LOG(INFO) << "PRTS encoder active=nvenc " << width << 'x' << height
              << " fps=" << fps_ << " SPS_level=" << params.ResolvedLevel()
              << " preset=P" << params.nvenc_preset
              << " vbv_bits=" << session_.vbv_bits()
              << " (UNTESTED ON NVIDIA HARDWARE)";
    return true;
  }

  void Close() {
    session_.Close();
    if (device_)
      state_->ClearEncoderDevice(device_.Get());
    upload_.Reset();
    context_.Reset();
    device_.Reset();
    initialized_ = false;
  }

  void ApplyRate(int kbps) {
    pending_kbps_ = 0;
    if (kbps == current_kbps_)
      return;
    const EncoderParams params =
        base_.ForStream(width_, height_, fps_, kbps, min_kbps_, max_kbps_);
    if (session_.Reconfigure(params)) {
      current_kbps_ = kbps;
    } else {
      // Keep streaming at the previous rate; never rebuild for GCC changes.
      RTC_LOG(LS_WARNING) << "PRTS NVENC rate reconfigure rejected: "
                          << session_.error()
                          << "; retain kbps=" << current_kbps_;
    }
  }

  // I420 -> padded NV12 on the encoder device for non-zero-copy frames.
  ID3D11Texture2D* Upload(
      const webrtc::scoped_refptr<webrtc::VideoFrameBuffer>& buffer) {
    auto i420 = buffer->ToI420();
    if (!i420 || i420->width() != width_ || i420->height() != height_)
      return nullptr;
    const int pitch = Align16(width_);
    const int padded_height = Align16(height_);
    if (!upload_) {
      D3D11_TEXTURE2D_DESC desc{};
      desc.Width = pitch;
      desc.Height = padded_height;
      desc.Format = DXGI_FORMAT_NV12;
      desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = D3D11_BIND_RENDER_TARGET;
      if (FAILED(device_->CreateTexture2D(&desc, nullptr, &upload_)))
        return nullptr;
    }
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
    context_->UpdateSubresource(upload_.Get(), 0, nullptr, packed_.data(),
                                pitch, 0);
    return upload_.Get();
  }

  void LogStats() {
    const auto now = base::TimeTicks::Now();
    if (now - stats_start_ < base::Seconds(5))
      return;
    LOG(INFO) << "PRTS NVENC stats5s frames=" << frames_ << " send_kbps="
              << encoded_bytes_ * 8 / (now - stats_start_).InMillisecondsF()
              << " target_kbps=" << current_kbps_ << " max_kbps=" << max_kbps_;
    frames_ = encoded_bytes_ = 0;
    stats_start_ = now;
  }

  int Drop(const webrtc::VideoFrame& frame,
           const char* operation,
           bool reference_lost) {
    if (++consecutive_errors_ >= 8)
      return Fail(operation);
    force_idr_ |= reference_lost;
    if (consecutive_errors_ == 1) {
      LOG(WARNING) << "PRTS NVENC transient " << operation
                   << "; drop raw frame";
    }
    callback_->OnFrameDropped(frame.rtp_timestamp(), 0, true);
    return WEBRTC_VIDEO_CODEC_OK;
  }

  // Reports the failure and asks WebRTC to switch to the software encoder.
  int Fail(const char* operation) {
    RTC_LOG(LS_ERROR) << "PRTS NVENC " << (operation ? operation : last_error_)
                      << "; fallback encoder=openh264";
    failed_ = true;
    Close();
    return WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE;
  }

  std::shared_ptr<VplEncoderState> state_;
  const EncoderParams base_;
  NvencSession session_;
  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<ID3D11Texture2D> upload_;
  std::vector<uint8_t> packed_;
  webrtc::EncodedImageCallback* callback_ = nullptr;
  std::string last_error_;
  int width_ = 0;
  int height_ = 0;
  int fps_ = 30;
  int min_kbps_ = 0;
  int max_kbps_ = 0;
  int current_kbps_ = 0;
  int pending_kbps_ = 0;
  int consecutive_errors_ = 0;
  uint64_t next_timestamp_ = 0;
  uint64_t frames_ = 0;
  uint64_t encoded_bytes_ = 0;
  base::TimeTicks stats_start_ = base::TimeTicks::Now();
  bool initialized_ = false;
  bool force_idr_ = true;
  bool failed_ = false;
  bool paused_ = false;
};

class NvencBackend final : public VideoEncoderBackend {
 public:
  explicit NvencBackend(std::shared_ptr<VplEncoderState> state)
      : state_(std::move(state)) {}
  const char* name() const override { return "nvenc"; }
  const char* label() const override { return "NVENC"; }

  // Loads the driver and opens (then closes) a throwaway session so that a
  // missing GPU, driver or API version is reported once, up front.
  bool Probe() override {
    NvencSession session;
    std::string error;
    auto device = CreateNvidiaDevice(&error);
    if (device && session.Load() && session.Open(device.Get()))
      return true;
    LOG(WARNING) << "PRTS NVENC probe failed: "
                 << (device ? session.error() : error);
    return false;
  }

  std::unique_ptr<webrtc::VideoEncoder> Create(
      const webrtc::Environment&,
      const webrtc::SdpVideoFormat&,
      const EncoderParams& base) override {
    return std::make_unique<NvencEncoder>(state_, base);
  }

 private:
  std::shared_ptr<VplEncoderState> state_;
};

}  // namespace

std::unique_ptr<VideoEncoderBackend> CreateNvencBackend(
    std::shared_ptr<VplEncoderState> state) {
  return std::make_unique<NvencBackend>(std::move(state));
}

}  // namespace re
