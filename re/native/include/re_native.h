#ifndef RE_NATIVE_H_
#define RE_NATIVE_H_
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32)
#if defined(RE_NATIVE_IMPLEMENTATION)
#define RE_NATIVE_API __declspec(dllexport)
#else
#define RE_NATIVE_API __declspec(dllimport)
#endif
#else
#define RE_NATIVE_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define RE_NATIVE_ABI_VERSION 1u
/* All strings are UTF-8. No C++ objects or allocator ownership crosses this
 * ABI. */
typedef int32_t re_native_result;
enum {
  RE_NATIVE_OK = 0,
  RE_NATIVE_INVALID_ARGUMENT = -1,
  RE_NATIVE_INVALID_STATE = -2,
  RE_NATIVE_INTERNAL_ERROR = -3,
  RE_NATIVE_BACKPRESSURE = -4,
  RE_NATIVE_ABI_MISMATCH = -5
};
/* Arbitrary native threads; callback must be nonblocking and must not reenter.
 * Bytes are borrowed for the callback only. Replace before creating sessions;
 * clear after destroying all sessions and before releasing user data. */
typedef void (*re_native_log_callback)(void* user,
                                       int32_t severity,
                                       const char* text,
                                       size_t size);
RE_NATIVE_API uint32_t re_native_abi_version(void);
RE_NATIVE_API void re_native_set_log_callback(re_native_log_callback callback,
                                              void* user);
#ifdef __cplusplus
}
#endif
#endif
