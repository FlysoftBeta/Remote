// Copyright 2026 Flysoft. All rights reserved.
#ifndef RE_NATIVE_MOJO_H_
#define RE_NATIVE_MOJO_H_

#include <stdint.h>
#include "re_native.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RE_MOJO_API_VERSION 1u
typedef struct re_mojo re_mojo;
typedef struct re_mojo_size {
  int32_t width, height;
} re_mojo_size;
typedef struct re_mojo_rect {
  int32_t x, y, width, height;
} re_mojo_rect;
typedef struct re_mojo_rect_f {
  float x, y, width, height;
} re_mojo_rect_f;
// UTF-8, length in bytes; never required to be NUL terminated.
typedef struct re_mojo_string {
  const char* data;
  uint32_t size;
} re_mojo_string;
typedef struct re_mojo_hello {
  uint32_t version;
  uint64_t gpu_luid;
  uint8_t software_compositing;
} re_mojo_hello;
typedef struct re_mojo_window {
  uint32_t window_id;
  re_mojo_size size_dip;
  float device_scale_factor;
  re_mojo_size size_px;
  re_mojo_string title;
} re_mojo_window;
typedef struct re_mojo_ime {
  uint32_t seq;
  uint8_t focused;
  re_mojo_string type, mode;
  uint32_t flags;
  re_mojo_rect_f caret_norm;
  re_mojo_string reason;
  uint8_t composing;
  uint32_t context;
} re_mojo_ime;
typedef struct re_mojo_buffer {
  uint32_t window_id, buffer_id;
  uint64_t epoch;
  void* texture;
  re_mojo_size coded_size;
} re_mojo_buffer;
typedef struct re_mojo_frame {
  uint32_t window_id, buffer_id;
  uint64_t epoch, lease_id, frame_seq;
  void* texture;
  void* fence;  // NULL means keyed mutex, key 0.
  uint64_t fence_token_high, fence_token_low, fence_value, adapter_luid;
  re_mojo_size coded_size;
  re_mojo_rect visible_rect, content_rect;
  // Same-machine Chromium TimeTicks microseconds (QPC); 0 = unavailable.
  int64_t capture_begin_us, capture_end_us, reference_time_us;
  int64_t received_us, callback_us;
} re_mojo_frame;
typedef struct re_mojo_pointer {
  int32_t action;  // Down=0, Move=1, Up=2, Cancel=3, Leave=4.
  int32_t type;    // Mouse=0, Touch=1, Pen=2.
  uint8_t pid;
  float x, y;
  uint8_t buttons;
  int8_t button;
  uint8_t mods;
  float pressure, radius_x, radius_y;
  int64_t timestamp_us;  // 0 selects now.
} re_mojo_pointer;
typedef struct re_mojo_wheel {
  float x, y, dx, dy;
  uint8_t mods;
  int64_t timestamp_us;
} re_mojo_wheel;
typedef struct re_mojo_key {
  uint8_t down, mods, repeat;
  uint16_t key_code;
  re_mojo_string code, key;
  int64_t timestamp_us;
} re_mojo_key;
typedef struct re_mojo_stats {
  uint64_t frames, released, abandoned, rejected, resets, outstanding;
  int64_t now_us;
} re_mojo_stats;

// All calls, callbacks and destruction run on the create thread. poll() pumps
// Mojo on that thread; no application callback runs on the Mojo IO thread.
// Callbacks may issue calls/release frames, but MUST NOT poll/destroy/shutdown
// or stop/start captures. No callback is delivered outside poll().
// Callbacks must return promptly and must not throw or unwind across this ABI.
// Strings/struct pointers are borrowed for the callback only. NT handles are
// owned by the shim, borrowed until the pool reset callback RETURNS, capture
// stop, disconnect callback RETURNS, or shutdown. Do not CloseHandle them.
// OpenSharedResource1/OpenSharedFence once per buffer/token; retain COM objects
// if needed. Wait(fence,value) or AcquireSync(0) BEFORE reading. ReleaseSync(0)
// in mutex mode. release_frame(true) ONLY after all GPU reads have completed;
// false abandons the allocation. Unreleased frames are abandoned on reset.
// Retained COM objects must not be read after release or a pool reset.
// Frame callbacks add no allocation; at most 64 outstanding leases per capture.
typedef struct re_mojo_callbacks {
  uint32_t version, struct_size;
  void (*hello)(void*, const re_mojo_hello*);
  void (*window_added)(void*, const re_mojo_window*);
  void (*window_changed)(void*, const re_mojo_window*);
  void (*window_removed)(void*, uint32_t);
  void (*window_activated)(void*, uint32_t);
  void (*cursor)(void*, re_mojo_string);
  void (*ime)(void*, const re_mojo_ime*);
  void (*virtual_keyboard)(void*, uint8_t, uint32_t);
  void (*new_buffer)(void*, const re_mojo_buffer*);
  void (*frame)(void*, const re_mojo_frame*);
  // An ordered pool boundary; also used by browser GPU/Viz recovery. The mojom
  // does not distinguish resize, initial setup and GPU restart. recovered is
  // the first valid frame after this boundary, not a new mojom message.
  void (*reset)(void*, uint32_t window_id, uint64_t epoch);
  void (*recovered)(void*, uint32_t window_id, uint64_t epoch);
  void (*disconnect)(void*);
} re_mojo_callbacks;

RE_NATIVE_API uint32_t re_mojo_api_version(void);
// Returns 0 on success, negative on invalid args/state/capacity/launch failure.
// Only one re_mojo object per thread. The module must stay loaded until exit
// (Mojo embedder process lifetime); destroy drains the object and its
// callbacks.
RE_NATIVE_API int32_t re_mojo_create(const re_mojo_callbacks*,
                                     void* user,
                                     re_mojo** out);
// argv[0] is chrome.exe; UTF-8 arguments, one argument per entry. Adds the same
// isolated invitation, --rb-remote and --headless=new as the legacy endpoint.
// --rb-capture-sync=fence|mutex can override the D3D11 fence capability probe.
// Browser/profile lifetime belongs to this object; shutdown terminates ONLY
// its launched process. Existing browsers cannot accept a fresh invitation.
RE_NATIVE_API int32_t re_mojo_launch(re_mojo*,
                                     uint32_t argc,
                                     const char* const* argv);
RE_NATIVE_API int32_t re_mojo_poll(re_mojo*, uint32_t timeout_ms);
RE_NATIVE_API int32_t re_mojo_start_capture(re_mojo*,
                                            uint32_t window,
                                            uint32_t fps,
                                            uint8_t nv12);
RE_NATIVE_API int32_t re_mojo_stop_capture(re_mojo*, uint32_t window);
RE_NATIVE_API int32_t re_mojo_release_frame(re_mojo*,
                                            uint64_t lease_id,
                                            uint8_t consumed);
RE_NATIVE_API int32_t re_mojo_refresh(re_mojo*, uint32_t window);
RE_NATIVE_API int32_t re_mojo_activate(re_mojo*, uint32_t window);
RE_NATIVE_API int32_t re_mojo_pointer_input(re_mojo*,
                                            uint32_t window,
                                            const re_mojo_pointer*);
RE_NATIVE_API int32_t re_mojo_wheel_input(re_mojo*,
                                          uint32_t window,
                                          const re_mojo_wheel*);
RE_NATIVE_API int32_t re_mojo_key_input(re_mojo*,
                                        uint32_t window,
                                        const re_mojo_key*);
RE_NATIVE_API int32_t re_mojo_reset_input(re_mojo*);
RE_NATIVE_API int32_t re_mojo_ime_commit(re_mojo*,
                                         re_mojo_string,
                                         uint32_t context);
RE_NATIVE_API int32_t re_mojo_ime_compose(re_mojo*,
                                          re_mojo_string,
                                          int32_t cursor,
                                          uint32_t context);
RE_NATIVE_API int32_t re_mojo_ime_delete(re_mojo*,
                                         int32_t before,
                                         int32_t after,
                                         uint32_t context);
RE_NATIVE_API int32_t re_mojo_focus(re_mojo*, uint8_t focused);
RE_NATIVE_API int32_t re_mojo_new_window(re_mojo*, re_mojo_string url);
RE_NATIVE_API int32_t re_mojo_set_viewport(re_mojo*,
                                           re_mojo_size dip,
                                           float dsf);
RE_NATIVE_API int32_t re_mojo_ime_hidden(re_mojo*, uint32_t context);
RE_NATIVE_API int32_t re_mojo_touch_capabilities(re_mojo*,
                                                 uint8_t points,
                                                 uint8_t touch_only);
// Local bridge counters and monotonic probe only: rb.mojom has no stats/ping.
RE_NATIVE_API int32_t re_mojo_get_stats(re_mojo*, re_mojo_stats*);
RE_NATIVE_API int64_t re_mojo_now_us(void);
RE_NATIVE_API int32_t re_mojo_shutdown(re_mojo*);
RE_NATIVE_API void re_mojo_destroy(re_mojo*);

#ifdef __cplusplus
}
#endif
#endif
