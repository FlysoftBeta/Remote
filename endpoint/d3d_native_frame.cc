// Copyright 2026 The remotekit Authors
// Use of this source code is governed by a BSD-style license.
#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#pragma allow_unsafe_libc_calls
#endif

#include "rb_legacy_endpoint/d3d_native_frame.h"

#include <d3d11_1.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "rb_legacy_endpoint/latency_stats.h"
#include "third_party/webrtc/api/make_ref_counted.h"
#include "third_party/webrtc_overrides/rtc_base/logging.h"

namespace re {
using Microsoft::WRL::ComPtr;
namespace {
constexpr DXGI_COLOR_SPACE_TYPE kInputColor =
    DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
constexpr DXGI_COLOR_SPACE_TYPE kOutputColor =
    DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
int Align16(int value) {
  return (value + 15) & ~15;
}
}  // namespace

class D3DNativeFramePool::Impl {
 public:
  Impl(ComPtr<ID3D11Device> device, int fps)
      : device_(std::move(device)), fps_(fps) {
    device_->GetImmediateContext(&context_);
    *healthy_ = SUCCEEDED(device_.As(&video_)) &&
               SUCCEEDED(context_.As(&video_context_));
  }

  struct Slot {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11Texture2D> bgra;
    ComPtr<ID3D11VideoProcessorInputView> input;
    ComPtr<ID3D11Query> done;
    ComPtr<ID3D11VideoProcessorOutputView> view;
  };

  bool Fail(const char* operation, HRESULT hr) {
    RTC_LOG(LS_ERROR) << "PRTS native VPP " << operation << " hr=" << hr
                      << "; use I420 bridge on subsequent frames";
    *healthy_ = false;
    return false;
  }

  bool Configure(const D3D11_TEXTURE2D_DESC& source, int width, int height) {
    if (enumerator_ && source.Width == source_width_ &&
        source.Height == source_height_ && width == width_ && height == height_)
      return true;
    ClearImports();
    slots_.clear();  // Outstanding buffers retain the old slots/resources.
    processor_.Reset();
    enumerator_.Reset();
    width_ = width;
    height_ = height;
    source_width_ = source.Width;
    source_height_ = source.Height;
    source_desc_ = source;
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputFrameRate = {static_cast<UINT>(fps_), 1};
    content.OutputFrameRate = content.InputFrameRate;
    content.InputWidth = source.Width;
    content.InputHeight = source.Height;
    content.OutputWidth = Align16(width);
    content.OutputHeight = Align16(height);
    content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    HRESULT hr = video_->CreateVideoProcessorEnumerator(&content, &enumerator_);
    if (FAILED(hr))
      return Fail("CreateVideoProcessorEnumerator", hr);
    ComPtr<ID3D11VideoProcessorEnumerator1> formats;
    BOOL supported = FALSE;
    if (FAILED(hr = enumerator_.As(&formats)) ||
        FAILED(hr = formats->CheckVideoProcessorFormatConversion(
                   source.Format, kInputColor, DXGI_FORMAT_NV12, kOutputColor,
                   &supported)) ||
        !supported) {
      return Fail("BT709 BGRA->NV12 format conversion", hr);
    }
    if (FAILED(hr = video_->CreateVideoProcessor(enumerator_.Get(), 0,
                                                 &processor_)))
      return Fail("CreateVideoProcessor", hr);
    RECT target{0, 0, Align16(width), Align16(height)};
    RECT destination{0, 0, width, height};
    video_context_->VideoProcessorSetOutputTargetRect(processor_.Get(), TRUE,
                                                      &target);
    video_context_->VideoProcessorSetStreamDestRect(processor_.Get(), 0, TRUE,
                                                    &destination);
    video_context_->VideoProcessorSetStreamFrameFormat(
        processor_.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    video_context_->VideoProcessorSetStreamAutoProcessingMode(processor_.Get(),
                                                              0, FALSE);
    video_context_->VideoProcessorSetStreamColorSpace1(processor_.Get(), 0,
                                                       kInputColor);
    video_context_->VideoProcessorSetOutputColorSpace1(processor_.Get(),
                                                       kOutputColor);
    // Define all coded padding outside the visible destination as limited
    // black.
    D3D11_VIDEO_COLOR black{};
    black.YCbCr = {16.0f / 255, 128.0f / 255, 128.0f / 255, 1.0f};
    video_context_->VideoProcessorSetOutputBackgroundColor(processor_.Get(),
                                                           TRUE, &black);
    return true;
  }

  std::shared_ptr<Slot> GetSlot(int limit) {
    for (const auto& slot : slots_) {
      // Pool is the sole owner only after WebRTC and MFX have released it.
      if (slot.use_count() == 1)
        return slot;
    }
    if (slots_.size() >= static_cast<size_t>(std::clamp(limit, 4, 35))) {
      if (++pool_drops_ % 30 == 1)
        RTC_LOG(LS_WARNING)
            << "PRTS native pool pressure drops=" << pool_drops_;
      return nullptr;
    }
    auto slot = std::make_shared<Slot>();
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = Align16(width_);
    desc.Height = Align16(height_);
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &slot->texture);
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC view{};
    view.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    if (FAILED(hr) || FAILED(hr = video_->CreateVideoProcessorOutputView(
                                 slot->texture.Get(), enumerator_.Get(), &view,
                                 &slot->view))) {
      Fail("NV12 output slot", hr);
      return nullptr;
    }
    desc = source_desc_;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = desc.MiscFlags = 0;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input{};
    input.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0};
    if (FAILED(hr = device_->CreateTexture2D(&desc, nullptr, &slot->bgra)) ||
        FAILED(
            hr = video_->CreateVideoProcessorInputView(
                slot->bgra.Get(), enumerator_.Get(), &input, &slot->input)) ||
        FAILED(hr = device_->CreateQuery(&query, &slot->done))) {
      Fail("private BGRA slot", hr);
      return nullptr;
    }
    slots_.push_back(slot);
    return slot;
  }

  webrtc::scoped_refptr<webrtc::VideoFrameBuffer> Convert(
      ID3D11Texture2D* texture,
      IDXGIKeyedMutex* mutex,
      const RECT& visible,
      int limit,
      bool* discard_source,
      ID3D11Query* source_read_done,
      bool* source_read_submitted) {
    *discard_source = false;
    if (!healthy_->load() || !texture || (!mutex && !source_read_done))
      return nullptr;
    ComPtr<ID3D11Device> owner;
    texture->GetDevice(&owner);
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    const int width = visible.right - visible.left;
    const int height = visible.bottom - visible.top;
    if (owner.Get() != device_.Get() ||
        desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM || visible.left < 0 ||
        visible.top < 0 || width <= 0 || height <= 0 || (width & 1) ||
        (height & 1) || visible.right > static_cast<LONG>(desc.Width) ||
        visible.bottom > static_cast<LONG>(desc.Height)) {
      *discard_source = true;
      return nullptr;
    }
    if (!Configure(desc, width, height))
      return nullptr;
    auto slot = GetSlot(limit);
    if (!slot)
      return nullptr;  // Drop raw input, never an encoded reference picture.
    // All resources/views exist before acquiring the cross-process mutex.
    const auto started = base::TimeTicks::Now();
    HRESULT hr = mutex ? mutex->AcquireSync(0, 20) : S_OK;
    if (hr != S_OK) {
      if (hr != WAIT_TIMEOUT) {
        *discard_source = true;
        LOG(WARNING) << "PRTS native AcquireSync hr=" << hr
                     << "; discard shared buffer";
      }
      return nullptr;
    }
    const auto acquired = base::TimeTicks::Now();
    context_->CopyResource(slot->bgra.Get(), texture);
    // RB: incoming GPU Wait was queued on this same context by the source.
    if (source_read_done) {
      context_->End(source_read_done);
      *source_read_submitted = true;
    }
    const HRESULT released = mutex ? mutex->ReleaseSync(0) : S_OK;
    const auto copied = base::TimeTicks::Now();
    auto& latency = LatencyStats::Get();
    latency.Add("native_mutex_acquire", started, acquired);
    latency.Add("native_mutex_hold", acquired, copied);
    if (released != S_OK) {
      *discard_source = true;
      LOG(WARNING) << "PRTS native ReleaseSync hr=" << released
                   << "; discard shared buffer";
      return nullptr;
    }
    // Same immediate context/device orders private-copy -> VPP -> encode.
    // ReleaseSync orders the shared source read before another process writes.
    video_context_->VideoProcessorSetStreamSourceRect(processor_.Get(), 0, TRUE,
                                                      &visible);
    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = slot->input.Get();
    hr = video_context_->VideoProcessorBlt(processor_.Get(), slot->view.Get(),
                                           0, 1, &stream);
    context_->End(slot->done.Get());
    // Submit the VP command buffer before the runtime consumes its output.
    // Flush submits work and does not wait for GPU completion.
    context_->Flush();
    const auto submitted = base::TimeTicks::Now();
    latency.Add("vpp_submit", copied, submitted);
    if (FAILED(hr)) {
      Fail("VideoProcessorBlt", hr);
      return nullptr;
    }
    if (++frames_ % 120 == 0) {
      LOG(INFO) << "PRTS native VPP frames=" << frames_
                << " acquire_us=" << (acquired - started).InMicroseconds()
                << " mutex_hold_us=" << (copied - acquired).InMicroseconds()
                << " submit_us=" << (submitted - copied).InMicroseconds()
                << " pool=" << slots_.size() << " limit=" << limit;
    }
    return webrtc::make_ref_counted<VplNativeFrameBuffer>(
        slot->texture, width, height, slot, slot->done, copied, healthy_);
  }

  // Shared textures are never retained; leased private slots survive resets.
  void ClearImports() {}
  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<ID3D11VideoDevice> video_;
  ComPtr<ID3D11VideoContext1> video_context_;
  ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
  ComPtr<ID3D11VideoProcessor> processor_;
  D3D11_TEXTURE2D_DESC source_desc_{};
  std::vector<std::shared_ptr<Slot>> slots_;
  int fps_;
  int width_ = 0, height_ = 0;
  UINT source_width_ = 0, source_height_ = 0;
  std::shared_ptr<std::atomic<bool>> healthy_ =
      std::make_shared<std::atomic<bool>>(false);
  uint64_t frames_ = 0, pool_drops_ = 0;
};

D3DNativeFramePool::D3DNativeFramePool(ComPtr<ID3D11Device> device, int fps)
    : impl_(std::make_unique<Impl>(std::move(device), fps)) {}
D3DNativeFramePool::~D3DNativeFramePool() = default;
webrtc::scoped_refptr<webrtc::VideoFrameBuffer> D3DNativeFramePool::Convert(
    ID3D11Texture2D* texture,
    IDXGIKeyedMutex* mutex,
    const RECT& visible,
    int limit,
    bool* discard_source,
    ID3D11Query* source_read_done,
    bool* source_read_submitted) {
  return impl_->Convert(texture, mutex, visible, limit, discard_source,
                        source_read_done, source_read_submitted);
}
bool D3DNativeFramePool::healthy() const {
  return impl_->healthy_->load();
}
void D3DNativeFramePool::ClearImports() {
  impl_->ClearImports();
}
}  // namespace re
