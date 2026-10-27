// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Adapted from endpoint_link / browser_session / d3d_frame_source. The wire
// contract remains Chromium's generated rb.mojom C++ bindings.
#include "re_native/include/re_native_mojo.h"

#include <d3d11_4.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/run_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_executor.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "chrome/browser/rb/mojom/rb.mojom.h"
#include "mojo/core/embedder/embedder.h"
#include "mojo/core/embedder/scoped_ipc_support.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/platform/platform_channel.h"
#include "mojo/public/cpp/system/invitation.h"
#include "re_native/runtime.h"

namespace {
// API v1 Windows x64 layout; mirrored by the checked-in Rust FFI assertions.
static_assert(sizeof(void*) == 8);
static_assert(sizeof(re_mojo_hello) == 24);
static_assert(sizeof(re_mojo_window) == 40);
static_assert(sizeof(re_mojo_ime) == 88);
static_assert(sizeof(re_mojo_buffer) == 32);
static_assert(sizeof(re_mojo_frame) == 160);
static_assert(sizeof(re_mojo_pointer) == 48);
static_assert(sizeof(re_mojo_wheel) == 32);
static_assert(sizeof(re_mojo_key) == 48);
static_assert(sizeof(re_mojo_stats) == 56);
static_assert(sizeof(re_mojo_callbacks) == 112);
constexpr int32_t kOk = RE_NATIVE_OK;
constexpr int32_t kArg = RE_NATIVE_INVALID_ARGUMENT;
constexpr int32_t kState = RE_NATIVE_INVALID_STATE;
constexpr int32_t kFull = RE_NATIVE_BACKPRESSURE;
constexpr int32_t kError = RE_NATIVE_INTERNAL_ERROR;
thread_local re_mojo* current_link = nullptr;

int64_t Us(base::TimeTicks t) {
  return (t - base::TimeTicks()).InMicroseconds();
}
base::TimeTicks Ticks(int64_t us) {
  return us ? base::TimeTicks() + base::Microseconds(us)
            : base::TimeTicks::Now();
}
re_mojo_string View(const std::string& s) {
  return {s.data(), static_cast<uint32_t>(s.size())};
}
bool Valid(re_mojo_string s, size_t max = 65536) {
  return s.size <= max && (!s.size || s.data) &&
         base::IsStringUTF8AllowingNoncharacters(
             std::string_view(s.data ? s.data : "", s.size));
}
std::string Copy(re_mojo_string s) {
  return std::string(s.data ? s.data : "", s.size);
}
re_mojo_size Size(const gfx::Size& s) {
  return {s.width(), s.height()};
}
re_mojo_rect Rect(const gfx::Rect& r) {
  return {r.x(), r.y(), r.width(), r.height()};
}
bool Unit(float f) {
  return std::isfinite(f) && f >= 0 && f <= 1;
}

class MojoRuntime {
 public:
  MojoRuntime() : io_("ReMojoIO") {
    re::native::EnsureRuntime();
    mojo::core::Init({.is_broker_process = true});
    base::Thread::Options options;
    options.message_pump_type = base::MessagePumpType::IO;
    CHECK(io_.StartWithOptions(std::move(options)));
    ipc_ = std::make_unique<mojo::core::ScopedIPCSupport>(
        io_.task_runner(), mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);
  }

 private:
  base::Thread io_;
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_;
};
bool FenceSupported() {
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  Microsoft::WRL::ComPtr<ID3D11Device5> device5;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context4;
  Microsoft::WRL::ComPtr<ID3D11Fence> fence;
  return SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                     D3D11_CREATE_DEVICE_BGRA_SUPPORT |
                                         D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                                     nullptr, 0, D3D11_SDK_VERSION, &device,
                                     nullptr, &context)) &&
         SUCCEEDED(device.As(&device5)) && SUCCEEDED(context.As(&context4)) &&
         SUCCEEDED(device5->CreateFence(0, D3D11_FENCE_FLAG_SHARED,
                                        IID_PPV_ARGS(&fence)));
}
}  // namespace

struct re_mojo final : rb::mojom::RbBrowserClient {
  struct Capture final : rb::mojom::RbFrameSink {
    struct Buffer {
      uint32_t id = 0;
      mojo::PlatformHandle handle;
      gfx::Size size;
    };
    struct Fence {
      base::UnguessableToken token;
      mojo::PlatformHandle handle;
      uint64_t luid = 0;
    };
    struct Lease {
      uint64_t id = 0;
      OnFrameCallback reply;
    };
    Capture(re_mojo* owner, uint32_t id) : owner(owner), window(id) {}
    ~Capture() override { Clear(); }
    void Clear() {
      for (auto& l : leases) {
        if (l.reply) {
          std::move(l.reply).Run(false);
          ++owner->stats.abandoned;
          --owner->stats.outstanding;
        }
        l.id = 0;
      }
      for (auto& b : buffers) {
        b.handle.reset();
        b.id = 0;
      }
      for (auto& f : fences) {
        f.handle.reset();
        f.token = {};
      }
      recovering = true;
    }
    void Reset() override {
      ++epoch;
      ++owner->stats.resets;
      owner->Notify(owner->callbacks.reset, window, epoch);
      Clear();
    }
    void NewBuffer(uint32_t id,
                   mojo::PlatformHandle handle,
                   const gfx::Size& size) override {
      if (!id || !handle.is_valid() || size.IsEmpty() || size.width() > 8192 ||
          size.height() > 8192)
        return;
      Buffer* slot = nullptr;
      for (auto& b : buffers) {
        if (b.id == id) {
          slot = &b;
          break;
        }
        if (!slot && !b.id)
          slot = &b;
      }
      if (!slot) {
        ++owner->stats.rejected;
        return;
      }
      // Browser IDs are unique until reset; never replace a borrowed live
      // handle.
      if (slot->id) {
        ++owner->stats.rejected;
        return;
      }
      slot->id = id;
      slot->handle = std::move(handle);
      slot->size = size;
      re_mojo_buffer b{window, id, epoch, slot->handle.GetHandle().get(),
                       Size(size)};
      owner->Notify(owner->callbacks.new_buffer, &b);
    }
    void OnFrame(rb::mojom::FrameInfoPtr info, OnFrameCallback reply) override {
      const auto received = base::TimeTicks::Now();
      Buffer* buffer = nullptr;
      for (auto& b : buffers)
        if (b.id && b.id == info->buffer_id) {
          buffer = &b;
          break;
        }
      if (!buffer || buffer->size != info->coded_size ||
          info->visible_rect.IsEmpty() ||
          !gfx::Rect(info->coded_size).Contains(info->visible_rect) ||
          info->capture_end.is_null()) {
        ++owner->stats.rejected;
        std::move(reply).Run(true);  // No read submitted.
        return;
      }
      re_mojo_frame frame{};
      frame.window_id = window;
      frame.buffer_id = info->buffer_id;
      frame.epoch = epoch;
      frame.frame_seq = info->frame_seq;
      frame.texture = buffer->handle.GetHandle().get();
      if (info->ready_fence) {
        auto& ready = *info->ready_fence;
        Fence* fence = nullptr;
        Fence* empty = nullptr;
        for (auto& f : fences) {
          if (f.token == ready.token) {
            fence = &f;
            break;
          }
          if (!empty && !f.handle.is_valid())
            empty = &f;
        }
        if (!ready.value || ready.token.is_empty() ||
            (!fence && (!empty || !ready.fence_handle.is_valid()))) {
          ++owner->stats.rejected;
          std::move(reply).Run(true);
          return;
        }
        if (!fence) {
          fence = empty;
          fence->token = ready.token;
          fence->handle = std::move(ready.fence_handle);
          fence->luid = ready.adapter_luid;
        }
        if (fence->luid != ready.adapter_luid) {
          ++owner->stats.rejected;
          std::move(reply).Run(true);
          return;
        }
        frame.fence = fence->handle.GetHandle().get();
        frame.fence_token_high = ready.token.GetHighForSerialization();
        frame.fence_token_low = ready.token.GetLowForSerialization();
        frame.fence_value = ready.value;
        frame.adapter_luid = ready.adapter_luid;
      }
      Lease* lease = nullptr;
      for (auto& l : leases)
        if (!l.reply) {
          lease = &l;
          break;
        }
      if (!lease) {
        ++owner->stats.rejected;
        std::move(reply).Run(true);
        return;
      }
      lease->id = ++owner->next_lease;
      lease->reply = std::move(reply);
      frame.lease_id = lease->id;
      frame.coded_size = Size(info->coded_size);
      frame.visible_rect = Rect(info->visible_rect);
      frame.content_rect = Rect(info->content_rect);
      frame.capture_begin_us = Us(info->capture_begin);
      frame.capture_end_us = Us(info->capture_end);
      frame.reference_time_us = Us(info->reference_time);
      frame.received_us = Us(received);
      ++owner->stats.frames;
      ++owner->stats.outstanding;
      if (recovering) {
        recovering = false;
        owner->Notify(owner->callbacks.recovered, window, epoch);
      }
      frame.callback_us = Us(base::TimeTicks::Now());
      if (owner->callbacks.frame)
        owner->Notify(owner->callbacks.frame, &frame);
      else
        owner->Release(frame.lease_id, true);
    }
    re_mojo* const owner;
    const uint32_t window;
    uint64_t epoch = 0;
    bool recovering = true;
    std::array<Buffer, 64> buffers;
    std::array<Fence, 8> fences;
    std::array<Lease, 64> leases;
    mojo::Receiver<rb::mojom::RbFrameSink> receiver{this};
  };

  re_mojo(re_mojo_callbacks callbacks, void* user)
      : callbacks(callbacks), user(user) {
    if (!base::SingleThreadTaskRunner::HasCurrentDefault())
      executor = std::make_unique<base::SingleThreadTaskExecutor>();
  }
  bool Owner() const { return thread == base::PlatformThread::CurrentId(); }
  bool Ready() const { return Owner() && !closed && browser.is_bound(); }
  template <class Fn, class... Args>
  void Notify(Fn fn, Args... args) {
    if (!fn)
      return;
    CHECK(!in_callback);
    in_callback = true;
    fn(user, args...);
    in_callback = false;
  }
  int32_t Release(uint64_t id, bool consumed) {
    for (auto& [window, capture] : captures) {
      for (auto& l : capture->leases)
        if (l.id == id && l.reply) {
          l.id = 0;
          auto reply = std::move(l.reply);
          --stats.outstanding;
          if (consumed)
            ++stats.released;
          else
            ++stats.abandoned;
          std::move(reply).Run(consumed);
          return kOk;
        }
    }
    return kArg;
  }
  void Disconnected() {
    if (closed)
      return;
    closed = true;
    client.reset();
    browser.reset();
    Notify(callbacks.disconnect);
    captures.clear();
  }
  void Hello(rb::mojom::HelloInfoPtr info) override {
    re_mojo_hello hello{info->version, info->gpu_luid,
                        static_cast<uint8_t>(info->software_compositing)};
    Notify(callbacks.hello, &hello);
  }
  re_mojo_window Window(const rb::mojom::WindowInfo& w) {
    return {w.window_id, Size(w.size_dip), w.device_scale_factor,
            Size(w.size_px), View(w.title)};
  }
  void WindowAdded(rb::mojom::WindowInfoPtr info) override {
    auto w = Window(*info);
    Notify(callbacks.window_added, &w);
  }
  void WindowChanged(rb::mojom::WindowInfoPtr info) override {
    auto w = Window(*info);
    Notify(callbacks.window_changed, &w);
  }
  void WindowRemoved(uint32_t id) override {
    Notify(callbacks.window_removed, id);
  }
  void WindowActivated(uint32_t id) override {
    Notify(callbacks.window_activated, id);
  }
  void CursorChanged(const std::string& css) override {
    Notify(callbacks.cursor, View(css));
  }
  void ImeStateChanged(rb::mojom::ImeStatePtr s) override {
    const auto& r = s->caret_norm;
    re_mojo_ime ime{s->seq,          static_cast<uint8_t>(s->focused),
                    View(s->type),   View(s->mode),
                    s->flags,        {r.x(), r.y(), r.width(), r.height()},
                    View(s->reason), static_cast<uint8_t>(s->composing),
                    s->context};
    Notify(callbacks.ime, &ime);
  }
  void VirtualKeyboard(bool show, uint32_t seq) override {
    Notify(callbacks.virtual_keyboard, static_cast<uint8_t>(show), seq);
  }

  const base::PlatformThreadId thread = base::PlatformThread::CurrentId();
  std::unique_ptr<base::SingleThreadTaskExecutor> executor;
  const re_mojo_callbacks callbacks;
  void* const user;
  bool closed = false, polling = false, in_callback = false, launched = false;
  uint64_t next_lease = 0;
  re_mojo_stats stats{};
  base::Process process;
  mojo::Remote<rb::mojom::RbBrowser> browser;
  mojo::Receiver<rb::mojom::RbBrowserClient> client{this};
  std::map<uint32_t, std::unique_ptr<Capture>> captures;
};

extern "C" {
uint32_t re_mojo_api_version() {
  return RE_MOJO_API_VERSION;
}
int64_t re_mojo_now_us() {
  return Us(base::TimeTicks::Now());
}
int32_t re_mojo_create(const re_mojo_callbacks* callbacks,
                       void* user,
                       re_mojo** out) {
  if (!out)
    return kArg;
  *out = nullptr;
  if (!callbacks || callbacks->version != RE_MOJO_API_VERSION ||
      callbacks->struct_size != sizeof(*callbacks))
    return RE_NATIVE_ABI_MISMATCH;
  if (current_link)
    return kState;
  static base::NoDestructor<MojoRuntime> runtime;
  current_link = new re_mojo(*callbacks, user);
  *out = current_link;
  return kOk;
}
int32_t re_mojo_launch(re_mojo* s, uint32_t argc, const char* const* argv) {
  if (!s || !s->Owner() || s->in_callback || s->closed || s->launched)
    return kState;
  if (!argc || argc > 256 || !argv || !argv[0] || !*argv[0])
    return kArg;
  base::CommandLine::StringVector args;
  // The C caller guarantees argc readable pointers and NUL-terminated strings.
  for (const char* argument : UNSAFE_BUFFERS(base::span(argv, argc))) {
    if (!argument || !base::IsStringUTF8AllowingNoncharacters(argument))
      return kArg;
    args.push_back(base::UTF8ToWide(argument));
  }
  base::CommandLine command(args);
  if (command.HasSwitch(mojo::PlatformChannel::kHandleSwitch) ||
      command.HasSwitch("disable-mojo-broker") || command.HasSwitch("type") ||
      command.HasSwitch("rb-mojo") || !command.HasSwitch("user-data-dir"))
    return kArg;
  auto mode = command.GetSwitchValueASCII("rb-capture-sync");
  if (!mode.empty() && mode != "fence" && mode != "mutex")
    return kArg;
  if (mode.empty())
    command.AppendSwitchASCII("rb-capture-sync",
                              FenceSupported() ? "fence" : "mutex");
  mojo::PlatformChannel channel;
  base::LaunchOptions options;
  command.AppendSwitchASCII("rb-mojo",
                            channel.PrepareToPassRemoteEndpoint(options));
  command.AppendSwitch("rb-remote");
  command.AppendSwitchASCII("headless", "new");
  s->process = base::LaunchProcess(command, options);
  channel.RemoteProcessLaunchAttempted();
  if (!s->process.IsValid())
    return kError;
  s->launched = true;
  auto pipe = mojo::OutgoingInvitation::SendIsolated(
      channel.TakeLocalEndpoint(), {}, s->process.Handle());
  if (!pipe.is_valid()) {
    s->process.Terminate(1, false);
    s->closed = true;
    return kError;
  }
  s->browser.Bind(
      mojo::PendingRemote<rb::mojom::RbBrowser>(std::move(pipe), 0));
  s->browser.set_disconnect_handler(
      base::BindOnce(&re_mojo::Disconnected, base::Unretained(s)));
  auto client = s->client.BindNewPipeAndPassRemote();
  s->client.set_disconnect_handler(
      base::BindOnce(&re_mojo::Disconnected, base::Unretained(s)));
  s->browser->SetClient(std::move(client));
  return kOk;
}
int32_t re_mojo_poll(re_mojo* s, uint32_t timeout_ms) {
  if (!s || !s->Owner() || s->polling || s->in_callback || timeout_ms > 1000)
    return kState;
  s->polling = true;
  base::RunLoop loop;
  if (timeout_ms) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), base::Milliseconds(timeout_ms));
    loop.Run();
  } else
    loop.RunUntilIdle();
  s->polling = false;
  return kOk;
}
int32_t re_mojo_start_capture(re_mojo* s,
                              uint32_t window,
                              uint32_t fps,
                              uint8_t nv12) {
  if (!s || !s->Ready() || s->in_callback)
    return kState;
  if (!window || !fps || fps > 120 || nv12 > 1)
    return kArg;
  if (s->captures.contains(window))
    return kState;
  if (s->captures.size() >= 16)
    return kFull;
  auto capture = std::make_unique<re_mojo::Capture>(s, window);
  auto params = rb::mojom::CaptureParams::New(fps, !!nv12);
  s->browser->StartCapture(window, std::move(params),
                           capture->receiver.BindNewPipeAndPassRemote());
  s->captures.emplace(window, std::move(capture));
  return kOk;
}
int32_t re_mojo_stop_capture(re_mojo* s, uint32_t window) {
  if (!s || !s->Ready() || s->in_callback)
    return kState;
  s->browser->StopCapture(window);
  s->captures.erase(window);
  return kOk;
}
int32_t re_mojo_release_frame(re_mojo* s, uint64_t lease, uint8_t consumed) {
  if (!s || !s->Owner())
    return kState;
  if (!lease || consumed > 1)
    return kArg;
  return s->Release(lease, !!consumed);
}
int32_t re_mojo_refresh(re_mojo* s, uint32_t window) {
  if (!s || !s->Ready())
    return kState;
  s->browser->RequestRefreshFrame(window);
  return kOk;
}
int32_t re_mojo_activate(re_mojo* s, uint32_t window) {
  if (!s || !s->Ready())
    return kState;
  s->browser->ActivateWindow(window);
  return kOk;
}
int32_t re_mojo_pointer_input(re_mojo* s,
                              uint32_t window,
                              const re_mojo_pointer* i) {
  if (!s || !s->Ready())
    return kState;
  if (!i || !window || i->action < 0 || i->action > 4 || i->type < 0 ||
      i->type > 2 || i->pid > 15 || !Unit(i->x) || !Unit(i->y) ||
      !Unit(i->pressure) || !std::isfinite(i->radius_x) ||
      !std::isfinite(i->radius_y) || i->radius_x < 0 || i->radius_y < 0)
    return kArg;
  auto p = rb::mojom::PointerInput::New();
  p->action = static_cast<rb::mojom::PointerAction>(i->action);
  p->type = static_cast<rb::mojom::PointerType>(i->type);
  p->pid = i->pid;
  p->x = i->x;
  p->y = i->y;
  p->buttons = i->buttons;
  p->button = i->button;
  p->mods = i->mods;
  p->pressure = i->pressure;
  p->radius_x = i->radius_x;
  p->radius_y = i->radius_y;
  p->timestamp = Ticks(i->timestamp_us);
  s->browser->Pointer(window, std::move(p));
  return kOk;
}
int32_t re_mojo_wheel_input(re_mojo* s,
                            uint32_t window,
                            const re_mojo_wheel* i) {
  if (!s || !s->Ready())
    return kState;
  if (!i || !window || !Unit(i->x) || !Unit(i->y) || !std::isfinite(i->dx) ||
      !std::isfinite(i->dy))
    return kArg;
  auto p = rb::mojom::WheelInput::New();
  p->x = i->x;
  p->y = i->y;
  p->dx = i->dx;
  p->dy = i->dy;
  p->mods = i->mods;
  p->timestamp = Ticks(i->timestamp_us);
  s->browser->Wheel(window, std::move(p));
  return kOk;
}
int32_t re_mojo_key_input(re_mojo* s, uint32_t window, const re_mojo_key* i) {
  if (!s || !s->Ready())
    return kState;
  if (!i || !window || i->down > 1 || i->repeat > 1 || !Valid(i->code, 32) ||
      !Valid(i->key, 32))
    return kArg;
  auto p = rb::mojom::KeyInput::New();
  p->down = !!i->down;
  p->mods = i->mods;
  p->repeat = !!i->repeat;
  p->key_code = i->key_code;
  p->code = Copy(i->code);
  p->key = Copy(i->key);
  p->timestamp = Ticks(i->timestamp_us);
  s->browser->Key(window, std::move(p));
  return kOk;
}
int32_t re_mojo_reset_input(re_mojo* s) {
  if (!s || !s->Ready())
    return kState;
  s->browser->ResetInput();
  return kOk;
}
int32_t re_mojo_ime_commit(re_mojo* s, re_mojo_string text, uint32_t context) {
  if (!s || !s->Ready())
    return kState;
  if (!Valid(text, 16384) || base::UTF8ToUTF16(Copy(text)).size() > 4096)
    return kArg;
  s->browser->ImeCommit(Copy(text), context);
  return kOk;
}
int32_t re_mojo_ime_compose(re_mojo* s,
                            re_mojo_string text,
                            int32_t cursor,
                            uint32_t context) {
  if (!s || !s->Ready())
    return kState;
  if (!Valid(text, 16384))
    return kArg;
  auto length = base::UTF8ToUTF16(Copy(text)).size();
  if (length > 4096 || cursor < 0 || static_cast<size_t>(cursor) > length)
    return kArg;
  s->browser->ImeCompose(Copy(text), cursor, context);
  return kOk;
}
int32_t re_mojo_ime_delete(re_mojo* s,
                           int32_t before,
                           int32_t after,
                           uint32_t context) {
  if (!s || !s->Ready())
    return kState;
  if (before < 0 || after < 0 || before > 4096 || after > 4096)
    return kArg;
  s->browser->ImeDelete(before, after, context);
  return kOk;
}
int32_t re_mojo_focus(re_mojo* s, uint8_t focused) {
  if (!s || !s->Ready())
    return kState;
  if (focused > 1)
    return kArg;
  s->browser->Focus(!!focused);
  return kOk;
}
int32_t re_mojo_new_window(re_mojo* s, re_mojo_string url) {
  if (!s || !s->Ready())
    return kState;
  if (!Valid(url))
    return kArg;
  s->browser->NewWindow(Copy(url));
  return kOk;
}
int32_t re_mojo_set_viewport(re_mojo* s, re_mojo_size dip, float dsf) {
  if (!s || !s->Ready())
    return kState;
  if (dip.width < 1 || dip.height < 1 || dip.width > 16384 ||
      dip.height > 16384 || !std::isfinite(dsf) || dsf <= 0 || dsf > 8)
    return kArg;
  s->browser->SetViewport(gfx::Size(dip.width, dip.height), dsf);
  return kOk;
}
int32_t re_mojo_ime_hidden(re_mojo* s, uint32_t context) {
  if (!s || !s->Ready())
    return kState;
  s->browser->ImeHidden(context);
  return kOk;
}
int32_t re_mojo_touch_capabilities(re_mojo* s,
                                   uint8_t points,
                                   uint8_t touch_only) {
  if (!s || !s->Ready())
    return kState;
  if (points > 10 || touch_only > 1)
    return kArg;
  s->browser->SetTouchCapabilities(points, !!touch_only);
  return kOk;
}
int32_t re_mojo_get_stats(re_mojo* s, re_mojo_stats* stats) {
  if (!s || !s->Owner())
    return kState;
  if (!stats)
    return kArg;
  *stats = s->stats;
  stats->now_us = re_mojo_now_us();
  return kOk;
}
int32_t re_mojo_shutdown(re_mojo* s) {
  if (!s || !s->Owner() || s->in_callback || s->polling)
    return kState;
  if (s->browser.is_bound()) {
    s->browser->ResetInput();
    for (auto& [window, capture] : s->captures)
      s->browser->StopCapture(window);
  }
  s->closed = true;
  s->client.reset();
  s->browser.reset();
  s->captures.clear();
  if (s->process.IsValid()) {
    s->process.Terminate(0, false);
    s->process.Close();
  }
  return kOk;
}
void re_mojo_destroy(re_mojo* s) {
  if (!s)
    return;
  CHECK(s->Owner());
  CHECK(!s->in_callback);
  CHECK(!s->polling);
  re_mojo_shutdown(s);
  current_link = nullptr;
  delete s;
}
}  // extern "C"
