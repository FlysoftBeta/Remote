// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "re/browser_isolation.h"

#include <userenv.h>
#include <windows.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
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
#include "base/win/win_util.h"

namespace re {
namespace {

bool EnsureGrant(base::win::SecurityDescriptor& sd,
                 const base::win::Sid& sid,
                 DWORD mask,
                 DWORD inheritance) {
  if (sd.dacl() && sd.dacl()->get()) {
    const auto* acl = sd.dacl()->get();
    for (DWORD i = 0; i < acl->AceCount; ++i) {
      void* raw = nullptr;
      if (!::GetAce(const_cast<ACL*>(acl), i, &raw))
        return false;
      const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw);
      if (ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
          ace->Header.AceFlags == inheritance && ace->Mask == mask &&
          ::EqualSid(const_cast<DWORD*>(&ace->SidStart), sid.GetPSID())) {
        return true;
      }
    }
  }
  // Re-setting an existing ACE can reorder it and recursively rewrite an
  // unchanged install tree on every other tenant's launch.
  return sd.SetDaclEntry(sid, base::win::SecurityAccessMode::kSet, mask,
                         inheritance);
}

bool GrantPathTraversal(base::FilePath path, const base::win::Sid& sid) {
  // GetLongPathName checks read access on the ancestors, even when the package
  // has full access to its profile. Never inherit these directory-only grants.
  for (path = path.DirName(); path != path.DirName(); path = path.DirName()) {
    // MAXIMUM_ALLOWED prevents SetSecurityInfo from recursively propagating
    // the existing inherited ACEs through unrelated ancestor subtrees.
    base::win::ScopedHandle directory(::CreateFileW(
        path.value().c_str(), MAXIMUM_ALLOWED,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    auto sd = base::win::SecurityDescriptor::FromHandle(
        directory.get(), base::win::SecurityObjectType::kFile,
        DACL_SECURITY_INFORMATION);
    const auto original =
        sd ? sd->ToSddl(DACL_SECURITY_INFORMATION) : std::nullopt;
    if (!sd ||
        !EnsureGrant(*sd, sid, FILE_GENERIC_READ | FILE_GENERIC_EXECUTE, 0) ||
        (sd->ToSddl(DACL_SECURITY_INFORMATION) != original &&
         !sd->WriteToHandle(directory.get(),
                            base::win::SecurityObjectType::kFile,
                            DACL_SECURITY_INFORMATION))) {
      PLOG(ERROR) << "Grant AppContainer path traversal " << path;
      return false;
    }
  }
  return true;
}

bool GrantDirectory(const base::FilePath& path,
                    const base::win::Sid& sid,
                    bool writable) {
  auto sd =
      base::win::SecurityDescriptor::FromFile(path, DACL_SECURITY_INFORMATION);
  const auto original =
      sd ? sd->ToSddl(DACL_SECURITY_INFORMATION) : std::nullopt;
  const DWORD inherit = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
  auto owner = base::win::AccessToken::FromCurrentProcess();
  if (writable && sd) {
    // A shared parent's ALL APPLICATION PACKAGES grant must never let other
    // containers read this tenant's profile. Keep only the tenant and brokers.
    sd->set_dacl(base::win::AccessControlList());
    sd->set_dacl_protected(true);
    if (!sd->SetDaclEntry(base::win::WellKnownSid::kLocalSystem,
                          base::win::SecurityAccessMode::kGrant,
                          FILE_ALL_ACCESS, inherit) ||
        !sd->SetDaclEntry(base::win::WellKnownSid::kBuiltinAdministrators,
                          base::win::SecurityAccessMode::kGrant,
                          FILE_ALL_ACCESS, inherit)) {
      return false;
    }
  }
  if (!sd ||
      !EnsureGrant(
          *sd, sid,
          writable ? FILE_ALL_ACCESS : FILE_GENERIC_READ | FILE_GENERIC_EXECUTE,
          inherit) ||
      (writable && (!owner || !EnsureGrant(*sd, owner->User(), FILE_ALL_ACCESS,
                                           inherit))) ||
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

std::optional<std::wstring> PrepareSandboxDesktop(
    const base::win::Sid* sid,
    ScopedBrowserDesktop& sandbox_desktop) {
  const std::wstring desktop_name =
      L"rb-sandbox." +
      base::ASCIIToWide(base::Uuid::GenerateRandomV4().AsLowercaseString());
  auto desktop_sd = base::win::SecurityDescriptor::FromHandle(
      ::GetThreadDesktop(::GetCurrentThreadId()),
      base::win::SecurityObjectType::kDesktop, DACL_SECURITY_INFORMATION);
  if (!desktop_sd ||
      (sid &&
       !desktop_sd->SetDaclEntry(*sid, base::win::SecurityAccessMode::kGrant,
                                 GENERIC_ALL, 0)) ||
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
  return desktop_name;
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
  // Concurrent endpoint starts must not lose another package's install or
  // ancestor ACE during a read/modify/write of the shared directory DACLs.
  base::win::ScopedHandle provisioning(
      ::CreateMutexW(nullptr, FALSE, L"Local\\rb.isolation.provision"));
  const DWORD wait = provisioning.is_valid()
                         ? ::WaitForSingleObject(provisioning.get(), 120000)
                         : WAIT_FAILED;
  if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
    LOG(ERROR) << "AppContainer provisioning lock failed: " << wait;
    return std::nullopt;
  }
  base::ScopedClosureRunner unlock(base::BindOnce(
      [](HANDLE mutex) { ::ReleaseMutex(mutex); }, provisioning.get()));
  if (!sid || !base::CreateDirectory(profile) ||
      !GrantPathTraversal(profile, *sid) ||
      !GrantPathTraversal(install, *sid) ||
      !GrantDirectory(profile, *sid, true) ||
      !GrantDirectory(install, *sid, false)) {
    return std::nullopt;
  }
  const auto user = profile.Append(L"Environment");
  const auto local = user.Append(L"Local");
  const auto roaming = user.Append(L"Roaming");
  const auto temp = user.Append(L"Temp");
  // Windows redirects GetTempPath/GetKnownFolderPath for a contained process
  // under LOCALAPPDATA/Packages/<profile>/AC, independently of TEMP/TMP.
  const auto package = local.Append(L"Packages").Append(name).Append(L"AC");
  if (!base::CreateDirectory(local) || !base::CreateDirectory(roaming) ||
      !base::CreateDirectory(temp) ||
      !base::CreateDirectory(package.Append(L"Temp"))) {
    PLOG(ERROR) << "Create AppContainer environment directories";
    return std::nullopt;
  }
  wchar_t windows[MAX_PATH];
  const UINT length = ::GetWindowsDirectoryW(windows, MAX_PATH);
  if (!length || length >= MAX_PATH)
    return std::nullopt;
  const std::wstring system_root(windows, length);
  const auto desktop_name = PrepareSandboxDesktop(&*sid, sandbox_desktop);
  if (!desktop_name)
    return std::nullopt;
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
           {L"RB_SANDBOX_DESKTOP", *desktop_name}});
  LOG(INFO) << "PRTS AppContainer name=" << base::WideToUTF8(name)
            << " sid=" << base::WideToUTF8(sid->ToSddlString().value());
  return sid;
}

}  // namespace

base::Process LaunchIsolatedBrowser(const base::CommandLine& requested_command,
                                    base::LaunchOptions& options,
                                    ScopedBrowserDesktop& sandbox_desktop) {
  base::CommandLine command(requested_command);
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
        endpoint.HasSwitch("job-memory-mb") ||
        endpoint.HasSwitch("job-cpu-rate") ||
        endpoint.HasSwitch("job-process-count") ||
        endpoint.HasSwitch("isolation-capabilities")) {
      LOG(ERROR)
          << "Isolation options require --isolation=job|lpac|appcontainer";
      return base::Process();
    }
    return base::LaunchProcess(command, options);
  }
  // Preserve the caller's kill-on-close flag while adding whole-browser limits.
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
  if (!::QueryInformationJobObject(options.job_handle,
                                   JobObjectExtendedLimitInformation, &limits,
                                   sizeof(limits), nullptr)) {
    PLOG(ERROR) << "QueryInformationJobObject(limits)";
    return base::Process();
  }
  unsigned int memory_mb = 0, process_count = 0, cpu_rate = 0;
  const struct {
    const char* name;
    unsigned int* value;
    unsigned int max;
  } job_values[] = {{"job-memory-mb", &memory_mb, 1048576},
                    {"job-process-count", &process_count, 65535},
                    {"job-cpu-rate", &cpu_rate, 10000}};
  for (const auto& value : job_values) {
    if (endpoint.HasSwitch(value.name) &&
        (!base::StringToUint(endpoint.GetSwitchValueASCII(value.name),
                             value.value) ||
         !*value.value || *value.value > value.max)) {
      LOG(ERROR) << "Invalid --" << value.name;
      return base::Process();
    }
  }
  if (memory_mb) {
    limits.JobMemoryLimit = static_cast<SIZE_T>(memory_mb) * 1024 * 1024;
    limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_JOB_MEMORY;
  }
  if (process_count) {
    limits.BasicLimitInformation.ActiveProcessLimit = process_count;
    limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
  }
  if (!::SetInformationJobObject(options.job_handle,
                                 JobObjectExtendedLimitInformation, &limits,
                                 sizeof(limits))) {
    PLOG(ERROR) << "SetInformationJobObject(limits)";
    return base::Process();
  }
  if (cpu_rate) {
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu = {};
    cpu.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE |
                       JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
    cpu.CpuRate = cpu_rate;  // 1..10000 = 0.01%..100% of all system CPUs.
    if (!::SetInformationJobObject(options.job_handle,
                                   JobObjectCpuRateControlInformation, &cpu,
                                   sizeof(cpu))) {
      PLOG(ERROR) << "SetInformationJobObject(cpu)";
      return base::Process();
    }
  }
  LOG(INFO) << "PRTS job_memory_mb=" << memory_mb
            << " job_process_count=" << process_count
            << " job_cpu_rate=" << cpu_rate;
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
  std::string extra_caps =
      endpoint.GetSwitchValueASCII("isolation-capabilities");
  if (!extra_caps.empty() && mode != "lpac" && mode != "appcontainer") {
    LOG(ERROR) << "--isolation-capabilities requires lpac or appcontainer";
    return base::Process();
  }
  if (mode == "lpac") {
    extra_caps +=
        ",registryRead,lpacCom,lpacCryptoServices,lpacIdentityServices";
  }
  for (const auto& name : base::SplitStringPiece(
           extra_caps, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    auto sid = base::win::Sid::FromNamedCapability(base::ASCIIToWide(name));
    if (std::ranges::any_of(capability_sids, [&](const auto& existing) {
          return existing.Equal(sid.GetPSID());
        })) {
      continue;
    }
    capability_sids.push_back(std::move(sid));
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
    // Nested AppContainer profile registration fails from an outer container.
    // The network child inherits the outer token; other child sandboxes keep
    // their restricted tokens and mitigations. Never use --no-sandbox here.
    std::string disabled = command.GetSwitchValueASCII("disable-features");
    if (!disabled.empty())
      disabled += ',';
    disabled +=
        "NetworkServiceSandbox,RendererAppContainer,PrintCompositorLPAC,"
        "WinSboxNetworkServiceSandboxIsLPAC";
    command.AppendSwitchASCII("disable-features", disabled);
    container = PrepareContainer(command, mode, environment, sandbox_desktop);
    if (!container)
      return base::Process();
    security.AppContainerSid = container->GetPSID();
    security.Capabilities = capabilities.data();
    security.CapabilityCount = capabilities.size();
  }
  if (mode == "job" && ui_limits) {
    const auto desktop_name = PrepareSandboxDesktop(nullptr, sandbox_desktop);
    if (!desktop_name)
      return base::Process();
    const auto inherited = base::internal::GetEnvironment();
    environment = base::internal::AlterEnvironment(
        inherited,
        {{L"RB_ISOLATION", L"job"}, {L"RB_SANDBOX_DESKTOP", *desktop_name}});
    command.AppendSwitchASCII("rb-isolation", "job");
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
  std::wstring desktop_name;
  if (sandbox_desktop.is_valid()) {
    // A different desktop is required even with HANDLES: FindWindow can reveal
    // foreign HWNDs and SetWindowsHookEx can succeed across outer jobs.
    desktop_name = base::win::GetWindowObjectName(sandbox_desktop.get());
    if (desktop_name.empty()) {
      LOG(ERROR) << "Get isolated browser desktop name";
      return base::Process();
    }
    startup.startup_info()->lpDesktop = desktop_name.data();
  }
  std::wstring line = command.GetCommandLineString();
  if (!::CreateProcessW(
          command.GetProgram().value().c_str(), line.data(), nullptr, nullptr,
          TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
          !environment.empty() ? environment.data() : nullptr, nullptr,
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
