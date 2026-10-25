// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#pragma allow_unsafe_libc_calls
#endif

#include "prts_endpoint/browser_session.h"

#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/gfx/geometry/rect_f.h"

namespace prts {
BrowserSession::BrowserSession(std::shared_ptr<D3DFrameSource> source,
                               int fps,
                               base::OnceClosure failure)
    : source_(std::move(source)),
      fps_(fps),
      failure_(std::move(failure)),
      main_runner_(base::SingleThreadTaskRunner::GetCurrentDefault()) {
  weak_ = weak_factory_.GetWeakPtr();
}
BrowserSession::~BrowserSession() {
  Shutdown();
}
void BrowserSession::SetPeerCallbacks(ControlSender send,
                                      base::RepeatingClosure keyframe) {
  send_ = std::move(send);
  keyframe_ = std::move(keyframe);
}
bool BrowserSession::Launch(base::CommandLine command) {
  if (!link_.Launch(std::move(command))) {
    return false;
  }
  main_runner_->PostDelayedTask(FROM_HERE,
                                base::BindOnce(
                                    [](base::WeakPtr<BrowserSession> self) {
                                      if (self && !self->hello_) {
                                        self->OnDisconnect();
                                      }
                                    },
                                    weak_),
                                base::Seconds(15));
  return true;
}
void BrowserSession::Shutdown() {
  if (closed_) {
    return;
  }
  closed_ = true;
  weak_factory_.InvalidateWeakPtrs();
  if (auto* browser = link_.browser()) {
    browser->ResetInput();
    if (active_) {
      browser->StopCapture(active_);
    }
  }
  source_->ClearCapture();
  source_->Stop();
  link_.Close();
  if (link_.browser_process().IsValid()) {
    link_.browser_process().Terminate(0, false);
  }
}
void BrowserSession::Hello(mojom::HelloInfoPtr info) {
  if (info->version != 1 || info->software_compositing) {
    LOG(ERROR) << "M1 requires protocol 1 and D3D11 GPU compositing";
    OnDisconnect();
    return;
  }
  hello_ = true;
  luid_ = info->gpu_luid;
  if (!windows_.empty()) {
    Select(active_ ? active_ : windows_.begin()->first);
  }
}
void BrowserSession::WindowAdded(mojom::WindowInfoPtr info) {
  uint32_t id = info->window_id;
  windows_[id] = std::move(info);
  if (!active_ || wanted_ == id) {
    Select(id);
  }
}
void BrowserSession::WindowChanged(mojom::WindowInfoPtr info) {
  uint32_t id = info->window_id;
  auto it = windows_.find(id);
  bool changed = it == windows_.end() || it->second->size_px != info->size_px ||
                 it->second->device_scale_factor != info->device_scale_factor;
  windows_[id] = std::move(info);
  if ((!active_ || id == active_ || id == wanted_) && changed) {
    Select(id);
  }
}
void BrowserSession::WindowRemoved(uint32_t id) {
  if (id == wanted_) {
    wanted_ = 0;
  }
  windows_.erase(id);
  if (active_ != id) {
    return;
  }
  if (auto* browser = link_.browser()) {
    browser->ResetInput();
    browser->StopCapture(id);
  }
  active_ = 0;
  source_->ClearCapture();
  if (!windows_.empty()) {
    Select(windows_.begin()->first);
  }
}
void BrowserSession::WindowActivated(uint32_t id) {
  wanted_ = id;
  if (id != active_) {
    Select(id);
  }
}
void BrowserSession::Select(uint32_t id) {
  auto it = windows_.find(id);
  auto* browser = link_.browser();
  if (!hello_ || !browser || it == windows_.end()) {
    return;
  }
  const auto& w = *it->second;
  if (!id || w.size_px.IsEmpty() || w.size_px.width() > 8192 ||
      w.size_px.height() > 8192 || !std::isfinite(w.device_scale_factor) ||
      w.device_scale_factor <= 0) {
    return;
  }
  browser->ResetInput();
  if (active_) {
    browser->StopCapture(active_);
  }
  active_ = id;
  FrameGeometry g{w.size_px.width(), w.size_px.height(), w.device_scale_factor};
  auto params = mojom::CaptureParams::New();
  params->max_fps = fps_;
  params->nv12 = false;
  browser->StartCapture(id, std::move(params), source_->BindCapture(luid_, g));
  Send(base::DictValue()
           .Set("t", "frame")
           .Set("w", g.width)
           .Set("h", g.height)
           .Set("dsf", g.dsf));
  Refresh();
}
void BrowserSession::Send(base::DictValue message) {
  if (send_) {
    send_.Run(std::move(message));
  }
}
void BrowserSession::Refresh() {
  if (active_ && link_.browser()) {
    link_.browser()->RequestRefreshFrame(active_);
  }
  if (keyframe_) {
    keyframe_.Run();
  }
}
void BrowserSession::CursorChanged(const std::string& css) {
  Send(base::DictValue().Set("t", "cursor").Set("css", css));
}
void BrowserSession::ImeStateChanged(mojom::ImeStatePtr s) {
  const auto& r = s->caret_norm;
  Send(base::DictValue()
           .Set("t", "ime.state")
           .Set("seq", static_cast<double>(s->seq))
           .Set("focused", s->focused)
           .Set("type", s->type)
           .Set("mode", s->mode)
           .Set("flags", static_cast<double>(s->flags))
           .Set("reason", s->reason)
           .Set("rect", base::DictValue()
                            .Set("x", r.x() * 65535.0)
                            .Set("y", r.y() * 65535.0)
                            .Set("w", r.width() * 65535.0)
                            .Set("h", r.height() * 65535.0)));
}
void BrowserSession::VirtualKeyboard(bool show, uint32_t seq) {
  Send(base::DictValue()
           .Set("t", "ime.vk")
           .Set("show", show)
           .Set("seq", static_cast<double>(seq)));
}
void BrowserSession::OnDisconnect() {
  LOG(ERROR) << "Browser Mojo disconnected, incompatible, or Hello timed out";
  Shutdown();
  if (failure_) {
    std::move(failure_).Run();
  }
}
void BrowserSession::Post(base::OnceCallback<void(BrowserSession&)> task) {
  main_runner_->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<BrowserSession> self,
                        base::OnceCallback<void(BrowserSession&)> task) {
                       if (self && !self->closed_ && self->link_.browser()) {
                         std::move(task).Run(*self);
                       }
                     },
                     weak_, std::move(task)));
}
void BrowserSession::Pointer(const PointerInput& i) {
  Post(base::BindOnce(
      [](PointerInput i, BrowserSession& s) {
        if (!s.active_) {
          return;
        }
        auto p = mojom::PointerInput::New();
        p->action = static_cast<mojom::PointerAction>(i.action);
        p->type = static_cast<mojom::PointerType>(i.type);
        p->pid = i.pid;
        p->x = i.x;
        p->y = i.y;
        p->buttons = i.buttons;
        p->button = i.button;
        p->mods = i.mods;
        p->pressure = i.pressure;
        p->radius_x = i.radius_x;
        p->radius_y = i.radius_y;
        p->timestamp = base::TimeTicks::Now();
        s.link_.browser()->Pointer(s.active_, std::move(p));
      },
      i));
}
void BrowserSession::Wheel(const WheelInput& i) {
  Post(base::BindOnce(
      [](WheelInput i, BrowserSession& s) {
        if (!s.active_) {
          return;
        }
        auto p = mojom::WheelInput::New();
        p->x = i.x;
        p->y = i.y;
        p->dx = i.dx;
        p->dy = i.dy;
        p->mods = i.mods;
        p->timestamp = base::TimeTicks::Now();
        s.link_.browser()->Wheel(s.active_, std::move(p));
      },
      i));
}
void BrowserSession::Key(const KeyInput& i) {
  Post(base::BindOnce(
      [](KeyInput i, BrowserSession& s) {
        if (!s.active_) {
          return;
        }
        auto p = mojom::KeyInput::New();
        p->down = i.down;
        p->mods = i.mods;
        p->repeat = i.repeat;
        p->key_code = i.key_code;
        p->code = i.code;
        p->key = i.key;
        p->timestamp = base::TimeTicks::Now();
        s.link_.browser()->Key(s.active_, std::move(p));
      },
      i));
}
void BrowserSession::ResetInput() {
  Post(base::BindOnce(
      [](BrowserSession& s) { s.link_.browser()->ResetInput(); }));
}
void BrowserSession::Control(const base::DictValue& message) {
  Post(base::BindOnce(
      [](base::DictValue m, BrowserSession& s) { s.OnControl(std::move(m)); },
      message.Clone()));
}
void BrowserSession::OnControl(base::DictValue m) {
  auto* browser = link_.browser();
  const std::string* type = m.FindString("t");
  if (!type) {
    return;
  }
  if (*type == "keyframe") {
    // Peer already asks the encoder for an IDR on this ctl message and hello.
    if (active_) {
      browser->RequestRefreshFrame(active_);
    }
  } else if (*type == "focus") {
    if (auto focused = m.FindBool("focused")) {
      if (!*focused) {
        browser->ResetInput();
      }
      if (*focused && active_) {
        browser->ActivateWindow(active_);
      }
      browser->Focus(*focused);
    }
  } else if (*type == "ime.commit" || *type == "ime.compose") {
    const std::string* text = m.FindString("text");
    if (!active_ || !text || !base::IsStringUTF8AllowingNoncharacters(*text)) {
      return;
    }
    auto utf16 = base::UTF8ToUTF16(*text);
    if (utf16.size() > 4096) {
      return;
    }
    if (*type == "ime.commit") {
      browser->ImeCommit(*text);
    } else if (auto cursor = m.FindInt("cursor");
               cursor && *cursor >= 0 &&
               static_cast<size_t>(*cursor) <= utf16.size()) {
      browser->ImeCompose(*text, *cursor);
    }
  } else if (*type == "ime.delete") {
    auto before = m.FindInt("before"), after = m.FindInt("after");
    if (active_ && before && after && *before >= 0 && *before <= 4096 &&
        *after >= 0 && *after <= 4096) {
      browser->ImeDelete(*before, *after);
    }
  } else if (*type == "ime.key" && active_) {
    const std::string* key = m.FindString("key");
    if (!key) {
      return;
    }
    uint16_t code = *key == "Enter"       ? 13
                    : *key == "Backspace" ? 8
                    : *key == "Tab"       ? 9
                    : *key == "Delete"    ? 46
                    : *key == "Escape"    ? 27
                                          : 0;
    if (!code) {
      return;
    }
    auto p = mojom::KeyInput::New();
    p->down = true;
    p->key_code = code;
    p->code = *key;
    p->key = *key;
    p->timestamp = base::TimeTicks::Now();
    auto up = p.Clone();
    up->down = false;
    browser->Key(active_, std::move(p));
    browser->Key(active_, std::move(up));
  }
  // ime.hidden records local OSK dismissal only. resize has no v1 Mojo method;
  // browser WindowChanged notifications remain authoritative for frame size.
}
}  // namespace prts
