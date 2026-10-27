// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Standalone, controlled two-tenant boundary probe. Never points at host data.
#include <windows.h>

#include <sddl.h>
#include <share.h>
#include <winternl.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#if defined(RB_PROBE_HOOK)
extern "C" __declspec(dllexport) LRESULT CALLBACK ProbeHook(int code,
                                                            WPARAM wparam,
                                                            LPARAM lparam) {
  return CallNextHookEx(nullptr, code, wparam, lparam);
}
#else
namespace {
std::wstring Arg(int argc, wchar_t** argv, const wchar_t* key) {
  const std::wstring prefix = std::wstring(L"--") + key + L"=";
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]).starts_with(prefix))
      return argv[i] + prefix.size();
  }
  return {};
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
  return DefWindowProcW(hwnd, message, wp, lp);
}

void TokenDetails(FILE* log, HANDLE token) {
  DWORD length = 0;
  GetTokenInformation(token, TokenAppContainerSid, nullptr, 0, &length);
  std::vector<BYTE> buffer(length);
  if (GetTokenInformation(token, TokenAppContainerSid, buffer.data(), length,
                          &length)) {
    const auto* info =
        reinterpret_cast<TOKEN_APPCONTAINER_INFORMATION*>(buffer.data());
    wchar_t* text = nullptr;
    if (info->TokenAppContainer &&
        ConvertSidToStringSidW(info->TokenAppContainer, &text)) {
      fprintf(log, "package_sid=%ls\n", text);
      LocalFree(text);
    }
  }
  // Same observed opt-out attribute used by re/Chromium. NT uses counted
  // UNICODE_STRING fields here, unlike the public CLAIM_* structures.
  struct Attribute {
    UNICODE_STRING name;
    USHORT type;
    USHORT reserved;
    ULONG flags;
    ULONG count;
    UNICODE_STRING* strings;
  };
  struct Attributes {
    USHORT version;
    USHORT reserved;
    ULONG count;
    Attribute* attributes;
  };
  GetTokenInformation(token, TokenSecurityAttributes, nullptr, 0, &length);
  buffer.resize(length);
  bool opt_out = false;
  if (GetTokenInformation(token, TokenSecurityAttributes, buffer.data(), length,
                          &length)) {
    const auto* info = reinterpret_cast<Attributes*>(buffer.data());
    if (info->version == 1) {
      for (ULONG i = 0; i < info->count; ++i) {
        const auto& a = info->attributes[i];
        if (std::wstring_view(a.name.Buffer, a.name.Length / sizeof(wchar_t)) ==
                L"WIN://NOALLAPPPKG" &&
            a.count == 1) {
          if (a.type == CLAIM_SECURITY_ATTRIBUTE_TYPE_UINT64) {
            opt_out = *reinterpret_cast<const ULONG64*>(a.strings) == 1;
          } else if (a.type == CLAIM_SECURITY_ATTRIBUTE_TYPE_STRING) {
            const auto& value = a.strings[0];
            opt_out = std::wstring_view(value.Buffer,
                                        value.Length / sizeof(wchar_t)) == L"1";
          }
        }
      }
    }
  }
  fprintf(log, "lpac_opt_out=%d\n", opt_out);
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  const auto output = Arg(argc, argv, L"probe-output");
  FILE* log = _wfsopen(output.c_str(), L"w", _SH_DENYNO);
  if (!log)
    return 2;
  HANDLE token = nullptr;
  DWORD contained = 0, length = 0;
  const auto inspect = Arg(argc, argv, L"probe-process");
  HANDLE subject = inspect.empty()
                       ? GetCurrentProcess()
                       : OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                     wcstoul(inspect.c_str(), nullptr, 10));
  if (subject && OpenProcessToken(subject, TOKEN_QUERY, &token)) {
    GetTokenInformation(token, TokenIsAppContainer, &contained,
                        sizeof(contained), &length);
    TokenDetails(log, token);
    CloseHandle(token);
  } else {
    fprintf(log, "token_error=%lu\n", GetLastError());
  }
  fprintf(log, "pid=%lu subject_pid=%lu thread=%lu appcontainer=%lu\n",
          GetCurrentProcessId(), subject ? GetProcessId(subject) : 0,
          GetCurrentThreadId(), contained);
  if (!inspect.empty() && subject)
    CloseHandle(subject);
  wchar_t desktop[256];
  if (GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),
                                UOI_NAME, desktop, sizeof(desktop), &length)) {
    fprintf(log, "desktop=%ls\n", desktop);
  }
  const auto launch = Arg(argc, argv, L"probe-launch");
  if (!launch.empty()) {
    // Reproduce the original desktop initialization under an outer UI job,
    // without the broker-desktop hook. The child inherits this helper's job.
    SetEnvironmentVariableW(L"RB_ISOLATION", L"diagnostic");
    std::wstring line =
        L"\"" + launch +
        L"\" --headless=new --no-first-run --no-default-browser-check "
        L"--enable-logging --v=1 --log-file=\"" +
        output + L".chrome.log\" --user-data-dir=\"" +
        Arg(argc, argv, L"user-data-dir") +
        L"/desktop-reproduction\" about:blank";
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process = {};
    if (CreateProcessW(launch.c_str(), line.data(), nullptr, nullptr, FALSE, 0,
                       nullptr, nullptr, &startup, &process)) {
      if (WaitForSingleObject(process.hProcess, 10000) == WAIT_TIMEOUT)
        TerminateProcess(process.hProcess, 1);
      DWORD code = 0;
      GetExitCodeProcess(process.hProcess, &code);
      fprintf(log, "launched_pid=%lu exit=%lu\n", process.dwProcessId, code);
      CloseHandle(process.hProcess);
      CloseHandle(process.hThread);
    } else {
      fprintf(log, "launch_error=%lu\n", GetLastError());
    }
    fclose(log);
    return 0;
  }
  const auto own = Arg(argc, argv, L"user-data-dir");
  for (std::wstring p = own; !p.empty();) {
    wchar_t buffer[32768];
    SetLastError(0);
    const DWORD n = GetLongPathNameW(p.c_str(), buffer, 32768);
    fprintf(log, "longpath=%ls success=%d error=%lu\n", p.c_str(), n != 0,
            GetLastError());
    const auto slash = p.find_last_of(L"/\\");
    if (slash == std::wstring::npos || slash < 3)
      break;
    p.resize(slash);
  }
  const auto read = Arg(argc, argv, L"probe-read");
  if (!read.empty()) {
    SetLastError(0);
    HANDLE file = CreateFileW(read.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    const DWORD error = GetLastError();
    char bytes[32];
    DWORD count = 0;
    const bool ok = file != INVALID_HANDLE_VALUE &&
                    ReadFile(file, bytes, sizeof(bytes), &count, nullptr);
    fprintf(log, "read=%ls success=%d error=%lu bytes=%lu\n", read.c_str(), ok,
            error, count);
    if (file != INVALID_HANDLE_VALUE)
      CloseHandle(file);
  }
  const auto title = Arg(argc, argv, L"probe-title");
  const auto other_desktop = Arg(argc, argv, L"probe-desktop");
  if (!other_desktop.empty()) {
    SetLastError(0);
    HDESK opened =
        OpenDesktopW(other_desktop.c_str(), 0, FALSE,
                     DESKTOP_READOBJECTS | DESKTOP_CREATEWINDOW |
                         DESKTOP_HOOKCONTROL | DESKTOP_SWITCHDESKTOP);
    fprintf(log, "open_desktop=%d error=%lu\n", opened != nullptr,
            opened ? 0 : GetLastError());
    if (opened)
      CloseDesktop(opened);
  }
  if (Arg(argc, argv, L"probe-serve") == L"1") {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"RbIsolationCanary";
    RegisterClassW(&wc);
    HWND hwnd =
        CreateWindowW(wc.lpszClassName, title.c_str(), WS_OVERLAPPED, 0, 0, 100,
                      100, nullptr, nullptr, wc.hInstance, nullptr);
    fprintf(log, "window=%llu error=%lu\n",
            reinterpret_cast<unsigned long long>(hwnd),
            hwnd ? 0 : GetLastError());
    SetLastError(0);
    const BOOL opened = OpenClipboard(hwnd);
    fprintf(log, "serve_clipboard_open=%d error=%lu\n", opened, GetLastError());
    if (opened) {
      EmptyClipboard();
      constexpr wchar_t canary[] = L"rb-isolation-controlled-clipboard";
      HANDLE data = GlobalAlloc(GMEM_MOVEABLE, sizeof(canary));
      memcpy(GlobalLock(data), canary, sizeof(canary));
      GlobalUnlock(data);
      SetLastError(0);
      const bool set = SetClipboardData(CF_UNICODETEXT, data) != nullptr;
      fprintf(log, "serve_clipboard_set=%d error=%lu\n", set, GetLastError());
      if (!set)
        GlobalFree(data);
      CloseClipboard();
    }
    fflush(log);
    const auto until = GetTickCount64() + 45000;
    while (GetTickCount64() < until) {
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      Sleep(10);
    }
    DestroyWindow(hwnd);
  } else if (!title.empty()) {
    SetLastError(0);
    HWND found = FindWindowW(L"RbIsolationCanary", title.c_str());
    fprintf(log, "findwindow=%llu error=%lu\n",
            reinterpret_cast<unsigned long long>(found), GetLastError());
    HWND target = reinterpret_cast<HWND>(
        _wcstoui64(Arg(argc, argv, L"probe-hwnd").c_str(), nullptr, 10));
    SetLastError(0);
    const BOOL post = PostMessageW(target, WM_APP + 1, 0, 0);
    fprintf(log, "postmessage=%d error=%lu\n", post, GetLastError());
    const auto dll = Arg(argc, argv, L"probe-hook");
    HMODULE module = LoadLibraryW(dll.c_str());
    auto proc =
        module ? reinterpret_cast<HOOKPROC>(GetProcAddress(module, "ProbeHook"))
               : nullptr;
    SetLastError(0);
    HHOOK hook = proc ? SetWindowsHookExW(
                            WH_GETMESSAGE, proc, module,
                            wcstoul(Arg(argc, argv, L"probe-thread").c_str(),
                                    nullptr, 10))
                      : nullptr;
    fprintf(log, "hook_loaded=%d hook=%d error=%lu\n", proc != nullptr,
            hook != nullptr, GetLastError());
    if (hook)
      UnhookWindowsHookEx(hook);
    if (module)
      FreeLibrary(module);
    SetLastError(0);
    const BOOL opened = OpenClipboard(nullptr);
    const DWORD error = GetLastError();
    bool canary = false;
    if (opened) {
      SetLastError(0);
      HANDLE data = GetClipboardData(CF_UNICODETEXT);
      fprintf(log, "clipboard_data=%d error=%lu\n", data != nullptr,
              GetLastError());
      const auto* text =
          data ? static_cast<const wchar_t*>(GlobalLock(data)) : nullptr;
      canary = text && wcscmp(text, L"rb-isolation-controlled-clipboard") == 0;
      if (text)
        GlobalUnlock(data);
      CloseClipboard();
    }
    fprintf(log, "clipboard_open=%d canary_read=%d error=%lu\n", opened, canary,
            error);
  }
  fclose(log);
  return 0;
}
#endif
