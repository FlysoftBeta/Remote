// Copyright 2026 The remotekit Authors
// Use of this source code is governed by a BSD-style license.
#ifndef RE_D3D_NATIVE_FRAME_H_
#define RE_D3D_NATIVE_FRAME_H_

#include "re/vpl_encoder.h"

namespace re {

// Used only on the import queue. Owns a bounded pool, VP/enumerator, and
// private BGRA input views. Old output slots survive resize through each native
// buffer lease.
class D3DNativeFramePool {
 public:
  D3DNativeFramePool(Microsoft::WRL::ComPtr<ID3D11Device> device, int fps);
  ~D3DNativeFramePool();
  // Null on raw-frame pool pressure, mutex timeout, or failure. No GPU
  // readback. Source must not call the CPU path for this same frame after a
  // failed Blt.
  webrtc::scoped_refptr<webrtc::VideoFrameBuffer> Convert(
      ID3D11Texture2D* shared_texture,
      IDXGIKeyedMutex* keyed_mutex,
      const RECT& visible,
      int pool_limit,
      bool* discard_source,
      ID3D11Query* source_read_done = nullptr,
      bool* source_read_submitted = nullptr);
  bool healthy() const;
  void ClearImports();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace re
#endif  // RE_D3D_NATIVE_FRAME_H_
