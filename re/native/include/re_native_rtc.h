#ifndef RE_NATIVE_RTC_H_
#define RE_NATIVE_RTC_H_
#include "re_native.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct re_rtc_session re_rtc_session;
/* Initialize via re_rtc_default_config; all structs are size/version checked.
 * field_trials is Trial/Group/.../; NULL selects
 * WebRTC-Video-Pacing/factor:2.5/. Empty string disables overrides.
 * ice_servers_json is the design/06 iceServers array, including TURN
 * username/credential. NULL means []. Strings copied. */
typedef struct re_rtc_config {
  uint32_t struct_size, abi_version;
  int32_t min_kbps, start_kbps, max_kbps, fps;
  int32_t playout_delay, relay_only;
  const char* field_trials;
  const char* ice_servers_json;
} re_rtc_config;
/* Callbacks can run concurrently on RTC signaling/encoder threads. They must
 * return promptly, must not call this API, and must not throw/unwind. Copy
 * borrowed data before returning. Destroy joins callbacks; user stays alive
 * until destroy returns. Rust wrapper converts callbacks to owned events.
 * state: PeerConnectionState 0=new,1=connecting,2=connected,3=disconnected,
 * 4=failed,5=closed. channel state: 0=connecting,1=open,2=closing,3=closed.
 * candidate=NULL marks end of ICE. SDP kind is "offer" or "answer".
 * on_frame reports handoff timestamps (TimeTicks us), not network delivery. */
typedef struct re_rtc_callbacks {
  uint32_t struct_size, abi_version;
  void* user;
  void (*on_sdp)(void*, const char* kind, const char* sdp);
  void (*on_ice)(void*, const char* mid, int32_t mline, const char* candidate);
  void (*on_state)(void*, int32_t state);
  void (*on_data)(void*,
                  int32_t id,
                  const uint8_t*,
                  size_t,
                  int32_t binary,
                  double received_wall_ms,
                  int64_t received_ticks_us);
  void (*on_channel_state)(void*, int32_t id, int32_t state);
  void (*on_rates)(void*, uint32_t bitrate_bps, double fps);
  void (*on_keyframe)(void*);
  void (*on_stats)(void*, const char* json);
  void (*on_error)(void*, const char* message);
  void (*on_frame)(void*,
                   int64_t capture_us,
                   uint32_t rtp_timestamp,
                   int64_t handoff_us,
                   size_t bytes,
                   int32_t keyframe);
} re_rtc_callbacks;
typedef struct re_rtc_frame {
  uint32_t struct_size, abi_version;
  const uint8_t* annex_b;
  size_t size;
  int64_t timestamp_us; /* strictly increasing Windows TimeTicks us */
  int64_t capture_begin_us, capture_end_us, encode_begin_us, encode_end_us;
  uint32_t width, height;
  int32_t keyframe;
} re_rtc_frame;
/* Serialize all calls on an owner thread, except submit may use one separate
 * producer thread. No calls concurrent with destroy. NULL destroy is allowed.
 * Submit copies bytes before returning; <=16 MiB complete Annex-B AU, IDR
 * includes SPS/PPS, constrained baseline, honest SPS level 4.2 for 1080p60.
 * The shim cannot retain encoded references after a pipeline drop; it requests
 * an IDR and suppresses dependent pictures until that IDR reaches the adapter.
 * Rates callbacks include 0 when suspended; Rust should stop raw production.
 * Callback arrival can precede create return. NULL callbacks are allowed. */
RE_NATIVE_API void re_rtc_default_config(re_rtc_config* config);
RE_NATIVE_API re_native_result re_rtc_create(const re_rtc_config*,
                                             const re_rtc_callbacks*,
                                             re_rtc_session** out);
RE_NATIVE_API void re_rtc_destroy(re_rtc_session*);
/* Create a local offer (restart!=0 restarts ICE), or answer after remote offer.
 */
RE_NATIVE_API re_native_result re_rtc_offer(re_rtc_session*, int32_t restart);
RE_NATIVE_API re_native_result re_rtc_answer(re_rtc_session*);
RE_NATIVE_API re_native_result re_rtc_set_remote_description(re_rtc_session*,
                                                             const char* kind,
                                                             const char* sdp);
/* ICE is queued until SetRemoteDescription completes (max 512). */
RE_NATIVE_API re_native_result re_rtc_add_ice(re_rtc_session*,
                                              const char* mid,
                                              int32_t mline,
                                              const char* candidate);
RE_NATIVE_API re_native_result re_rtc_submit(re_rtc_session*,
                                             const re_rtc_frame*);
/* Create defaults automatically: input=0,ctl=1 ordered/reliable; probe=2
 * unordered/maxRetransmits=0, all negotiated. Extra channels may be created
 * before negotiation. max_retransmits=-1 means reliable. Send <=64 KiB,
 * returns BACKPRESSURE if not open or buffered_amount >64 KiB. Routing,
 * ctl hello/IME/input semantics and ping/pong belong to Rust. */
RE_NATIVE_API re_native_result re_rtc_create_channel(re_rtc_session*,
                                                     int32_t id,
                                                     const char* label,
                                                     int32_t ordered,
                                                     int32_t max_retransmits);
RE_NATIVE_API re_native_result re_rtc_send(re_rtc_session*,
                                           int32_t id,
                                           const uint8_t*,
                                           size_t,
                                           int32_t binary);
RE_NATIVE_API re_native_result re_rtc_request_keyframe(re_rtc_session*);
RE_NATIVE_API re_native_result re_rtc_stats(re_rtc_session*);
/* Uses same clock as browser trace timestamps. */
RE_NATIVE_API int64_t re_rtc_now_us(void);
RE_NATIVE_API double re_rtc_wall_ms(void);
#ifdef __cplusplus
}
#endif
#endif
