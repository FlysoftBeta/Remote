// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#pragma allow_unsafe_libc_calls
#endif

#include "prts_endpoint/d3d_frame_source.h"
#include "prts_endpoint/latency_stats.h"

#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <limits>
#include <map>
#include <utility>

#include "base/check.h"
#include "base/compiler_specific.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/platform/platform_handle.h"
#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/webrtc/api/video/i420_buffer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace prts {
LatencyStats& LatencyStats::Get() {
  static base::NoDestructor<LatencyStats> stats;
  return *stats;
}

using Microsoft::WRL::ComPtr;
namespace {
class Lease final : public FrameSource {
 public:
  explicit Lease(std::shared_ptr<D3DFrameSource> source)
      : source_(std::move(source)) {}
  ~Lease() override { Stop(); }
  void Start(Consumer consumer) override {
    source_->Start(std::move(consumer));
  }
  void Stop() override { source_->Stop(); }
  FrameGeometry geometry() const override { return source_->geometry(); }
  void SetInputSequence(uint16_t seq) override {
    source_->SetInputSequence(seq);
  }

 private:
  std::shared_ptr<D3DFrameSource> source_;
};
}  // namespace

class D3DFrameSource::Impl final : public mojom::PrtsFrameSink {
 public:
  Impl() : import_("PrtsD3DImport"), convert_("PrtsI420") {
    CHECK(base::TimeTicks::IsConsistentAcrossProcesses());
    CHECK(import_.Start());
    CHECK(convert_.Start());
    import_runner_ = import_.task_runner();
    convert_runner_ = convert_.task_runner();
    convert_runner_->PostDelayedTask(
        FROM_HERE, base::BindOnce(&Impl::LogStats, base::Unretained(this)),
        base::Seconds(1));
  }
  ~Impl() override {
    Stop();
    base::WaitableEvent done;
    import_runner_->PostTask(
        FROM_HERE, base::BindOnce(
                       [](Impl* self, base::WaitableEvent* done) {
                         self->receiver_.reset();
                         self->Reset();
                         self->context_.Reset();
                         self->device_.Reset();
                         done->Signal();
                       },
                       base::Unretained(this), base::Unretained(&done)));
    done.Wait();
    import_.Stop();
    convert_.Stop();
  }
  mojo::PendingRemote<mojom::PrtsFrameSink> Bind(uint64_t luid,
                                                 FrameGeometry g) {
    // Change generation synchronously so an old conversion cannot escape after
    // the main-sequence window switch. Receiver work follows on import_.
    uint64_t epoch;
    {
      base::AutoLock lock(mutex_);
      geometry_ = g;
      epoch = ++epoch_;
      pending_.reset();
    }
    Drain();
    mojo::PendingRemote<mojom::PrtsFrameSink> remote;
    import_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](Impl* self, uint64_t luid, uint64_t epoch,
               mojo::PendingReceiver<mojom::PrtsFrameSink> receiver) {
              self->receiver_.reset();
              self->buffers_.clear();
              self->bound_epoch_ = epoch;
              if (!self->device_ || self->luid_ != luid) {
                self->CreateDevice(luid);
              }
              self->receiver_ =
                  std::make_unique<mojo::Receiver<mojom::PrtsFrameSink>>(
                      self, std::move(receiver));
            },
            base::Unretained(this), luid, epoch,
            remote.InitWithNewPipeAndPassReceiver()));
    return remote;
  }
  void Clear() {
    {
      base::AutoLock lock(mutex_);
      ++epoch_;
      pending_.reset();
    }
    Drain();
    import_runner_->PostTask(FROM_HERE, base::BindOnce(
                                            [](Impl* self) {
                                              self->receiver_.reset();
                                              self->buffers_.clear();
                                            },
                                            base::Unretained(this)));
  }
  void Start(Consumer consumer) {
    base::AutoLock lock(mutex_);
    CHECK(!consumer_);
    consumer_ = std::move(consumer);
    ++consumer_epoch_;
    pending_.reset();
  }
  void Stop() {
    {
      base::AutoLock lock(mutex_);
      consumer_ = {};
      pending_.reset();
      ++consumer_epoch_;
    }
    // Consumer delivery happens outside mutex_. This barrier drains any copy
    // already taken by conversion before track/PC teardown on signaling.
    Drain();
  }
  FrameGeometry geometry() const {
    base::AutoLock lock(mutex_);
    return geometry_;
  }
  void NewBuffer(uint32_t id,
                 mojo::PlatformHandle handle,
                 const gfx::Size& size) override {
    buffers_.erase(id);
    if (!device_ || !handle.is_valid() || size.IsEmpty() ||
        size.width() > 8192 || size.height() > 8192 || buffers_.size() >= 64) {
      return;
    }
    Buffer buffer;
    HRESULT hr = device_->OpenSharedResource1(handle.GetHandle().get(),
                                              IID_PPV_ARGS(&buffer.texture));
    if (FAILED(hr) || FAILED(buffer.texture.As(&buffer.mutex))) {
      LOG(ERROR) << "OpenSharedResource1/keyed mutex failed: " << hr;
      return;
    }
    D3D11_TEXTURE2D_DESC desc;
    buffer.texture->GetDesc(&desc);
    if (desc.Width != static_cast<UINT>(size.width()) ||
        desc.Height != static_cast<UINT>(size.height()) ||
        desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM || desc.ArraySize != 1 ||
        desc.MipLevels != 1 || desc.SampleDesc.Count != 1) {
      return;
    }
    buffers_.emplace(id, std::move(buffer));
    // The owning PlatformHandle closes here; the opened COM resource persists.
  }
  void OnFrame(mojom::FrameInfoPtr info, OnFrameCallback reply) override {
    const auto received = base::TimeTicks::Now();
    auto& latency = LatencyStats::Get();
    latency.Add("viz_capture", info->capture_begin, info->capture_end);
    // Reference time is an event/target time, not necessarily capture start.
    latency.Add("reference_to_begin", info->reference_time, info->capture_begin,
                true);
    latency.Add("reference_to_receive", info->reference_time, received, true);
    latency.Add("capture_to_receive", info->capture_end, received);
    static int frames_in = 0;
    if (frames_in++ % 300 == 0) {
      LOG(INFO) << "PRTS OnFrame #" << frames_in;
    }
    auto it = buffers_.find(info->buffer_id);
    FrameGeometry g;
    auto job = std::make_unique<Job>();
    job->received = received;
    {
      base::AutoLock lock(mutex_);
      g = geometry_;
      job->epoch = bound_epoch_;
      job->consumer_epoch = consumer_epoch_;
      if (!consumer_ || job->epoch != epoch_) {
        std::move(reply).Run();
        return;
      }
    }
    const auto& visible = info->visible_rect;
    if (it == buffers_.end() || visible.IsEmpty() ||
        !gfx::Rect(info->coded_size).Contains(visible) ||
        visible.width() != g.width || visible.height() != g.height ||
        info->capture_end.is_null()) {
      static int drops = 0;
      if (drops++ % 100 == 0) {
        LOG(WARNING) << "PRTS drop frame: known=" << (it != buffers_.end())
                     << " visible=" << visible.ToString()
                     << " coded=" << info->coded_size.ToString()
                     << " geometry=" << g.width << "x" << g.height
                     << " drops=" << drops;
      }
      std::move(reply).Run();
      return;
    }
    auto& buffer = it->second;
    D3D11_TEXTURE2D_DESC desc;
    buffer.texture->GetDesc(&desc);
    if (info->coded_size != gfx::Size(static_cast<int>(desc.Width),
                                      static_cast<int>(desc.Height))) {
      std::move(reply).Run();
      return;
    }
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &job->texture))) {
      std::move(reply).Run();
      return;
    }
    // WAIT_TIMEOUT and WAIT_ABANDONED are nonnegative: SUCCEEDED is wrong.
    if (HRESULT hr = buffer.mutex->AcquireSync(0, 20); hr != S_OK) {
      LOG(WARNING) << "PRTS AcquireSync failed: " << hr;
      std::move(reply).Run();
      return;
    }
    context_->CopyResource(job->texture.Get(), buffer.texture.Get());
    job->copied = base::TimeTicks::Now();
    latency.Add("import_copy_submit", received, job->copied);
    HRESULT released = buffer.mutex->ReleaseSync(0);
    std::move(reply).Run();  // Browser can call Done before staging/Map/encode.
    if (released != S_OK) {
      LOG(ERROR) << "ReleaseSync failed: " << released;
      buffers_.erase(it);
      return;
    }
    job->device = device_;
    job->context = context_;
    job->info = std::move(info);
    {
      base::AutoLock lock(mutex_);
      if (job->epoch != epoch_ || job->consumer_epoch != consumer_epoch_ ||
          !consumer_) {
        return;
      }
      job->serial = ++latest_;
      pending_ = std::move(job);  // Replace the older pending private texture.
      if (scheduled_) {
        return;
      }
      scheduled_ = true;
    }
    convert_runner_->PostTask(
        FROM_HERE, base::BindOnce(&Impl::Convert, base::Unretained(this)));
  }
  void Reset() override {
    buffers_.clear();
    base::AutoLock lock(mutex_);
    if (bound_epoch_ != epoch_) {
      return;
    }
    pending_.reset();
    bound_epoch_ = ++epoch_;
  }

 private:
  void Drain() {
    base::WaitableEvent done;
    convert_runner_->PostTask(
        FROM_HERE,
        base::BindOnce([](base::WaitableEvent* done) { done->Signal(); },
                       base::Unretained(&done)));
    done.Wait();
  }
  struct Buffer {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<IDXGIKeyedMutex> mutex;
  };
  struct Job {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11Device1> device;
    ComPtr<ID3D11DeviceContext> context;
    mojom::FrameInfoPtr info;
    base::TimeTicks received, copied;
    uint64_t epoch = 0, consumer_epoch = 0, serial = 0;
  };
  void CreateDevice(uint64_t luid) {
    device_.Reset();
    context_.Reset();
    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIAdapter1> adapter;
    if (luid && SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
      for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> candidate;
        if (factory->EnumAdapters1(index, &candidate) != S_OK) {
          break;
        }
        DXGI_ADAPTER_DESC1 desc;
        if (FAILED(candidate->GetDesc1(&desc))) {
          continue;
        }
        uint64_t id =
            (uint64_t{static_cast<uint32_t>(desc.AdapterLuid.HighPart)} << 32) |
            desc.AdapterLuid.LowPart;
        if (id == luid && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
          adapter = candidate;
          break;
        }
      }
    }
    ComPtr<ID3D11Device> device;
    const UINT flags =
        D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    auto create = [&] {
      return D3D11CreateDevice(
          adapter.Get(),
          adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
          flags, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context_);
    };
    HRESULT hr = create();
    if (FAILED(hr) && adapter) {
      adapter.Reset();
      device.Reset();
      context_.Reset();
      hr = create();
    }
    ComPtr<ID3D11Multithread> multithread;
    if (FAILED(hr) || FAILED(device.As(&device_)) ||
        FAILED(device.As(&multithread))) {
      LOG(ERROR) << "D3D11 hardware device creation failed: " << hr;
      device_.Reset();
      context_.Reset();
      return;
    }
    multithread->SetMultithreadProtected(TRUE);
    luid_ = luid;
    LOG(INFO) << "D3D import requested LUID=" << luid
              << " matched=" << static_cast<bool>(adapter);
  }
  void Convert() {
    std::unique_ptr<Job> job;
    {
      base::AutoLock lock(mutex_);
      job = std::move(pending_);
      scheduled_ = false;
    }
    if (!job) {
      return;
    }
    auto begin = base::TimeTicks::Now();
    auto& latency = LatencyStats::Get();
    latency.Add("convert_queue", job->copied, begin);
    D3D11_TEXTURE2D_DESC desc;
    job->texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(job->device->CreateTexture2D(&desc, nullptr, &staging))) {
      return;
    }
    job->context->CopyResource(staging.Get(), job->texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(
            job->context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
      return;
    }
    if (mapped.RowPitch < desc.Width * 4 ||
        mapped.RowPitch > static_cast<UINT>(std::numeric_limits<int>::max())) {
      job->context->Unmap(staging.Get(), 0);
      return;
    }
    const auto mapped_at = base::TimeTicks::Now();
    latency.Add("staging_map", begin, mapped_at);
    const gfx::Rect& r = job->info->visible_rect;
    auto i420 = webrtc::I420Buffer::Create(r.width(), r.height());
    // SAFETY: Map exposes Height rows of RowPitch bytes. visible_rect was
    // checked inside coded_size, and the row pitch covers all coded pixels.
    const auto* bytes = UNSAFE_BUFFERS(
        static_cast<const uint8_t*>(mapped.pData) +
        static_cast<size_t>(r.y()) * mapped.RowPitch + r.x() * 4);
    // DXGI B8G8R8A8 is libyuv ARGB in little-endian memory (B,G,R,A).
    int result = libyuv::ARGBToI420(
        bytes, static_cast<int>(mapped.RowPitch), i420->MutableDataY(),
        i420->StrideY(), i420->MutableDataU(), i420->StrideU(),
        i420->MutableDataV(), i420->StrideV(), r.width(), r.height());
    job->context->Unmap(staging.Get(), 0);
    const auto converted_at = base::TimeTicks::Now();
    latency.Add("argb_i420", mapped_at, converted_at);
    latency.Add("receive_to_converted", job->received, converted_at);
    if (result != 0) {
      return;
    }
    convert_ms_ += (base::TimeTicks::Now() - begin).InMillisecondsF();
    ++converted_;
    Consumer consumer;
    {
      base::AutoLock lock(mutex_);
      // Drop even the in-progress conversion if a newer frame has arrived.
      if (job->epoch == epoch_ && job->consumer_epoch == consumer_epoch_ &&
          job->serial == latest_) {
        consumer = consumer_;
      }
    }
    if (consumer) {
      latency.Add("converted_to_source", converted_at, base::TimeTicks::Now());
      latency.SourceReady(
          (job->info->capture_end - base::TimeTicks()).InMicroseconds());
      consumer(
          webrtc::VideoFrame::Builder()
              .set_video_frame_buffer(i420)
              .set_timestamp_us(
                  (job->info->capture_end - base::TimeTicks()).InMicroseconds())
              .set_rotation(webrtc::kVideoRotation_0)
              .build());
      ++frames_;
    }
  }
  void LogStats() {
    auto now = base::TimeTicks::Now();
    LOG(INFO) << "D3D source fps=" << frames_ / (now - stats_at_).InSecondsF()
              << " convert_ms=" << (converted_ ? convert_ms_ / converted_ : 0);
    stats_at_ = now;
    frames_ = converted_ = 0;
    convert_ms_ = 0;
    if (now - latency_at_ >= base::Seconds(5)) {
      LatencyStats::Get().Log();
      latency_at_ = now;
    }
    convert_runner_->PostDelayedTask(
        FROM_HERE, base::BindOnce(&Impl::LogStats, base::Unretained(this)),
        base::Seconds(1));
  }
  mutable base::Lock mutex_;
  Consumer consumer_;
  FrameGeometry geometry_;
  uint64_t epoch_ = 0, consumer_epoch_ = 0, latest_ = 0;
  std::unique_ptr<Job> pending_;
  bool scheduled_ = false;
  base::Thread import_, convert_;
  scoped_refptr<base::SingleThreadTaskRunner> import_runner_, convert_runner_;
  // Receiver is only bound/used/reset on import_; device/context are immutable
  // within a job. Multithread protection serializes context calls across
  // threads.
  std::unique_ptr<mojo::Receiver<mojom::PrtsFrameSink>> receiver_;
  std::map<uint32_t, Buffer> buffers_;
  ComPtr<ID3D11Device1> device_;
  ComPtr<ID3D11DeviceContext> context_;
  uint64_t luid_ = 0, bound_epoch_ = 0;
  base::TimeTicks stats_at_ = base::TimeTicks::Now();
  base::TimeTicks latency_at_ = stats_at_;
  int frames_ = 0, converted_ = 0;
  double convert_ms_ = 0;
};

D3DFrameSource::D3DFrameSource() : impl_(std::make_unique<Impl>()) {}
D3DFrameSource::~D3DFrameSource() = default;
mojo::PendingRemote<mojom::PrtsFrameSink> D3DFrameSource::BindCapture(
    uint64_t luid,
    FrameGeometry g) {
  return impl_->Bind(luid, g);
}
void D3DFrameSource::ClearCapture() {
  impl_->Clear();
}
std::unique_ptr<FrameSource> D3DFrameSource::CreateLease() {
  return std::make_unique<Lease>(shared_from_this());
}
void D3DFrameSource::Start(Consumer consumer) {
  impl_->Start(std::move(consumer));
}
void D3DFrameSource::Stop() {
  impl_->Stop();
}
FrameGeometry D3DFrameSource::geometry() const {
  return impl_->geometry();
}
void D3DFrameSource::SetInputSequence(uint16_t) {
}  // Browser owns real markers.
}  // namespace prts
