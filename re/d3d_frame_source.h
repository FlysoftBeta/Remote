// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RE_D3D_FRAME_SOURCE_H_
#define RE_D3D_FRAME_SOURCE_H_

#include <cstdint>
#include <memory>

#include "base/functional/callback.h"
#include "chrome/browser/rb/mojom/rb.mojom.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "re/frame_source.h"

namespace re {
class VplEncoderState;
// Construct/destroy and BindCapture on main. Start/Stop on RTC signaling;
// geometry is thread safe. Mojo import and CPU conversion have separate
// threads.
class D3DFrameSource : public FrameSource,
                       public std::enable_shared_from_this<D3DFrameSource> {
 public:
  D3DFrameSource(std::shared_ptr<VplEncoderState> encoder_state, int fps);
  ~D3DFrameSource() override;
  // on_reset must marshal to its owner sequence; invoked on the import thread.
  mojo::PendingRemote<rb::mojom::RbFrameSink> BindCapture(
      uint64_t gpu_luid,
      FrameGeometry geometry,
      base::RepeatingClosure on_reset);
  void ClearCapture();
  // A new lease for each Peer session; destruction drains its consumer.
  std::unique_ptr<FrameSource> CreateLease();
  void Start(Consumer consumer) override;
  void Stop() override;
  FrameGeometry geometry() const override;
  void SetInputSequence(uint16_t seq) override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace re
#endif  // RE_D3D_FRAME_SOURCE_H_
