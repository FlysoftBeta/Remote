// Copyright 2026 The remotekit Authors
// Use of this source code is governed by a BSD-style license.
#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#pragma allow_unsafe_libc_calls
#endif

#include "prts_endpoint/d3d_native_frame.h"

#include <d3d11_1.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <thread>
#include <utility>
#include <vector>

#include "third_party/webrtc/api/make_ref_counted.h"
#include "third_party/webrtc/rtc_base/logging.h"

namespace prts {
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
    healthy_ = SUCCEEDED(device_.As(&video_)) &&
               SUCCEEDED(context_.As(&video_context_));
  }

  struct Slot {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11VideoProcessorOutputView> view;
  };

  bool Fail(const char* operation, HRESULT hr) {
    RTC_LOG(LS_ERROR) << "PRTS native VPP " << operation << " hr=" << hr
                      << "; use I420 bridge on subsequent frames";
    healthy_ = false;
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
    done_.Reset();
    width_ = width;
    height_ = height;
    source_width_ = source.Width;
    source_height_ = source.Height;
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
    D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0};
    if (FAILED(hr = device_->CreateQuery(&query, &done_)))
      return Fail("CreateQuery", hr);

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
    slots_.push_back(slot);
    return slot;
  }

  webrtc::scoped_refptr<webrtc::VideoFrameBuffer> Convert(
      ID3D11Texture2D* texture,
      IDXGIKeyedMutex* mutex,
      const RECT& visible,
      int limit) {
    if (!healthy_ || !texture || !mutex)
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
      Fail("source descriptor", E_INVALIDARG);
      return nullptr;
    }
    if (!Configure(desc, width, height))
      return nullptr;
    auto slot = GetSlot(limit);
    if (!slot)
      return nullptr;  // Drop raw input, never an encoded reference picture.
    auto it = inputs_.find(texture);
    if (it == inputs_.end()) {
      if (inputs_.size() >= 64)
        ClearImports();
      ComPtr<ID3D11VideoProcessorInputView> view;
      D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input{};
      input.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
      HRESULT hr = video_->CreateVideoProcessorInputView(
          texture, enumerator_.Get(), &input, &view);
      if (FAILED(hr)) {
        Fail("shared BGRA input view", hr);
        return nullptr;
      }
      it = inputs_.emplace(texture, std::move(view)).first;
    }
    // Allocate resources/views before acquiring the cross-process mutex.
    auto started = std::chrono::steady_clock::now();
    HRESULT hr = mutex->AcquireSync(0, 20);
    if (hr != S_OK) {  // WAIT_TIMEOUT/WAIT_ABANDONED are nonnegative.
      if (hr != WAIT_TIMEOUT)
        Fail("AcquireSync", hr);
      return nullptr;
    }
    auto acquired = std::chrono::steady_clock::now();
    video_context_->VideoProcessorSetStreamSourceRect(processor_.Get(), 0, TRUE,
                                                      &visible);
    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = it->second.Get();
    hr = video_context_->VideoProcessorBlt(processor_.Get(), slot->view.Get(),
                                           0, 1, &stream);
    if (SUCCEEDED(hr)) {
      // Conservative draft synchronization: wait for ONLY the source read/CSC
      // before releasing key 0. No MFX wait occurs with this mutex held.
      context_->End(done_.Get());
      context_->Flush();
      const auto deadline = acquired + std::chrono::milliseconds(20);
      BOOL complete = FALSE;
      for (;;) {
        hr = context_->GetData(done_.Get(), &complete, sizeof(complete),
                               D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (FAILED(hr) || (hr == S_OK && complete))
          break;
        if (std::chrono::steady_clock::now() >= deadline) {
          hr = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
    HRESULT released = mutex->ReleaseSync(0);
    if (FAILED(hr) || released != S_OK) {
      Fail("Blt/completion/ReleaseSync", FAILED(hr) ? hr : released);
      return nullptr;
    }
    if (++frames_ % 120 == 0) {
      auto now = std::chrono::steady_clock::now();
      RTC_LOG(LS_INFO) << "PRTS native VPP frames=" << frames_ << " acquire_us="
                       << std::chrono::duration_cast<std::chrono::microseconds>(
                              acquired - started)
                              .count()
                       << " mutex_hold_us="
                       << std::chrono::duration_cast<std::chrono::microseconds>(
                              now - acquired)
                              .count()
                       << " pool=" << slots_.size() << " limit=" << limit;
    }
    return webrtc::make_ref_counted<VplNativeFrameBuffer>(slot->texture, width,
                                                          height, slot);
  }

  void ClearImports() { inputs_.clear(); }
  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<ID3D11VideoDevice> video_;
  ComPtr<ID3D11VideoContext1> video_context_;
  ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
  ComPtr<ID3D11VideoProcessor> processor_;
  ComPtr<ID3D11Query> done_;
  std::map<ID3D11Texture2D*, ComPtr<ID3D11VideoProcessorInputView>> inputs_;
  std::vector<std::shared_ptr<Slot>> slots_;
  int fps_;
  int width_ = 0, height_ = 0;
  UINT source_width_ = 0, source_height_ = 0;
  bool healthy_ = false;
  uint64_t frames_ = 0, pool_drops_ = 0;
};

D3DNativeFramePool::D3DNativeFramePool(ComPtr<ID3D11Device> device, int fps)
    : impl_(std::make_unique<Impl>(std::move(device), fps)) {}
D3DNativeFramePool::~D3DNativeFramePool() = default;
webrtc::scoped_refptr<webrtc::VideoFrameBuffer> D3DNativeFramePool::Convert(
    ID3D11Texture2D* texture,
    IDXGIKeyedMutex* mutex,
    const RECT& visible,
    int limit) {
  return impl_->Convert(texture, mutex, visible, limit);
}
bool D3DNativeFramePool::healthy() const {
  return impl_->healthy_;
}
void D3DNativeFramePool::ClearImports() {
  impl_->ClearImports();
}
}  // namespace prts
