// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "re/browser_isolation.h"

#include <userenv.h>
#include <windows.h>

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/hash/sha1.h"
#include "base/logging.h"
#include "base/process/environment_internal.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "base/win/access_token.h"
#include "base/win/scoped_handle.h"
#include "base/win/security_descriptor.h"
#include "base/win/sid.h"
#include "base/win/startup_information.h"

namespace re {
namespace {

bool GrantDirectory(const base::FilePath& path,
                    const base::win::Sid& sid,
                    bool writable) {
  auto sd =
      base::win::SecurityDescriptor::FromFile(path, DACL_SECURITY_INFORMATION);
  const auto original =
      sd ? sd->ToSddl(DACL_SECURITY_INFORMATION) : std::nullopt;
  const DWORD inherit = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
  auto owner = base::win::AccessToken::FromCurrentProcess();
  if (!sd ||
      !sd->SetDaclEntry(
          sid, base::win::SecurityAccessMode::kSet,
          writable ? FILE_ALL_ACCESS : FILE_GENERIC_READ | FILE_GENERIC_EXECUTE,
          inherit) ||
      (writable &&
       (!owner ||
        !sd->SetDaclEntry(owner->User(), base::win::SecurityAccessMode::kGrant,
                          FILE_ALL_ACCESS, inherit))) ||
      (sd->ToSddl(DACL_SECURITY_INFORMATION) != original &&
       !sd->WriteToFile(path, DACL_SECURITY_INFORMATION))) {
    PLOG(ERROR) << "Grant AppContainer directory " << path;
    return false;
  }
  // The profile owner needs WRITE_OWNER to lower the integrity label. The
  // package needs WRITE_DAC for Chromium's network-cache sandbox ACLs.
  if (writable && (!sd->SetMandatoryLabel(SECURITY_MANDATORY_LOW_RID, inherit,
                                          SYSTEM_MANDATORY_LABEL_NO_WRITE_UP) ||
                   !sd->WriteToFile(path, LABEL_SECURITY_INFORMATION))) {
    PLOG(ERROR) << "Set AppContainer directory integrity " << path;
    return false;
  }
  return true;
}

std::optional<base::win::Sid> PrepareContainer(
    const base::CommandLine& command,
    const std::string& mode,
    std::wstring& environment,
    ScopedBrowserDesktop& sandbox_desktop) {
  const auto profile =
      base::MakeAbsoluteFilePath(command.GetSwitchValuePath("user-data-dir"));
  const auto install =
      base::MakeAbsoluteFilePath(command.GetProgram()).DirName();
  if (profile.empty() || install.empty() || profile == install ||
      profile.IsParent(install) || install.IsParent(profile)) {
    LOG(ERROR) << "AppContainer needs a dedicated profile outside the install";
    return std::nullopt;
  }
  // Stable per dedicated profile, with no dependency on a user-controlled web
  // origin. Profiles remain registered for reuse; provisioning owns deletion.
  const std::wstring name =
      L"rb." + base::ASCIIToWide(base::HexEncodeLower(base::SHA1HashString(
                   base::ToLowerASCII(profile.AsUTF8Unsafe()))));
  PSID raw_sid = nullptr;
  HRESULT hr = ::CreateAppContainerProfile(name.c_str(), L"PRTS browser",
                                           L"Remotekit isolated browser",
                                           nullptr, 0, &raw_sid);
  if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
    hr = ::DeriveAppContainerSidFromAppContainerName(name.c_str(), &raw_sid);
  }
  if (FAILED(hr)) {
    LOG(ERROR) << "Create/derive AppContainer profile HRESULT=" << hr;
    return std::nullopt;
  }
  auto sid = base::win::Sid::FromPSID(raw_sid);
  ::FreeSid(raw_sid);
  if (!sid || !base::CreateDirectory(profile) ||
      !GrantDirectory(profile, *sid, true) ||
      !GrantDirectory(install, *sid, false)) {
    return std::nullopt;
  }
  const auto user = profile.Append(L"Environment");
  const auto local = user.Append(L"Local");
  const auto roaming = user.Append(L"Roaming");
  const auto temp = user.Append(L"Temp");
  if (!base::CreateDirectory(local) || !base::CreateDirectory(roaming) ||
      !base::CreateDirectory(temp)) {
    PLOG(ERROR) << "Create AppContainer environment directories";
    return std::nullopt;
  }
  wchar_t windows[MAX_PATH];
  const UINT length = ::GetWindowsDirectoryW(windows, MAX_PATH);
  if (!length || length >= MAX_PATH)
    return std::nullopt;
  const std::wstring system_root(windows, length);
  const std::wstring desktop_name =
      L"rb-sandbox." +
      base::ASCIIToWide(base::Uuid::GenerateRandomV4().AsLowercaseString());
  auto desktop_sd = base::win::SecurityDescriptor::FromHandle(
      ::GetThreadDesktop(::GetCurrentThreadId()),
      base::win::SecurityObjectType::kDesktop, DACL_SECURITY_INFORMATION);
  if (!desktop_sd ||
      !desktop_sd->SetDaclEntry(*sid, base::win::SecurityAccessMode::kGrant,
                                GENERIC_ALL, 0) ||
      !desktop_sd->SetDaclEntry(
          base::win::WellKnownSid::kRestricted,
          base::win::SecurityAccessMode::kDeny,
          WRITE_DAC | WRITE_OWNER | DELETE | DESKTOP_CREATEMENU |
              DESKTOP_CREATEWINDOW | DESKTOP_HOOKCONTROL |
              DESKTOP_JOURNALPLAYBACK | DESKTOP_JOURNALRECORD |
              DESKTOP_SWITCHDESKTOP,
          0)) {
    PLOG(ERROR) << "Prepare browser sandbox desktop ACL";
    return std::nullopt;
  }
  auto absolute_sd = desktop_sd->ToAbsolute();
  SECURITY_ATTRIBUTES attributes = {sizeof(attributes), &absolute_sd, FALSE};
  sandbox_desktop.reset(::CreateDesktopW(desktop_name.c_str(), nullptr, nullptr,
                                         0, GENERIC_ALL, &attributes));
  if (!sandbox_desktop.is_valid()) {
    PLOG(ERROR) << "Create browser sandbox desktop";
    return std::nullopt;
  }
  if (!desktop_sd->SetMandatoryLabel(SECURITY_MANDATORY_UNTRUSTED_RID, 0,
                                     SYSTEM_MANDATORY_LABEL_NO_WRITE_UP) ||
      !desktop_sd->WriteToHandle(sandbox_desktop.get(),
                                 base::win::SecurityObjectType::kDesktop,
                                 LABEL_SECURITY_INFORMATION)) {
    PLOG(ERROR) << "Label browser sandbox desktop";
    return std::nullopt;
  }
  LOG(INFO) << "PRTS sandbox desktop=" << base::WideToUTF8(desktop_name);
  // Do not pass broker credentials or host profile paths into a tenant.
  environment = base::internal::AlterEnvironment(
      {}, {{L"SystemRoot", system_root},
           {L"WINDIR", system_root},
           {L"PATH", system_root + L"\\System32;" + system_root},
           {L"USERPROFILE", user.value()},
           {L"APPDATA", roaming.value()},
           {L"LOCALAPPDATA", local.value()},
           {L"TEMP", temp.value()},
           {L"TMP", temp.value()},
           {L"RB_ISOLATION", base::ASCIIToWide(mode)},
           {L"RB_SANDBOX_DESKTOP", desktop_name}});
  LOG(INFO) << "PRTS AppContainer name=" << base::WideToUTF8(name)
            << " sid=" << base::WideToUTF8(sid->ToSddlString().value());
  return sid;
}

}  // namespace

base::Process LaunchIsolatedBrowser(const base::CommandLine& command,
                                    base::LaunchOptions& options,
                                    ScopedBrowserDesktop& sandbox_desktop) {
  const auto& endpoint = *base::CommandLine::ForCurrentProcess();
  std::string mode = endpoint.GetSwitchValueASCII("isolation");
  if (!endpoint.HasSwitch("isolation"))
    mode = "off";
  if (mode != "off" && mode != "job" && mode != "lpac" &&
      mode != "appcontainer") {
    LOG(ERROR) << "--isolation must be off, job, lpac or appcontainer";
    return base::Process();
  }
  if (mode == "off") {
    if (endpoint.HasSwitch("job-ui-limits") ||
        endpoint.HasSwitch("isolation-capabilities")) {
      LOG(ERROR)
          << "Isolation options require --isolation=job|lpac|appcontainer";
      return base::Process();
    }
    return base::LaunchProcess(command, options);
  }
  DWORD ui_limits = 0;
  const struct {
    const char* name;
    DWORD bit;
  } bits[] = {{"handles", JOB_OBJECT_UILIMIT_HANDLES},
              {"readclipboard", JOB_OBJECT_UILIMIT_READCLIPBOARD},
              {"writeclipboard", JOB_OBJECT_UILIMIT_WRITECLIPBOARD},
              {"systemparameters", JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS},
              {"displaysettings", JOB_OBJECT_UILIMIT_DISPLAYSETTINGS},
              {"globalatoms", JOB_OBJECT_UILIMIT_GLOBALATOMS},
              {"desktop", JOB_OBJECT_UILIMIT_DESKTOP},
              {"exitwindows", JOB_OBJECT_UILIMIT_EXITWINDOWS},
              {"all", 0xff},
              {"none", 0}};
  const std::string ui_names = endpoint.GetSwitchValueASCII("job-ui-limits");
  for (const auto& name : base::SplitStringPiece(
           ui_names, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
    if (name.empty() && !endpoint.HasSwitch("job-ui-limits"))
      continue;
    bool found = false;
    for (const auto& bit : bits) {
      if (name == bit.name) {
        ui_limits |= bit.bit;
        found = true;
        break;
      }
    }
    if (!found) {
      LOG(ERROR) << "Unknown --job-ui-limits name: " << name;
      return base::Process();
    }
  }
  JOBOBJECT_BASIC_UI_RESTRICTIONS ui = {ui_limits};
  if (!::SetInformationJobObject(
          options.job_handle, JobObjectBasicUIRestrictions, &ui, sizeof(ui))) {
    PLOG(ERROR) << "SetInformationJobObject(UI)";
    return base::Process();
  }
  LOG(INFO) << "PRTS isolation=" << mode << " job_ui_limits=" << ui_limits;
  std::wstring environment;
  std::optional<base::win::Sid> container;
  std::vector<base::win::Sid> capability_sids;
  capability_sids.push_back(base::win::Sid::FromKnownCapability(
      base::win::WellKnownCapability::kInternetClient));
  const std::string extra_caps =
      endpoint.GetSwitchValueASCII("isolation-capabilities");
  if (!extra_caps.empty() && mode != "lpac" && mode != "appcontainer") {
    LOG(ERROR) << "--isolation-capabilities requires lpac or appcontainer";
    return base::Process();
  }
  for (const auto& name : base::SplitStringPiece(
           extra_caps, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    capability_sids.push_back(
        base::win::Sid::FromNamedCapability(base::ASCIIToWide(name)));
    LOG(INFO) << "PRTS additional isolation capability=" << name;
  }
  std::vector<SID_AND_ATTRIBUTES> capabilities;
  for (const auto& sid : capability_sids)
    capabilities.push_back({sid.GetPSID(), SE_GROUP_ENABLED});
  SECURITY_CAPABILITIES security = {};
  DWORD package_policy = PROCESS_CREATION_ALL_APPLICATION_PACKAGES_OPT_OUT;
  if (mode == "lpac" || mode == "appcontainer") {
    if (command.HasSwitch("no-sandbox")) {
      LOG(ERROR) << "Whole-browser isolation requires Chromium's child sandbox";
      return base::Process();
    }
    container = PrepareContainer(command, mode, environment, sandbox_desktop);
    if (!container)
      return base::Process();
    security.AppContainerSid = container->GetPSID();
    security.Capabilities = capabilities.data();
    security.CapabilityCount = capabilities.size();
  }
  // Atomic job assignment prevents children escaping between launch and assign,
  // and leaves no suspended orphan if the endpoint is killed during launch.
  base::win::StartupInformation startup;
  if (!startup.InitializeProcThreadAttributeList(2 + (container ? 1 : 0) +
                                                 (mode == "lpac" ? 1 : 0)) ||
      !startup.UpdateProcThreadAttribute(PROC_THREAD_ATTRIBUTE_JOB_LIST,
                                         &options.job_handle, sizeof(HANDLE)) ||
      !startup.UpdateProcThreadAttribute(
          PROC_THREAD_ATTRIBUTE_HANDLE_LIST, options.handles_to_inherit.data(),
          options.handles_to_inherit.size() * sizeof(HANDLE)) ||
      (container && !startup.UpdateProcThreadAttribute(
                        PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &security,
                        sizeof(security))) ||
      (mode == "lpac" &&
       !startup.UpdateProcThreadAttribute(
           PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY,
           &package_policy, sizeof(package_policy)))) {
    PLOG(ERROR) << "Initialize browser launch attributes";
    return base::Process();
  }
  PROCESS_INFORMATION process = {};
  std::wstring line = command.GetCommandLineString();
  if (!::CreateProcessW(
          command.GetProgram().value().c_str(), line.data(), nullptr, nullptr,
          TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
          container ? environment.data() : nullptr, nullptr,
          startup.startup_info(), &process)) {
    PLOG(ERROR) << "CreateProcessW(isolation=" << mode << ")";
    return base::Process();
  }
  base::win::ScopedHandle thread(process.hThread);
  if (container) {
    auto token = base::win::AccessToken::FromProcess(process.hProcess);
    auto actual_sid = token ? token->AppContainerSid() : std::nullopt;
    // TokenIsLessPrivilegedAppContainer is rejected by current Windows user
    // mode. Chromium's token wrapper can read the observed opt-out attribute.
    auto opt_out = token ? token->GetSecurityAttribute(L"WIN://NOALLAPPPKG")
                         : std::nullopt;
    const bool lpac = opt_out && opt_out->values().size() == 1 &&
                      opt_out->values()[0] == L"1";
    if (!token || !token->IsAppContainer() || !actual_sid ||
        !actual_sid->Equal(container->GetPSID()) || lpac != (mode == "lpac")) {
      LOG(ERROR) << "AppContainer token verification failed";
      ::TerminateProcess(process.hProcess, 1);
      ::CloseHandle(process.hProcess);
      return base::Process();
    }
    LOG(INFO) << "PRTS browser token AppContainer=1 LPAC=" << lpac
              << " capabilities=" << token->Capabilities().size();
  }
  LOG(INFO) << "PRTS isolated browser pid=" << process.dwProcessId;
  return base::Process(process.hProcess);
}

}  // namespace re
