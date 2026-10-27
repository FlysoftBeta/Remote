#include "re_native/runtime.h"

#include <windows.h>
#include <memory>
#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/logging.h"
#include "base/logging/logging_settings.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/time/time.h"
#include "re_native/include/re_native.h"

namespace {
struct LogSink {
  base::Lock lock;
  re_native_log_callback callback = nullptr;
  void* user = nullptr;
};
LogSink& Sink() {
  static base::NoDestructor<LogSink> sink;
  return *sink;
}
bool Log(int severity, const char*, int, size_t, const std::string& message) {
  auto& sink = Sink();
  base::AutoLock lock(sink.lock);
  if (!sink.callback)
    return false;
  sink.callback(sink.user, severity, message.data(), message.size());
  return true;
}
struct Runtime {
  base::AtExitManager at_exit;
  Runtime() {
    base::CommandLine::Init(0, nullptr);
    if (!base::FeatureList::GetInstance()) {
      auto features = std::make_unique<base::FeatureList>();
      features->InitFromCommandLine("", "");
      base::FeatureList::SetInstance(std::move(features));
    }
    logging::LoggingSettings settings;
    settings.logging_dest = logging::LOG_TO_STDERR;
    logging::InitLogging(settings);
    logging::SetLogMessageHandler(Log);
    PROCESS_POWER_THROTTLING_STATE throttling = {};
    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED |
                             PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    ::SetProcessInformation(::GetCurrentProcess(), ProcessPowerThrottling,
                            &throttling, sizeof(throttling));
    base::Time::EnableHighResolutionTimer(true);
    base::Time::ActivateHighResolutionTimer(true);
    base::ThreadPoolInstance::CreateAndStartWithDefaultParams("re_native");
  }
};
}  // namespace
namespace re::native {
void EnsureRuntime() {
  // Deliberately process lifetime: Rust may own multiple RTC/Mojo sessions.
  // Native module must remain loaded until process exit (no FreeLibrary).
  static base::NoDestructor<Runtime> runtime;
}
}  // namespace re::native
uint32_t re_native_abi_version() {
  return RE_NATIVE_ABI_VERSION;
}
void re_native_set_log_callback(re_native_log_callback callback, void* user) {
  auto& sink = Sink();
  base::AutoLock lock(sink.lock);
  sink.callback = callback;
  sink.user = user;
}
