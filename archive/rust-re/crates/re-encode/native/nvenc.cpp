// Copyright 2026 The remotekit Authors. BSD-3-Clause.
// NVENC is compile-checked only: UNTESTED ON NVIDIA HARDWARE.
#include "bridge.h"
#include "nvEncodeAPI.h"
#include <dxgi.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {
using Microsoft::WRL::ComPtr;
class Nvenc final : public ReEngine {
 public:
  ~Nvenc() override {
    if (session_) {
      for (auto& resource : resources_) {
        if (resource.mapped) api_.nvEncUnmapInputResource(session_, resource.mapped);
        if (resource.registered) api_.nvEncUnregisterResource(session_, resource.registered);
      }
      if (output_) api_.nvEncDestroyBitstreamBuffer(session_, output_);
      api_.nvEncDestroyEncoder(session_);
    }
    if (dll_) FreeLibrary(dll_);
  }
  void Check(NVENCSTATUS status, const char* operation) {
    if (status == NV_ENC_SUCCESS) return;
    std::string detail;
    if (session_ && api_.nvEncGetLastErrorString) {
      const char* text = api_.nvEncGetLastErrorString(session_);
      if (text) detail = text;
    }
    throw std::runtime_error(std::string("NVENC ")+operation+" failed (unsupported device/driver/parameters), status="+
      std::to_string(status)+" "+detail);
  }
  void Start(ID3D11Device* device, const ReParams& p) {
    dll_ = LoadLibraryExW(L"nvEncodeAPI64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll_) throw std::runtime_error("NVENC: nvEncodeAPI64.dll missing or unloadable in System32 (Win32="+
      std::to_string(GetLastError())+"); install an NVIDIA driver on an NVENC-capable device");
    auto create = reinterpret_cast<NVENCSTATUS (NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*)>(GetProcAddress(dll_, "NvEncodeAPICreateInstance"));
    auto version = reinterpret_cast<NVENCSTATUS (NVENCAPI*)(uint32_t*)>(GetProcAddress(dll_, "NvEncodeAPIGetMaxSupportedVersion"));
    if (!create || !version) throw std::runtime_error("NVENC DLL is missing required API entry points");
    uint32_t supported = 0;
    Check(version(&supported), "GetMaxSupportedVersion");
    if (supported < ((NVENCAPI_MAJOR_VERSION<<4)|NVENCAPI_MINOR_VERSION))
      throw std::runtime_error("NVENC driver API too old: requires Video Codec SDK 12.1 or newer");
    api_.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    Check(create(&api_), "CreateInstance");
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgi))) || FAILED(dxgi->GetAdapter(&adapter)) ||
        FAILED(adapter->GetDesc(&desc)) || desc.VendorId != 0x10de)
      throw std::runtime_error("NVENC: no NVIDIA device on the supplied D3D11 adapter");
    device_ = device;
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open{};
    open.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    open.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
    open.device = device;
    open.apiVersion = NVENCAPI_VERSION;
    Check(api_.nvEncOpenEncodeSessionEx(&open, &session_), "OpenEncodeSessionEx");
    const GUID preset = p.preset == 1 ? NV_ENC_PRESET_P1_GUID : p.preset == 2 ? NV_ENC_PRESET_P2_GUID : NV_ENC_PRESET_P3_GUID;
    NV_ENC_PRESET_CONFIG preset_config{};
    preset_config.version = NV_ENC_PRESET_CONFIG_VER;
    preset_config.presetCfg.version = NV_ENC_CONFIG_VER;
    Check(api_.nvEncGetEncodePresetConfigEx(session_, NV_ENC_CODEC_H264_GUID, preset,
      NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY, &preset_config), "GetEncodePresetConfigEx");
    config_ = preset_config.presetCfg;
    config_.version = NV_ENC_CONFIG_VER;
    config_.profileGUID = p.profile == 578 ? NV_ENC_H264_PROFILE_BASELINE_GUID :
      p.profile == 77 ? NV_ENC_H264_PROFILE_MAIN_GUID : NV_ENC_H264_PROFILE_HIGH_GUID;
    config_.gopLength = p.gop ? p.gop : NVENC_INFINITE_GOPLENGTH;
    config_.frameIntervalP = 1;
    config_.frameFieldMode = NV_ENC_PARAMS_FRAME_FIELD_MODE_FRAME;
    config_.mvPrecision = NV_ENC_MV_PRECISION_QUARTER_PEL;
    auto& h = config_.encodeCodecConfig.h264Config;
    h.level = p.level;
    h.idrPeriod = config_.gopLength;
    h.maxNumRefFrames = 1;
    h.useBFramesAsRef = NV_ENC_BFRAME_REF_MODE_DISABLED;
    h.repeatSPSPPS = 1;
    h.outputAUD = 1;
    h.sliceMode = 3; h.sliceModeData = p.slices;
    h.enableIntraRefresh = p.refresh != 0;
    h.intraRefreshPeriod = p.refresh ? p.refresh+1 : 0;
    h.intraRefreshCnt = p.refresh;
    h.entropyCodingMode = p.profile == 578 ? NV_ENC_H264_ENTROPY_CODING_MODE_CAVLC : NV_ENC_H264_ENTROPY_CODING_MODE_CABAC;
    auto& vui = h.h264VUIParameters;
    vui.videoSignalTypePresentFlag = vui.colourDescriptionPresentFlag = 1;
    vui.videoFormat = NV_ENC_VUI_VIDEO_FORMAT_UNSPECIFIED;
    vui.videoFullRangeFlag = 0;
    vui.colourPrimaries = NV_ENC_VUI_COLOR_PRIMARIES_BT709;
    vui.transferCharacteristics = NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709;
    vui.colourMatrix = NV_ENC_VUI_MATRIX_COEFFS_BT709;
    Rates(config_, p);
    init_.version = NV_ENC_INITIALIZE_PARAMS_VER;
    init_.encodeGUID = NV_ENC_CODEC_H264_GUID;
    init_.presetGUID = preset;
    init_.encodeWidth = init_.darWidth = p.width;
    init_.encodeHeight = init_.darHeight = p.height;
    init_.maxEncodeWidth = p.width; init_.maxEncodeHeight = p.height;
    init_.frameRateNum = p.fps; init_.frameRateDen = 1;
    init_.enableEncodeAsync = 0;
    init_.enablePTD = 1;
    init_.tuningInfo = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
    init_.encodeConfig = &config_;
    Check(api_.nvEncInitializeEncoder(session_, &init_), "InitializeEncoder");
    NV_ENC_CREATE_BITSTREAM_BUFFER bitstream{};
    bitstream.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    Check(api_.nvEncCreateBitstreamBuffer(session_, &bitstream), "CreateBitstreamBuffer");
    output_ = bitstream.bitstreamBuffer;
    params_ = p;
    diagnostics = "NVENC SDK 12.1; P"+std::to_string(p.preset)+" ULTRA_LOW_LATENCY; async=0 reorder=0 lookahead=0; vbv_bits="+
      std::to_string(config_.rcParams.vbvBufferSize)+"; frame cap mapped to VBV (best effort); UNTESTED ON NVIDIA HARDWARE";
  }
  int Reset(const ReParams& p) override {
    NV_ENC_CONFIG config = config_;
    Rates(config, p);
    NV_ENC_RECONFIGURE_PARAMS reset{};
    reset.version = NV_ENC_RECONFIGURE_PARAMS_VER;
    reset.reInitEncodeParams = init_;
    reset.reInitEncodeParams.frameRateNum = p.fps;
    reset.reInitEncodeParams.encodeConfig = &config;
    reset.resetEncoder = 0; reset.forceIDR = 0;
    const auto status = api_.nvEncReconfigureEncoder(session_, &reset);
    if (status != NV_ENC_SUCCESS) {
      error = "NVENC seamless reconfigure unsupported/rejected, status="+std::to_string(status);
      return 1;
    }
    config_ = config;
    init_.frameRateNum = p.fps;
    params_ = p;
    return 0;
  }
  int Encode(ID3D11Texture2D* texture, ReLease lease, uint64_t timestamp, bool idr, ReAu& output) override {
    Resource* resource = nullptr;
    for (auto& entry : resources_) if (entry.texture.Get() == texture) { resource = &entry; break; }
    if (!resource) {
      // Registration cache owns COM references but never producer pool leases after completion.
      if (resources_.size() == 32) {
        Check(api_.nvEncUnregisterResource(session_, resources_.front().registered), "evict input registration");
        resources_.erase(resources_.begin());
      }
      D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
      if (desc.Format != DXGI_FORMAT_NV12 || desc.ArraySize != 1 || desc.MipLevels != 1)
        return Fail("NVENC expects a single-subresource NV12 texture");
      NV_ENC_REGISTER_RESOURCE registration{};
      registration.version = NV_ENC_REGISTER_RESOURCE_VER;
      registration.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
      registration.resourceToRegister = texture;
      registration.width = desc.Width; registration.height = desc.Height;
      registration.bufferFormat = NV_ENC_BUFFER_FORMAT_NV12;
      registration.bufferUsage = NV_ENC_INPUT_IMAGE;
      Check(api_.nvEncRegisterResource(session_, &registration), "RegisterResource");
      resources_.push_back({texture, registration.registeredResource, nullptr});
      resource = &resources_.back();
    }
    resource->lease = std::move(lease);
    NV_ENC_MAP_INPUT_RESOURCE mapped{};
    mapped.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
    mapped.registeredResource = resource->registered;
    Check(api_.nvEncMapInputResource(session_, &mapped), "MapInputResource");
    resource->mapped = mapped.mappedResource;
    NV_ENC_PIC_PARAMS picture{};
    picture.version = NV_ENC_PIC_PARAMS_VER;
    picture.inputBuffer = mapped.mappedResource;
    picture.bufferFmt = mapped.mappedBufferFmt;
    picture.inputWidth = params_.width; picture.inputHeight = params_.height;
    picture.outputBitstream = output_;
    picture.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    picture.inputTimeStamp = timestamp;
    if (idr) picture.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
    Check(api_.nvEncEncodePicture(session_, &picture), "EncodePicture (buffering is disallowed)");
    NV_ENC_LOCK_BITSTREAM locked{};
    locked.version = NV_ENC_LOCK_BITSTREAM_VER;
    locked.outputBitstream = output_;
    locked.doNotWait = 0;
    Check(api_.nvEncLockBitstream(session_, &locked), "LockBitstream");
    bool valid = locked.outputTimeStamp == timestamp && locked.pictureType != NV_ENC_PIC_TYPE_B &&
      locked.bitstreamSizeInBytes && locked.bitstreamSizeInBytes <= 16*1024*1024;
    // Unlock even if copying the output throws.
    try {
      if (valid) bytes_.assign(static_cast<uint8_t*>(locked.bitstreamBufferPtr),
        static_cast<uint8_t*>(locked.bitstreamBufferPtr)+locked.bitstreamSizeInBytes);
    } catch (...) { api_.nvEncUnlockBitstream(session_, output_); throw; }
    Check(api_.nvEncUnlockBitstream(session_, output_), "UnlockBitstream");
    Check(api_.nvEncUnmapInputResource(session_, resource->mapped), "UnmapInputResource");
    resource->mapped = nullptr;
    resource->lease.Clear();
    if (!valid) return Fail("NVENC invalid/reordered access unit");
    output = {bytes_.data(), bytes_.size(), timestamp,
      locked.pictureType == NV_ENC_PIC_TYPE_IDR ? 0u : locked.pictureType == NV_ENC_PIC_TYPE_I ? 1u : 2u};
    return 0;
  }
 private:
  static void Rates(NV_ENC_CONFIG& config, const ReParams& p) {
    auto& r = config.rcParams;
    r.version = NV_ENC_RC_PARAMS_VER;
    r.rateControlMode = p.cbr ? NV_ENC_PARAMS_RC_CBR : NV_ENC_PARAMS_RC_VBR;
    r.averageBitRate = p.target_kbps*1000;
    r.maxBitRate = (p.cbr ? p.target_kbps : p.max_kbps)*1000;
    r.vbvBufferSize = p.vbv_kb*8000;
    // SDK 12.1 exposes no global AVC MaxFrameSize. A smaller VBV is only a hint,
    // not a byte guarantee; retain all valid reference pictures above this cap.
    if (!p.cbr && p.cap_bytes) r.vbvBufferSize = std::min(r.vbvBufferSize, p.cap_bytes*8);
    r.vbvInitialDelay = r.vbvBufferSize/2;
    r.enableAQ = r.enableTemporalAQ = r.enableLookahead = 0;
    r.disableIadapt = r.disableBadapt = 1;
    r.lookaheadDepth = 0; r.zeroReorderDelay = 1;
    r.multiPass = NV_ENC_MULTI_PASS_DISABLED;
    r.lowDelayKeyFrameScale = 1;
  }
  struct Resource {
    ComPtr<ID3D11Texture2D> texture;
    NV_ENC_REGISTERED_PTR registered;
    NV_ENC_INPUT_PTR mapped;
    ReLease lease;
  };
  HMODULE dll_ = nullptr;
  NV_ENCODE_API_FUNCTION_LIST api_{};
  void* session_ = nullptr;
  ComPtr<ID3D11Device> device_;
  NV_ENC_CONFIG config_{};
  NV_ENC_INITIALIZE_PARAMS init_{};
  NV_ENC_OUTPUT_PTR output_ = nullptr;
  ReParams params_{};
  std::vector<Resource> resources_;
  std::vector<uint8_t> bytes_;
};
} // namespace
std::unique_ptr<ReEngine> ReCreateNvenc(ID3D11Device* device, const ReParams& p) {
  auto engine = std::make_unique<Nvenc>();
  engine->Start(device, p);
  return engine;
}

