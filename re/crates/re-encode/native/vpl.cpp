// Copyright 2026 The remotekit Authors. BSD-3-Clause.
// Allocator and readback policy ported from endpoint/vpl_encoder.cc.
#include "bridge.h"
#include <vpl/mfxdispatcher.h>
#include <vpl/mfxvideo.h>
#include <dxgi1_2.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <thread>
namespace {
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
constexpr size_t kMaxBytes = 16 * 1024 * 1024;
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


int64_t Effective(const mfxInfoMFX& m, mfxU16 value) {
  return int64_t(value) * std::max<mfxU16>(1, m.BRCParamMultiplier);
}
bool Rounded(int64_t requested, int64_t actual, int64_t quantum) {
  if (requested == actual) return true;
  return requested && actual && std::abs(actual-requested) <= std::max(quantum, requested/10);
}
bool Compatible(const Parameters& request, const Parameters& actual, bool minimum) {
  const auto& r = request.p.mfx;
  const auto& a = actual.p.mfx;
  return actual.p.AsyncDepth == 1 && actual.p.IOPattern == MFX_IOPATTERN_IN_VIDEO_MEMORY &&
    a.CodecId == MFX_CODEC_AVC && a.CodecProfile == r.CodecProfile && a.CodecLevel == r.CodecLevel &&
    a.LowPower == r.LowPower && a.TargetUsage == r.TargetUsage && a.GopPicSize == r.GopPicSize &&
    a.IdrInterval == 0 && a.RateControlMethod == r.RateControlMethod &&
    Rounded(Effective(r,r.TargetKbps), Effective(a,a.TargetKbps), 16) &&
    Rounded(Effective(r,r.MaxKbps), Effective(a,a.MaxKbps), 16) &&
    Rounded(Effective(r,r.BufferSizeInKB), Effective(a,a.BufferSizeInKB), minimum ? 64 : 2) &&
    Rounded(Effective(r,r.InitialDelayInKB), Effective(a,a.InitialDelayInKB), minimum ? 32 : 2) &&
    a.GopRefDist == 1 && a.NumRefFrame == 1 && a.NumSlice == r.NumSlice &&
    a.FrameInfo.FourCC == MFX_FOURCC_NV12 && a.FrameInfo.Width == r.FrameInfo.Width &&
    a.FrameInfo.Height == r.FrameInfo.Height && a.FrameInfo.CropW == r.FrameInfo.CropW &&
    a.FrameInfo.CropH == r.FrameInfo.CropH && a.FrameInfo.CropX == 0 && a.FrameInfo.CropY == 0 &&
    a.FrameInfo.ChromaFormat == MFX_CHROMAFORMAT_YUV420 && a.FrameInfo.PicStruct == MFX_PICSTRUCT_PROGRESSIVE &&
    a.FrameInfo.FrameRateExtN == r.FrameInfo.FrameRateExtN && a.FrameInfo.FrameRateExtD == 1 &&
    (r.RateControlMethod != MFX_RATECONTROL_VBR || actual.co3.LowDelayBRC == request.co3.LowDelayBRC) &&
    actual.co2.AdaptiveI == MFX_CODINGOPTION_OFF && actual.co2.AdaptiveB == MFX_CODINGOPTION_OFF &&
    actual.co2.MBBRC == MFX_CODINGOPTION_OFF && actual.co2.IntRefType == request.co2.IntRefType &&
    (!request.co2.IntRefType || actual.co2.IntRefCycleSize == request.co2.IntRefCycleSize) &&
    (r.RateControlMethod != MFX_RATECONTROL_VBR || Rounded(request.co2.MaxFrameSize, actual.co2.MaxFrameSize, 1024)) &&
    actual.vui.ColourDescriptionPresent == 1 && actual.vui.ColourPrimaries == 1 &&
    actual.vui.TransferCharacteristics == 1 && actual.vui.MatrixCoefficients == 1 && actual.vui.VideoFullRange == 0;
}
Parameters MakeParameters(const ReParams& p) {
  Parameters q;
  q.p.AsyncDepth = 1;
  q.p.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;
  auto& m = q.p.mfx;
  m.CodecId = MFX_CODEC_AVC;
  m.CodecProfile = static_cast<mfxU16>(p.profile);
  m.CodecLevel = static_cast<mfxU16>(p.level);
  m.LowPower = p.low_power ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;
  m.TargetUsage = static_cast<mfxU16>(p.tu);
  m.RateControlMethod = p.cbr ? MFX_RATECONTROL_CBR : MFX_RATECONTROL_VBR;
  m.GopPicSize = static_cast<mfxU16>(p.gop ? p.gop : 65535);
  m.GopRefDist = m.NumRefFrame = 1;
  m.NumSlice = static_cast<mfxU16>(p.slices);
  m.BRCParamMultiplier = 1;
  m.TargetKbps = static_cast<mfxU16>(p.target_kbps);
  m.MaxKbps = static_cast<mfxU16>(p.cbr ? p.target_kbps : p.max_kbps);
  m.BufferSizeInKB = static_cast<mfxU16>(p.vbv_kb);
  m.InitialDelayInKB = m.BufferSizeInKB/2;
  m.FrameInfo.FourCC = MFX_FOURCC_NV12;
  m.FrameInfo.Width = static_cast<mfxU16>((p.width+15)&~15);
  m.FrameInfo.Height = static_cast<mfxU16>((p.height+15)&~15);
  m.FrameInfo.CropW = static_cast<mfxU16>(p.width);
  m.FrameInfo.CropH = static_cast<mfxU16>(p.height);
  m.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
  m.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
  m.FrameInfo.FrameRateExtN = p.fps;
  m.FrameInfo.FrameRateExtD = 1;
  q.co2.AdaptiveI = q.co2.AdaptiveB = q.co2.MBBRC = MFX_CODINGOPTION_OFF;
  q.co2.MaxFrameSize = p.cbr ? 0 : p.cap_bytes;
  if (p.refresh) {
    q.co2.IntRefType = MFX_REFRESH_VERTICAL;
    q.co2.IntRefCycleSize = static_cast<mfxU16>(p.refresh);
  }
  q.co3.LowDelayBRC = p.cbr ? MFX_CODINGOPTION_OFF : MFX_CODINGOPTION_ON;
  q.vui.VideoFormat = 5;
  q.vui.ColourDescriptionPresent = 1;
  q.vui.ColourPrimaries = q.vui.TransferCharacteristics = q.vui.MatrixCoefficients = 1;
  return q;
}
std::string Describe(const Parameters& p) {
  const auto& m = p.p.mfx;
  std::ostringstream s;
  s << "target/max_kbps=" << Effective(m,m.TargetKbps) << '/' << Effective(m,m.MaxKbps)
    << " fps=" << m.FrameInfo.FrameRateExtN << " vbv/initial_kb=" << Effective(m,m.BufferSizeInKB)
    << '/' << Effective(m,m.InitialDelayInKB) << " cap_bytes=" << p.co2.MaxFrameSize
    << " low_delay_brc=" << p.co3.LowDelayBRC << " low_power=" << m.LowPower
    << " tu=" << m.TargetUsage << " profile/level=" << m.CodecProfile << '/' << m.CodecLevel
    << " slices=" << m.NumSlice << " gop=" << m.GopPicSize << " refresh=" << p.co2.IntRefCycleSize;
  return s.str();
}
class Vpl final : public ReEngine {
 public:
  ~Vpl() override {
    // Close the runtime before destroying any external surfaces or producer leases.
    if (initialized_) MFXVideoENCODE_Close(session_);
    if (session_) MFXClose(session_);
    slots_.clear();
    allocator_.reset();
    if (loader_) MFXUnload(loader_);
  }
  bool Filter(const char* key, mfxU32 value) {
    auto config = MFXCreateConfig(loader_);
    mfxVariant v{}; v.Type = MFX_VARIANT_TYPE_U32; v.Data.U32 = value;
    return config && MFXSetConfigFilterProperty(config, reinterpret_cast<const mfxU8*>(key), v) == 0;
  }
  void Start(ID3D11Device* device, const ReParams& p) {
    device_ = device;
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(device_.As(&dxgi)) || FAILED(dxgi->GetAdapter(&adapter)) ||
        FAILED(adapter->GetDesc(&desc)) || desc.VendorId != 0x8086)
      throw std::runtime_error("oneVPL requires an Intel D3D11 adapter");
    loader_ = MFXLoad();
    if (!loader_ || !Filter("mfxImplDescription.Impl", MFX_IMPL_TYPE_HARDWARE) ||
        !Filter("mfxImplDescription.mfxEncoderDescription.encoder.CodecID", MFX_CODEC_AVC) ||
        !Filter("mfxImplDescription.AccelerationMode", MFX_ACCEL_MODE_VIA_D3D11))
      throw std::runtime_error("oneVPL dispatcher could not load a hardware AVC implementation");
    for (mfxU32 i = 0;; ++i) {
      mfxHDL implementation = nullptr;
      auto status = MFXEnumImplementations(loader_, i, MFX_IMPLCAPS_IMPLDESCSTRUCTURE, &implementation);
      if (status == MFX_ERR_NOT_FOUND) break;
      if (status != 0 || !implementation) throw std::runtime_error("oneVPL implementation enumeration failed");
      MFXDispReleaseImplDescription(loader_, implementation);
      mfxHDL extended = nullptr;
      bool matches = false;
      if (MFXEnumImplementations(loader_, i, MFX_IMPLCAPS_DEVICE_ID_EXTENDED, &extended) == 0 && extended) {
        auto* id = static_cast<mfxExtendedDeviceId*>(extended);
        matches = id->LUIDValid && std::memcmp(id->DeviceLUID, &desc.AdapterLuid, sizeof(LUID)) == 0;
        MFXDispReleaseImplDescription(loader_, extended);
      }
      if (matches && MFXCreateSession(loader_, i, &session_) == 0) break;
    }
    if (!session_) throw std::runtime_error("oneVPL: no hardware AVC runtime matching the D3D11 adapter LUID");
    allocator_ = std::make_unique<TextureAllocator>(device);
    Check(MFXVideoCORE_SetHandle(session_, MFX_HANDLE_D3D11_DEVICE, device), "SetHandle");
    Check(MFXVideoCORE_SetFrameAllocator(session_, &allocator_->api), "SetFrameAllocator");
    // Match the endpoint: size BRC resources at the ceiling, then decrease seamlessly.
    ReParams initial = p; initial.target_kbps = p.max_kbps;
    Parameters wanted = MakeParameters(initial), queried(wanted);
    auto status = MFXVideoENCODE_Query(session_, &wanted.p, &queried.p);
    if (status < 0 || !Compatible(wanted, queried, true))
      throw std::runtime_error("oneVPL Query incompatible: requested " + Describe(wanted) + "; actual " + Describe(queried));
    Check(MFXVideoENCODE_Init(session_, &queried.p), "Init");
    initialized_ = true;
    Parameters actual;
    Check(MFXVideoENCODE_GetVideoParam(session_, &actual.p), "GetVideoParam");
    if (!Compatible(wanted, actual, true))
      throw std::runtime_error("oneVPL readback incompatible: requested " + Describe(wanted) + "; actual " + Describe(actual));
    parameters_ = actual;
    diagnostics = "requested " + Describe(wanted) + "; effective " + Describe(actual);
    mfxFrameAllocRequest request{};
    Check(MFXVideoENCODE_QueryIOSurf(session_, &parameters_.p, &request), "QueryIOSurf");
    request.NumFrameSuggested = std::max<mfxU16>(4, request.NumFrameSuggested);
    if (request.NumFrameSuggested > 32) throw std::runtime_error("oneVPL requested >32 surfaces");
    for (mfxU16 i = 0; i < request.NumFrameSuggested; ++i) {
      auto slot = std::make_unique<Slot>();
      slot->surface.Info = parameters_.p.mfx.FrameInfo;
      slots_.push_back(std::move(slot));
    }
    storage_.resize(1024*1024);
    config_ = p;
    if (p.target_kbps != p.max_kbps && Reset(p) != 0)
      throw std::runtime_error("oneVPL could not apply initial target after maximum-rate initialization: " + error);
  }
  int Reset(const ReParams& p) override {
    Parameters wanted(parameters_);
    const auto multiplier = std::max<mfxU16>(1, wanted.p.mfx.BRCParamMultiplier);
    wanted.p.mfx.TargetKbps = static_cast<mfxU16>((p.target_kbps+multiplier-1)/multiplier);
    wanted.p.mfx.MaxKbps = static_cast<mfxU16>(((p.cbr ? p.target_kbps : p.max_kbps)+multiplier-1)/multiplier);
    wanted.p.mfx.FrameInfo.FrameRateExtN = p.fps;
    // VBV and frame cap remain at their effective Init values on rate-only resets.
    const Parameters expected(wanted);
    mfxExtEncoderResetOption reset{};
    reset.Header = {MFX_EXTBUFF_ENCODER_RESET_OPTION, sizeof(reset)};
    reset.StartNewSequence = MFX_CODINGOPTION_OFF;
    mfxExtBuffer* extensions[] = {wanted.extensions[0],wanted.extensions[1],wanted.extensions[2],reinterpret_cast<mfxExtBuffer*>(&reset)};
    wanted.p.ExtParam = extensions; wanted.p.NumExtParam = 4;
    auto status = MFXVideoENCODE_Reset(session_, &wanted.p);
    if (status < 0) {
      // Legacy runtimes may reject ResetOption while accepting a plain rate reset.
      wanted = expected;
      status = MFXVideoENCODE_Reset(session_, &wanted.p);
    }
    if (status < 0) { error = "oneVPL seamless reset rejected; previous rates retained, status="+std::to_string(status); return 1; }
    Parameters actual;
    const auto read = MFXVideoENCODE_GetVideoParam(session_, &actual.p);
    if (read != 0 || !Compatible(expected, actual, false))
      return Fail("oneVPL reset readback incompatible: requested " + Describe(expected) + "; actual " + Describe(actual), read);
    parameters_ = actual;
    config_ = p;
    diagnostics = "effective " + Describe(actual);
    check_reset_idr_ = true;
    return 0;
  }
  int Encode(ID3D11Texture2D* texture, ReLease lease, uint64_t timestamp, bool idr, ReAu& output) override {
    Slot* slot = nullptr;
    for (auto& candidate : slots_) {
      if (!candidate->surface.Data.Locked) {
        candidate->lease.Clear(); candidate->memory.texture.Reset();
        if (!slot) slot = candidate.get();
      }
    }
    if (!slot) return Fail("oneVPL surface pool exhausted");
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    if (desc.Format != DXGI_FORMAT_NV12 || desc.Width != parameters_.p.mfx.FrameInfo.Width ||
        desc.Height != parameters_.p.mfx.FrameInfo.Height || desc.ArraySize != 1 || desc.MipLevels != 1 ||
        desc.SampleDesc.Count != 1 || desc.Usage != D3D11_USAGE_DEFAULT || !(desc.BindFlags&D3D11_BIND_RENDER_TARGET))
      return Fail("oneVPL requires an aligned, single-subresource NV12 render-target texture");
    slot->memory.texture = texture;
    slot->lease = std::move(lease);
    slot->surface.Info = parameters_.p.mfx.FrameInfo;
    slot->surface.Data.MemId = &slot->memory;
    slot->surface.Data.TimeStamp = timestamp;
    slot->surface.Data.FrameOrder = order_++;
    mfxEncodeCtrl control{};
    if (idr) control.FrameType = MFX_FRAMETYPE_I | MFX_FRAMETYPE_IDR | MFX_FRAMETYPE_REF;
    mfxBitstream bits{};
    mfxSyncPoint sync = nullptr;
    const auto started = Clock::now();
    mfxStatus status;
    for (;;) {
      bits.Data = storage_.data(); bits.MaxLength = static_cast<mfxU32>(storage_.size());
      status = MFXVideoENCODE_EncodeFrameAsync(session_, &control, &slot->surface, &bits, &sync);
      if (status == MFX_ERR_NOT_ENOUGH_BUFFER && storage_.size() < kMaxBytes) {
        storage_.resize(std::min(kMaxBytes, storage_.size()*2)); continue;
      }
      if (status != MFX_WRN_DEVICE_BUSY) break;
      if (Clock::now()-started > std::chrono::milliseconds(100)) return Fail("oneVPL device busy timeout", status);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (status < 0 || !sync) return Fail("oneVPL EncodeFrameAsync did not produce a synchronous access unit", status);
    status = MFXVideoCORE_SyncOperation(session_, sync, 100);
    if (status != 0) return Fail("oneVPL SyncOperation", status);
    if (!bits.DataLength || bits.TimeStamp != timestamp || bits.DataOffset > bits.MaxLength ||
        bits.DataLength > bits.MaxLength-bits.DataOffset || (bits.FrameType&MFX_FRAMETYPE_B))
      return Fail("oneVPL invalid, reordered or empty access unit");
    if (check_reset_idr_ && !idr && (bits.FrameType&MFX_FRAMETYPE_IDR))
      diagnostics += "; driver inserted IDR after reset";
    check_reset_idr_ = false;
    output = {bits.Data+bits.DataOffset, bits.DataLength, timestamp,
      (bits.FrameType&MFX_FRAMETYPE_IDR) ? 0u : (bits.FrameType&MFX_FRAMETYPE_I) ? 1u : 2u};
    return 0;
  }
 private:
  void Check(mfxStatus status, const char* what) {
    if (status < 0) throw std::runtime_error(std::string("oneVPL ")+what+" failed, status="+std::to_string(status));
  }
  struct Slot {
    mfxFrameSurface1 surface{};
    TextureMemory memory;
    ReLease lease;
  };
  ComPtr<ID3D11Device> device_;
  mfxLoader loader_ = nullptr;
  mfxSession session_ = nullptr;
  bool initialized_ = false, check_reset_idr_ = false;
  std::unique_ptr<TextureAllocator> allocator_;
  std::vector<std::unique_ptr<Slot>> slots_;
  Parameters parameters_;
  ReParams config_{};
  std::vector<uint8_t> storage_;
  mfxU32 order_ = 0;
};
} // namespace
std::unique_ptr<ReEngine> ReCreateVpl(ID3D11Device* device, const ReParams& p) {
  auto engine = std::make_unique<Vpl>();
  engine->Start(device, p);
  return engine;
}
