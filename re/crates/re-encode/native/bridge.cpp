// Copyright 2026 The remotekit Authors. BSD-3-Clause.
#include "bridge.h"
#include <cstdio>
#include <exception>

extern "C" {
ReEngine* re_encoder_create(uint32_t kind, ID3D11Device* device,
                            const ReParams* params, char* error, size_t capacity) noexcept {
  try {
    auto encoder = kind == 0 ? ReCreateVpl(device, *params) : ReCreateNvenc(device, *params);
    return encoder.release();
  } catch (const std::exception& e) {
    if (capacity) std::snprintf(error, capacity, "%s", e.what());
  } catch (...) {
    if (capacity) std::snprintf(error, capacity, "encoder initialization failed (unknown native exception)");
  }
  return nullptr;
}
void re_encoder_destroy(ReEngine* encoder) noexcept { delete encoder; }
const char* re_encoder_error(ReEngine* encoder) noexcept { return encoder->error.c_str(); }
const char* re_encoder_diagnostics(ReEngine* encoder) noexcept { return encoder->diagnostics.c_str(); }
int re_encoder_encode(ReEngine* encoder, ID3D11Texture2D* texture, void* pointer,
                       void (*release)(void*), uint64_t timestamp, uint32_t idr, ReAu* output) noexcept {
  ReLease lease(pointer, release);
  try { return encoder->Encode(texture, std::move(lease), timestamp, idr != 0, *output); }
  catch (const std::exception& e) { encoder->error = e.what(); }
  catch (...) { encoder->error = "unknown native encode exception"; }
  return -1;
}
int re_encoder_reset(ReEngine* encoder, const ReParams* params) noexcept {
  try { return encoder->Reset(*params); }
  catch (const std::exception& e) { encoder->error = e.what(); }
  catch (...) { encoder->error = "unknown native reconfigure exception"; }
  return -1;
}
}
