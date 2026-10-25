// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRTS_ENDPOINT_D3D_FRAME_SOURCE_H_
#define PRTS_ENDPOINT_D3D_FRAME_SOURCE_H_

#include <cstdint>
#include <memory>

#include "chrome/browser/prts/mojom/prts.mojom.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "prts_endpoint/frame_source.h"

namespace prts {
// Construct/destroy and BindCapture on main. Start/Stop on RTC signaling;
// geometry is thread safe. Mojo import and CPU conversion have separate
// threads.
class D3DFrameSource : public FrameSource,
                       public std::enable_shared_from_this<D3DFrameSource> {
 public:
  D3DFrameSource();
  ~D3DFrameSource() override;
  mojo::PendingRemote<mojom::PrtsFrameSink> BindCapture(uint64_t gpu_luid,
                                                        FrameGeometry geometry);
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
}  // namespace prts
#endif  // PRTS_ENDPOINT_D3D_FRAME_SOURCE_H_
