// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#pragma allow_unsafe_libc_calls
#endif

#include "prts_endpoint/d3d_frame_source.h"
#include "prts_endpoint/latency_stats.h"

#include "prts_endpoint/d3d_native_frame.h"

#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <limits>
#include <map>
#include <utility>
#include <vector>

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
#include "third_party/libyuv/include/libyuv/convert_from_argb.h"
#include "third_party/webrtc/api/video/color_space.h"
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
  Impl(std::shared_ptr<VplEncoderState> state, int fps)
      : import_("PrtsD3DImport"), convert_("PrtsI420"),
        encoder_state_(std::move(state)), fps_(fps) {
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
                         self->native_pool_.reset();
                         self->encoder_state_->SetCaptureDevice(nullptr);
                         self->context_.Reset();
                         self->device_.Reset();
                         done->Signal();
                       },
                       base::Unretained(this), base::Unretained(&done)));
    done.Wait();
    import_.Stop();
    convert_.Stop();
  }
  mojo::PendingRemote<mojom::PrtsFrameSink>
  Bind(uint64_t luid, FrameGeometry g, base::RepeatingClosure on_reset) {
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
               base::RepeatingClosure on_reset,
               mojo::PendingReceiver<mojom::PrtsFrameSink> receiver) {
              self->receiver_.reset();
              self->buffers_.clear();
              self->fences_.clear();
              if (self->native_pool_)
                self->native_pool_->ClearImports();
              self->bound_epoch_ = epoch;
              self->on_reset_ = std::move(on_reset);
              if (!self->device_ || self->luid_ != luid) {
                self->CreateDevice(luid);
              }
              self->RebuildNativePool();
              self->receiver_ =
                  std::make_unique<mojo::Receiver<mojom::PrtsFrameSink>>(
                      self, std::move(receiver));
            },
            base::Unretained(this), luid, epoch, std::move(on_reset),
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
                                              if (self->native_pool_)
                                                self->native_pool_->ClearImports();
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
    if (native_pool_)
      native_pool_->ClearImports();
    if (!device_ || !handle.is_valid() || size.IsEmpty() ||
        size.width() > 8192 || size.height() > 8192 || buffers_.size() >= 64) {
      return;
    }
    Buffer buffer;
    HRESULT hr = device_->OpenSharedResource1(handle.GetHandle().get(),
                                              IID_PPV_ARGS(&buffer.texture));
    if (FAILED(hr)) {
      LOG(ERROR) << "OpenSharedResource1/keyed mutex failed: " << hr;
      return;
    }
    // PRTS: no-keyed textures are usable only with a matching ready dependency.
    buffer.texture.As(&buffer.mutex);
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
        std::move(reply).Run(true);
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
      std::move(reply).Run(true);
      return;
    }
    auto& buffer = it->second;
    const auto shared_source = buffer.texture;
    D3D11_TEXTURE2D_DESC desc;
    buffer.texture->GetDesc(&desc);
    if (info->coded_size != gfx::Size(static_cast<int>(desc.Width),
                                      static_cast<int>(desc.Height))) {
      std::move(reply).Run(true);
      return;
    }
    // PRTS: open once per writer token; validate the actual consumer adapter.
    const bool fenced = !!info->ready_fence;
    if (fenced == !!buffer.mutex) {
      LOG(ERROR) << "PRTS capture sync/texture mismatch";
      std::move(reply).Run(true);
      return;
    }
    ComPtr<ID3D11Query> source_read;
    bool read_submitted = false;
    if (fenced) {
      auto& ready = *info->ready_fence;
      ComPtr<IDXGIDevice> dxgi;
      ComPtr<IDXGIAdapter> adapter;
      DXGI_ADAPTER_DESC adapter_desc{};
      ComPtr<ID3D11Device5> device5;
      ComPtr<ID3D11DeviceContext4> context4;
      if (!ready.value || ready.token.is_empty() || FAILED(device_.As(&dxgi)) ||
          FAILED(dxgi->GetAdapter(&adapter)) ||
          FAILED(adapter->GetDesc(&adapter_desc)) ||
          ready.adapter_luid != ((uint64_t{static_cast<uint32_t>(
                                      adapter_desc.AdapterLuid.HighPart)}
                                  << 32) |
                                 adapter_desc.AdapterLuid.LowPart) ||
          FAILED(device_.As(&device5)) || FAILED(context_.As(&context4))) {
        std::move(reply).Run(true);  // No shared read submitted.
        return;
      }
      auto fence = fences_.find(ready.token);
      if (fence == fences_.end()) {
        ComPtr<ID3D11Fence> opened;
        if (!ready.fence_handle.is_valid() || fences_.size() >= 8 ||
            FAILED(device5->OpenSharedFence(
                ready.fence_handle.GetHandle().get(), IID_PPV_ARGS(&opened)))) {
          LOG(ERROR) << "PRTS OpenSharedFence failed";
          std::move(reply).Run(true);
          return;
        }
        LOG(INFO) << "PRTS OpenSharedFence token=" << ready.token.ToString();
        fence = fences_.emplace(ready.token, std::move(opened)).first;
      }
      D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0};
      if (pending_reads_.size() >= 32 ||
          FAILED(device_->CreateQuery(&query, &source_read)) ||
          FAILED(context4->Wait(fence->second.Get(), ready.value))) {
        std::move(reply).Run(true);
        return;
      }
      if (TraceEnabled())
        LOG(INFO) << "PRTS TR F seq=" << info->frame_seq
                  << " value=" << ready.value
                  << " cb=" << TraceUs(info->capture_begin)
                  << " ce=" << TraceUs(info->capture_end)
                  << " rcv=" << TraceUs(received)
                  << " wait=" << TraceUs(base::TimeTicks::Now()) << " pending="
                  << (fence->second->GetCompletedValue() < ready.value);
    }
    if (native_pool_ && !native_pool_->healthy() &&
        received - native_pool_retry_ >= base::Seconds(2)) {
      RebuildNativePool();
    }
    if (native_pool_ && native_pool_->healthy() &&
        (fenced || encoder_state_->NativeActive(device_.Get()))) {
      const RECT rect{visible.x(), visible.y(), visible.right(),
                      visible.bottom()};
      bool discard_source = false;
      job->native = native_pool_->Convert(
          buffer.texture.Get(), buffer.mutex.Get(), rect,
          encoder_state_->native_pool_limit.load(), &discard_source,
          source_read.Get(), &read_submitted);
      if (discard_source)
        buffers_.erase(it);
      job->copied = base::TimeTicks::Now();
      latency.Add("native_vpp", received, job->copied);
      if (fenced && read_submitted) {
        HoldSourceRead(shared_source, source_read, std::move(reply));
      } else {
        std::move(reply).Run(true);
      }
      if (TraceEnabled())
        LOG(INFO) << "PRTS TR P cb=" << TraceUs(info->capture_begin)
                  << " rcv=" << TraceUs(received)
                  << " vpp=" << TraceUs(job->copied);
      if (!job->native)
        return;  // Raw pool pressure/timeout; failed VPP uses CPU next frame.
    } else {
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = 0;
      desc.CPUAccessFlags = 0;
      desc.MiscFlags = 0;
      if (FAILED(device_->CreateTexture2D(&desc, nullptr, &job->texture))) {
        std::move(reply).Run(true);
        return;
      }
      // WAIT_TIMEOUT and WAIT_ABANDONED are nonnegative: SUCCEEDED is wrong.
      if (HRESULT hr = buffer.mutex ? buffer.mutex->AcquireSync(0, 20) : S_OK;
          hr != S_OK) {
        LOG(WARNING) << "PRTS AcquireSync failed: " << hr;
        if (hr != WAIT_TIMEOUT)
          buffers_.erase(it);
        std::move(reply).Run(true);
        return;
      }
      context_->CopyResource(job->texture.Get(), buffer.texture.Get());
      job->copied = base::TimeTicks::Now();
      latency.Add("import_copy_submit", received, job->copied);
      HRESULT released = buffer.mutex ? buffer.mutex->ReleaseSync(0) : S_OK;
      if (fenced) {
        context_->End(source_read.Get());
        context_->Flush();
        HoldSourceRead(shared_source, source_read, std::move(reply));
      } else {
        std::move(reply).Run(true);
      }
      if (released != S_OK) {
        LOG(ERROR) << "ReleaseSync failed: " << released;
        buffers_.erase(it);
        return;
      }
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
      pending_ = std::move(job);  // Replace the older pending raw-frame lease.
      if (scheduled_) {
        return;
      }
      scheduled_ = true;
    }
    convert_runner_->PostTask(
        FROM_HERE, base::BindOnce(&Impl::Convert, base::Unretained(this)));
  }
  void Reset() override {
    // PRTS: retire old leases, timelines and blocked GPU waits on pool restart.
    for (auto& read : pending_reads_)
      std::move(read.reply).Run(false);
    const bool had_pending = !pending_reads_.empty();
    pending_reads_.clear();
    fences_.clear();
    if (had_pending)
      CreateDevice(luid_);
    buffers_.clear();
    RebuildNativePool();
    {
      base::AutoLock lock(mutex_);
      if (bound_epoch_ != epoch_) {
        return;
      }
      pending_.reset();
      bound_epoch_ = ++epoch_;
    }
    // PRTS: Reset precedes replacement NewBuffer/OnFrame messages on this pipe.
    // Imported COM textures and native VPP imports were cleared above; retire
    // old conversion work by epoch and request an IDR via the owner sequence.
    LOG(INFO) << "PRTS: capture reset; texture imports cleared, requesting IDR";
    if (on_reset_) {
      on_reset_.Run();
    }
  }

 private:
  struct SourceRead {
    ComPtr<ID3D11Texture2D> source;
    ComPtr<ID3D11Query> query;
    ComPtr<ID3D11DeviceContext> context;
    OnFrameCallback reply;
    base::TimeTicks queued;
  };
  void HoldSourceRead(ComPtr<ID3D11Texture2D> source,
                      ComPtr<ID3D11Query> query,
                      OnFrameCallback reply) {
    pending_reads_.push_back({std::move(source), std::move(query), context_,
                              std::move(reply), base::TimeTicks::Now()});
    if (!poll_pending_) {
      poll_pending_ = true;
      import_runner_->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Impl::PollSourceReads, base::Unretained(this)),
          base::Milliseconds(1));
    }
  }
  void PollSourceReads() {
    poll_pending_ = false;
    for (auto it = pending_reads_.begin(); it != pending_reads_.end();) {
      BOOL complete = FALSE;
      const HRESULT hr =
          it->context->GetData(it->query.Get(), &complete, sizeof(complete),
                               D3D11_ASYNC_GETDATA_DONOTFLUSH);
      if (hr == S_OK && complete) {
        std::move(it->reply).Run(true);
        it = pending_reads_.erase(it);
      } else if (FAILED(hr) ||
                 base::TimeTicks::Now() - it->queued > base::Seconds(2)) {
        LOG(ERROR) << "PRTS source read abandoned hr=" << hr;
        std::move(it->reply).Run(false);
        it = pending_reads_.erase(it);
      } else {
        ++it;
      }
    }
    if (!pending_reads_.empty()) {
      poll_pending_ = true;
      import_runner_->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Impl::PollSourceReads, base::Unretained(this)),
          base::Milliseconds(1));
    }
  }
  void RebuildNativePool() {
    native_pool_.reset();
    native_pool_retry_ = base::TimeTicks::Now();
    if (device_) {
      native_pool_ = std::make_unique<D3DNativeFramePool>(device_, fps_);
      LOG(INFO) << "PRTS native pool rebuilt";
    }
  }

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
    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> native;
    ComPtr<ID3D11Device1> device;
    ComPtr<ID3D11DeviceContext> context;
    mojom::FrameInfoPtr info;
    base::TimeTicks received, copied;
    uint64_t epoch = 0, consumer_epoch = 0, serial = 0;
  };
  void CreateDevice(uint64_t luid) {
    native_pool_.reset();
    encoder_state_->SetCaptureDevice(nullptr);
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
    if (luid && !adapter) {
      LOG(ERROR) << "PRTS capture adapter LUID not found: " << luid;
      return;
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

    ComPtr<ID3D11Multithread> multithread;
    if (FAILED(hr) || FAILED(device.As(&device_)) ||
        FAILED(context_.As(&multithread))) {
      LOG(ERROR) << "D3D11 hardware device creation failed: " << hr;
      device_.Reset();
      context_.Reset();
      return;
    }
    multithread->SetMultithreadProtected(TRUE);
    encoder_state_->SetCaptureDevice(device);
    native_pool_ = std::make_unique<D3DNativeFramePool>(device, fps_);
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
    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> output = job->native;
    if (!output) {
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
      if (FAILED(job->context->Map(staging.Get(), 0, D3D11_MAP_READ, 0,
                                   &mapped))) {
        return;
      }
      if (mapped.RowPitch < desc.Width * 4 ||
          mapped.RowPitch >
              static_cast<UINT>(std::numeric_limits<int>::max())) {
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
      int result = libyuv::ARGBToI420Matrix(
          bytes, static_cast<int>(mapped.RowPitch), i420->MutableDataY(),
          i420->StrideY(), i420->MutableDataU(), i420->StrideU(),
          i420->MutableDataV(), i420->StrideV(), &libyuv::kArgbH709Constants,
          r.width(), r.height());
      job->context->Unmap(staging.Get(), 0);
      latency.Add("argb_i420", mapped_at, base::TimeTicks::Now());
      if (result != 0) {
        return;
      }
      convert_ms_ += (base::TimeTicks::Now() - begin).InMillisecondsF();
      ++converted_;
      output = i420;
    }
    const auto converted_at = base::TimeTicks::Now();
    latency.Add("receive_to_converted", job->received, converted_at);
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
      if (TraceEnabled()) {  // m3-trace
        LOG(INFO) << "PRTS TR S ce=" << TraceUs(job->info->capture_end)
                  << " cb=" << TraceUs(job->info->capture_begin)
                  << " ref=" << TraceUs(job->info->reference_time)
                  << " rcv=" << TraceUs(job->received)
                  << " cp=" << TraceUs(job->copied)
                  << " cv=" << TraceUs(converted_at)
                  << " native=" << (job->native != nullptr);
      }
      consumer(
          webrtc::VideoFrame::Builder()
              .set_video_frame_buffer(output)
              .set_color_space(
                  webrtc::ColorSpace(webrtc::ColorSpace::PrimaryID::kBT709,
                                     webrtc::ColorSpace::TransferID::kBT709,
                                     webrtc::ColorSpace::MatrixID::kBT709,
                                     webrtc::ColorSpace::RangeID::kLimited))
              .set_timestamp_us(
                  (job->info->capture_end - base::TimeTicks()).InMicroseconds())
              .set_rotation(webrtc::kVideoRotation_0)
              .build());
      ++frames_;
      native_frames_ += job->native ? 1 : 0;
    }
  }
  void LogStats() {
    auto now = base::TimeTicks::Now();
    LOG(INFO) << "D3D source fps=" << frames_ / (now - stats_at_).InSecondsF()
              << " native=" << native_frames_ << " i420=" << converted_
              << " convert_ms=" << (converted_ ? convert_ms_ / converted_ : 0);
    stats_at_ = now;
    frames_ = converted_ = native_frames_ = 0;
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
  std::map<base::UnguessableToken, ComPtr<ID3D11Fence>> fences_;
  std::vector<SourceRead> pending_reads_;
  bool poll_pending_ = false;
  base::RepeatingClosure on_reset_;  // Import sequence only.
  ComPtr<ID3D11Device1> device_;
  ComPtr<ID3D11DeviceContext> context_;
  std::shared_ptr<VplEncoderState> encoder_state_;
  const int fps_;
  std::unique_ptr<D3DNativeFramePool> native_pool_;
  base::TimeTicks native_pool_retry_;
  uint64_t luid_ = 0, bound_epoch_ = 0;
  base::TimeTicks stats_at_ = base::TimeTicks::Now();
  base::TimeTicks latency_at_ = stats_at_;
  int frames_ = 0, converted_ = 0, native_frames_ = 0;
  double convert_ms_ = 0;
};

D3DFrameSource::D3DFrameSource(std::shared_ptr<VplEncoderState> state, int fps)
    : impl_(std::make_unique<Impl>(std::move(state), fps)) {}
D3DFrameSource::~D3DFrameSource() = default;
mojo::PendingRemote<mojom::PrtsFrameSink> D3DFrameSource::BindCapture(
    uint64_t luid,
    FrameGeometry g,
    base::RepeatingClosure on_reset) {
  return impl_->Bind(luid, g, std::move(on_reset));
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
