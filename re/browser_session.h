// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RE_BROWSER_SESSION_H_
#define RE_BROWSER_SESSION_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "re/d3d_frame_source.h"
#include "re/endpoint_link.h"
#include "re/input_parser.h"

namespace re {
// Main-sequence browser owner; InputSink methods marshal from RTC signaling.
class BrowserSession final : public re::EndpointLink::Delegate,
                             public InputSink {
 public:
  using ControlSender = base::RepeatingCallback<void(base::DictValue)>;
  BrowserSession(std::shared_ptr<D3DFrameSource> source,
                 int fps,
                 base::OnceClosure failure);
  ~BrowserSession() override;
  void SetPeerCallbacks(ControlSender send, base::RepeatingClosure keyframe);
  bool Launch(base::CommandLine command);
  void Shutdown();
  void Hello(rb::mojom::HelloInfoPtr info) override;
  void WindowAdded(rb::mojom::WindowInfoPtr info) override;
  void WindowChanged(rb::mojom::WindowInfoPtr info) override;
  void WindowRemoved(uint32_t id) override;
  void WindowActivated(uint32_t id) override;
  void CursorChanged(const std::string& css) override;
  void ImeStateChanged(rb::mojom::ImeStatePtr state) override;
  void VirtualKeyboard(bool show, uint32_t seq) override;
  void OnDisconnect() override;
  void Pointer(const PointerInput& input) override;
  void Wheel(const WheelInput& input) override;
  void Key(const KeyInput& input) override;
  void ResetInput() override;
  void Control(const base::DictValue& message) override;

 private:
  void Select(uint32_t id);
  void StartCapture();
  void Refresh();
  void Send(base::DictValue message);
  void Post(base::OnceCallback<void(BrowserSession&)> task);
  void OnControl(base::DictValue message);
  std::shared_ptr<D3DFrameSource> source_;
  int fps_;
  base::OnceClosure failure_;
  ControlSender send_;
  base::RepeatingClosure keyframe_;
  scoped_refptr<base::SingleThreadTaskRunner> main_runner_;
  re::EndpointLink link_{this};
  std::map<uint32_t, rb::mojom::WindowInfoPtr> windows_;
  // Latest browser state is replayed after each viewer ctl hello.
  std::map<std::string, base::DictValue> state_messages_;
  uint32_t ime_seq_ = 0;
  uint32_t active_ = 0, wanted_ = 0;
  uint64_t luid_ = 0;
  bool hello_ = false, closed_ = false;
  bool disconnect_pending_ = false;
  base::FilePath profile_;
  base::TimeTicks launched_at_;
  base::WeakPtr<BrowserSession> weak_;
  base::WeakPtrFactory<BrowserSession> weak_factory_{this};
};
}  // namespace re
#endif  // RE_BROWSER_SESSION_H_
