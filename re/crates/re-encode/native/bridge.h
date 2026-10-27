// Copyright 2026 The remotekit Authors. BSD-3-Clause.
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// This private ABI contains no C++ ownership or compiler-dependent bool fields.
struct ReParams {
  uint32_t width, height, fps, target_kbps, min_kbps, max_kbps;
  uint32_t cbr, gop, refresh, slices, vbv_kb, cap_bytes;
  uint32_t low_power, tu, profile, level, preset;
};
struct ReAu { const uint8_t* data; size_t size; uint64_t timestamp; uint32_t frame_type; };
struct ReLease {
  void* pointer = nullptr;
  void (*release)(void*) = nullptr;
  ReLease() = default;
  ReLease(void* p, void (*r)(void*)) : pointer(p), release(r) {}
  ReLease(ReLease&& other) noexcept { *this = std::move(other); }
  ReLease& operator=(ReLease&& other) noexcept {
    if (this != &other) { Clear(); pointer = std::exchange(other.pointer, nullptr); release = other.release; }
    return *this;
  }
  ReLease(const ReLease&) = delete;
  ~ReLease() { Clear(); }
  void Clear() { if (pointer && release) release(std::exchange(pointer, nullptr)); }
};
class ReEngine {
 public:
  virtual ~ReEngine() = default;
  virtual int Encode(ID3D11Texture2D*, ReLease, uint64_t, bool, ReAu&) = 0;
  // 0=applied, 1=unsupported/rejected (old configuration intact), -1=fatal.
  virtual int Reset(const ReParams&) = 0;
  std::string error, diagnostics;
 protected:
  int Fail(const std::string& message, int status = 0) {
    error = message + " (status=" + std::to_string(status) + ")";
    return -1;
  }
};
std::unique_ptr<ReEngine> ReCreateVpl(ID3D11Device*, const ReParams&);
std::unique_ptr<ReEngine> ReCreateNvenc(ID3D11Device*, const ReParams&);
