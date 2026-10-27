// Copyright 2026 The remotekit Authors
// Use of this source code is governed by a BSD-style license.

#ifndef RB_LEGACY_ENDPOINT_VPL_ENCODER_H_
#define RB_LEGACY_ENDPOINT_VPL_ENCODER_H_

#include <d3d11.h>
#include <winsock2.h>
#include <wrl/client.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "base/time/time.h"
#include "third_party/webrtc/api/scoped_refptr.h"
#include "third_party/webrtc/api/video/video_frame_buffer.h"
#include "third_party/webrtc/api/video_codecs/video_encoder.h"
#include "third_party/webrtc/api/video_codecs/video_encoder_factory.h"

namespace re {

// Shared by import and encoder queues. Exact COM device identity gates native
// output; an adapter/device replacement first travels through the I420 bridge.
class VplEncoderState {
 public:
  void SetCaptureDevice(Microsoft::WRL::ComPtr<ID3D11Device> device);
  Microsoft::WRL::ComPtr<ID3D11Device> capture_device() const;
  void SetEncoderDevice(Microsoft::WRL::ComPtr<ID3D11Device> device);
  void ClearEncoderDevice(ID3D11Device* device);
  bool NativeActive(ID3D11Device* capture_device) const;
  std::atomic<int> native_pool_limit{8};
  std::atomic<int> min_bitrate_bps{2000000};
  std::atomic<int> max_bitrate_bps{12000000};

 private:
  mutable std::mutex mutex_;
  Microsoft::WRL::ComPtr<ID3D11Device> capture_device_;
  Microsoft::WRL::ComPtr<ID3D11Device> encoder_device_;
};

// Immutable, endpoint-owned, padded NV12 surface. Producer must submit
// conversion on the SAME device/context before publishing. No keyed mutex may
// remain held. lease keeps the pool slot unavailable until all consumers and
// MFX Data.Locked references are gone. A ComPtr alone does not prevent producer
// reuse! Use webrtc::make_ref_counted<VplNativeFrameBuffer>; not final because
// that helper derives RefCountedObject<T>. Readback supplies software fallback.
class VplNativeFrameBuffer : public webrtc::VideoFrameBuffer {
 public:
  VplNativeFrameBuffer(Microsoft::WRL::ComPtr<ID3D11Texture2D> texture,
                       int width,
                       int height,
                       std::shared_ptr<void> lease,
                       Microsoft::WRL::ComPtr<ID3D11Query> completion = nullptr,
                       base::TimeTicks submitted = {},
                       std::shared_ptr<std::atomic<bool>> pool_health = nullptr);
  Type type() const override;
  int width() const override;
  int height() const override;
  webrtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override;
  std::string storage_representation() const override;
  webrtc::scoped_refptr<webrtc::VideoFrameBuffer> CropAndScale(
      int offset_x,
      int offset_y,
      int crop_width,
      int crop_height,
      int scaled_width,
      int scaled_height) override;
  webrtc::scoped_refptr<webrtc::VideoFrameBuffer> GetMappedFrameBuffer(
      std::span<Type> types) override;

  ID3D11Texture2D* texture() const { return texture_.Get(); }
  void RecordVppCompletion(ID3D11DeviceContext* context) const;
  // Valid only for endpoint-owned buffers. Other kNative buffers are converted
  // through ToI420(), never blindly downcast. Chromium builds without RTTI.
  static const VplNativeFrameBuffer* From(
      const webrtc::VideoFrameBuffer& buffer);

 protected:
  ~VplNativeFrameBuffer() override;

 private:
  webrtc::scoped_refptr<webrtc::I420BufferInterface> ReadbackFailed();
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
  std::mutex readback_mutex_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
  std::shared_ptr<std::atomic<bool>> pool_health_;
  int width_;
  int height_;
  std::shared_ptr<void> lease_;
  Microsoft::WRL::ComPtr<ID3D11Query> completion_;
  base::TimeTicks submitted_;
};

// All methods except construction/destruction run on WebRTC's encoder queue.
// Synchronous AsyncDepth=1: no private work queue and no encoded frame
// dropping. Caller must provide the capture/VPP device for the native zero-copy
// path.
class VplEncoder final : public webrtc::VideoEncoder {
 public:
  explicit VplEncoder(std::shared_ptr<VplEncoderState> state);
  ~VplEncoder() override;
  static bool IsAvailable();
  int InitEncode(const webrtc::VideoCodec* codec,
                 const Settings& settings) override;
  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override;
  int32_t Release() override;
  int32_t Encode(const webrtc::VideoFrame& frame,
                 const std::vector<webrtc::VideoFrameType>* types) override;
  void SetRates(const RateControlParameters& parameters) override;
  EncoderInfo GetEncoderInfo() const override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Select VPL when requested and a D3D11 AVC implementation loads.
// Uses the pinned built-in software fallback wrapper. MF/VEA is a later
// adapter.
class VplEncoderFactory final : public webrtc::VideoEncoderFactory {
 public:
  VplEncoderFactory(std::shared_ptr<VplEncoderState> state, bool prefer_vpl);
  ~VplEncoderFactory() override;
  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;
  std::unique_ptr<webrtc::VideoEncoder> Create(
      const webrtc::Environment& env,
      const webrtc::SdpVideoFormat& format) override;

 private:
  std::shared_ptr<VplEncoderState> state_;
  bool use_vpl_ = false;
};

}  // namespace re

#endif  // RB_LEGACY_ENDPOINT_VPL_ENCODER_H_
