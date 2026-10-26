// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#pragma allow_unsafe_libc_calls
#endif

#include <windows.h>

#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "base/at_exit.h"
#include "base/check.h"
#include "base/check_op.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/logging/logging_settings.h"
#include "base/metrics/field_trial.h"
#include "base/process/process.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "mojo/core/embedder/configuration.h"
#include "mojo/core/embedder/embedder.h"
#include "mojo/core/embedder/scoped_ipc_support.h"
#include "prts_endpoint/browser_session.h"
#include "prts_endpoint/experiment_options.h"
#include "prts_endpoint/peer.h"
#include "prts_endpoint/synthetic_source.h"
#include "prts_endpoint/vpl_encoder.h"
#include "third_party/webrtc/rtc_base/ssl_adapter.h"
#include "third_party/webrtc/rtc_base/win32_socket_init.h"  // nogncheck
#include "third_party/webrtc_overrides/init_webrtc.h"

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
  const auto& command = *base::CommandLine::ForCurrentProcess();
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_STDERR;
  CHECK(logging::InitLogging(settings));
  const auto& experiments = prts::GetExperimentOptions();
  if (!experiments.valid)
    return 1;
  // Retain 30 fps: VPL can fall back during 60 fps startup or rate changes on
  // the current driver. --fps=60 plus --cr-fake-vsync-rate=60 is opt-in.
  int fps = 30;
  if (command.HasSwitch("fps") &&
      (!base::StringToInt(command.GetSwitchValueASCII("fps"), &fps) ||
       fps < 1 || fps > 120)) {
    LOG(ERROR) << "--fps must be in [1,120]";
    return 1;
  }
  const std::string encoder = command.GetSwitchValueASCII("encoder");
  if (command.HasSwitch("encoder") && encoder != "vpl" &&
      encoder != "openh264") {
    LOG(ERROR) << "--encoder must be vpl or openh264";
    return 1;
  }
  const bool prefer_vpl = encoder != "openh264";
  LOG(INFO) << "PRTS encoder requested=" << (prefer_vpl ? "vpl" : "openh264");
  const bool synthetic = command.HasSwitch("synthetic");
  auto chrome_path = command.GetSwitchValuePath("chrome");
  auto profile = command.GetSwitchValuePath("user-data-dir");
  if (!synthetic && (chrome_path.empty() || profile.empty())) {
    LOG(ERROR) << "Require --chrome and a dedicated --user-data-dir";
    return 1;
  }
  std::string instance = command.GetSwitchValueASCII("instance");
  if (instance.empty()) {
    instance = "default";
  }
  // PRTS: the endpoint has no window, so Windows 11 may ignore timer
  // resolution requests and throttle it; libwebrtc's pacer, probe and
  // packetizer wake-ups then land on the 15.6 ms tick. Opt out of both and
  // request 1 ms. --no-hires-timer restores the old behaviour for A/B runs.
  if (!command.HasSwitch("no-hires-timer")) {
    PROCESS_POWER_THROTTLING_STATE throttling = {};
    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED |
                             PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    throttling.StateMask = 0;
    ::SetProcessInformation(::GetCurrentProcess(), ProcessPowerThrottling,
                            &throttling, sizeof(throttling));
    base::Time::EnableHighResolutionTimer(true);
    base::Time::ActivateHighResolutionTimer(true);
    LOG(INFO) << "PRTS hires timer active="
              << base::Time::IsHighResolutionTimerInUse();
  }
  base::SingleThreadTaskExecutor executor;
  base::FieldTrialList field_trials;
  // PRTS: libwebrtc field trials (read through WebRtcFieldTrials ->
  // base::FieldTrialList). Opt-in: --webrtc-field-trials=<Trial/Group/...>,
  // e.g. WebRTC-Video-Pacing/factor:2.5/ . Group names may contain ':' and ','.
  if (command.HasSwitch("webrtc-field-trials")) {
    const std::string trials =
        command.GetSwitchValueASCII("webrtc-field-trials");
    if (!trials.empty() && trials != "none") {
      CHECK(base::FieldTrialList::CreateTrialsFromString(trials))
          << "bad --webrtc-field-trials";
      LOG(INFO) << "PRTS webrtc field trials: " << trials;
    }
  }
  if (experiments.fec == "flexfec") {
    // PRTS: advertise the codec AND generate the repair SSRC/FEC-FR group.
    // Reject conflicting --webrtc-field-trials rather than silently disabling
    // protection. CreateTrialsFromString must run before WebRtcEnvironment().
    for (const char* name :
         {"WebRTC-FlexFEC-03", "WebRTC-FlexFEC-03-Advertised"}) {
      const std::string group = base::FieldTrialList::FindFullName(name);
      if (!group.empty() && !group.starts_with("Enabled")) {
        LOG(ERROR) << "--fec=flexfec conflicts with " << name << '/' << group;
        return 1;
      }
      if (group.empty() && !base::FieldTrialList::CreateTrialsFromString(
                               std::string(name) + "/Enabled/")) {
        LOG(ERROR) << "Cannot enable FlexFEC trial " << name;
        return 1;
      }
    }
  }
  if (experiments.fec == "ulpfec" &&
      base::FieldTrialList::FindFullName("WebRTC-DisableUlpFecExperiment")
          .starts_with("Enabled")) {
    LOG(ERROR) << "--fec=ulpfec conflicts with WebRTC-DisableUlpFecExperiment";
    return 1;
  }
  base::FeatureList::InitInstance(
      command.GetSwitchValueASCII("enable-features"),
      command.GetSwitchValueASCII("disable-features"));
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("prts_endpoint");
  webrtc::WinsockInitializer winsock;
  CHECK_EQ(winsock.error(), 0);
  CHECK(InitializeWebRtcModuleBeforeSandbox());
  CHECK(webrtc::InitializeSSL());
  mojo::core::Init({.is_broker_process = true});
  base::Thread io("PrtsMojoIO");
  base::Thread::Options options;
  options.message_pump_type = base::MessagePumpType::IO;
  CHECK(io.StartWithOptions(std::move(options)));
  int result = 1;
  {
    mojo::core::ScopedIPCSupport ipc(
        io.task_runner(), mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);
    {
      prts::Clock clock;
      prts::Signaling signaling;
      auto encoder_state = std::make_shared<prts::VplEncoderState>();
      auto source = synthetic ? nullptr
                              : std::make_shared<prts::D3DFrameSource>(
                                    encoder_state, fps);
      std::unique_ptr<prts::BrowserSession> browser;
      if (!synthetic) {
        browser = std::make_unique<prts::BrowserSession>(
            source, fps, base::BindOnce([] {
              // CP owns endpoint restart. Its stdin may be blocked
              // indefinitely; a broken browser link is a terminal instance
              // error in M1.
              base::Process::TerminateCurrentProcessImmediately(1);
            }));
      }
      prts::LoggingInputSink logging_sink;
      prts::InputSink& input =
          browser ? static_cast<prts::InputSink&>(*browser) : logging_sink;
      prts::Peer peer(
          signaling, input, clock,
          [source, &clock, fps]() -> std::unique_ptr<prts::FrameSource> {
            if (source) {
              return source->CreateLease();
            }
            return std::make_unique<prts::SyntheticSource>(fps, clock);
          },
          fps, encoder_state, prefer_vpl);
      bool ready = peer.Initialize();
      if (ready && browser) {
        browser->SetPeerCallbacks(
            base::BindRepeating(&prts::Peer::SendControl,
                                base::Unretained(&peer)),
            base::BindRepeating(&prts::Peer::RequestKeyFrame,
                                base::Unretained(&peer)));
        base::CommandLine chrome(chrome_path);
        // EndpointLink adds --headless=new --prts-remote and --prts-mojo.
        chrome.AppendSwitchASCII("screen-info", "{1920x1080}");
        chrome.AppendSwitch("no-first-run");
        chrome.AppendSwitch("no-default-browser-check");
        chrome.AppendSwitch("disable-legacy-window");
        chrome.AppendSwitchPath("user-data-dir", profile);
        chrome.AppendSwitchASCII("window-size", "1920,1080");
        // Forward --cr-<switch>[=value] to chrome as --<switch>[=value].
        for (const auto& [name, value] : command.GetSwitches()) {
          if (name.starts_with("cr-") && name.size() > 3) {
            chrome.AppendSwitchNative(name.substr(3), value);
          }
        }
        auto url = command.GetSwitchValueNative("url");
        if (url.empty()) {
          url = L"about:blank";
        }
        // --app: open the URL as an app window (no tab strip/omnibox), used
        // by the latency harness so the page marker sits at the frame origin.
        if (command.HasSwitch("app")) {
          chrome.AppendSwitchNative("app", url);
        } else {
          chrome.AppendArgNative(url);
        }
        ready = browser->Launch(std::move(chrome));
      }
      if (ready) {
        base::ListValue codecs;
        codecs.Append("H264");
        signaling.Send(base::DictValue()
                           .Set("t", "hello")
                           .Set("v", 1)
                           .Set("role", "endpoint")
                           .Set("instance", instance)
                           .Set("name", "prts-endpoint")
                           .Set("caps", base::DictValue()
                                            .Set("codecs", std::move(codecs))
                                            .Set("maxFps", fps)));
        base::RunLoop run;
        signaling.Start([&peer](base::DictValue message) {
          peer.Handle(std::move(message));
        });
        bool valid = false;
        std::thread waiter([&signaling, &valid, quit = run.QuitClosure()] {
          valid = signaling.Wait();
          quit.Run();  // QuitClosure is explicitly safe across threads.
        });
        run.Run();
        waiter.join();
        result = valid ? 0 : 1;
        peer.Close();
      }
      if (browser) {
        browser->SetPeerCallbacks({}, {});
        browser->Shutdown();
      }
    }  // Peer/threads, browser/link/receiver, source and stdio writer
       // destroyed.
    mojo::core::ShutDown();
  }
  io.Stop();
  webrtc::CleanupSSL();
  base::ThreadPoolInstance::Get()->Shutdown();
  if (!command.HasSwitch("no-hires-timer")) {
    base::Time::ActivateHighResolutionTimer(false);
    base::Time::EnableHighResolutionTimer(false);
  }
  return result;
}
