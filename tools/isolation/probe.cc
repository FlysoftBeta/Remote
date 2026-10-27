// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Standalone, controlled two-tenant boundary probe. Never points at host data.
#include <shellapi.h>
#include <windows.h>

#include <cstdio>
#include <string>

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
}  // namespace

int wmain(int argc, wchar_t** argv) {
  const auto output = Arg(argc, argv, L"probe-output");
  FILE* log = nullptr;
  if (_wfopen_s(&log, output.c_str(), L"w") || !log)
    return 2;
  HANDLE token = nullptr;
  DWORD contained = 0, length = 0;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    GetTokenInformation(token, TokenIsAppContainer, &contained,
                        sizeof(contained), &length);
    CloseHandle(token);
  }
  fprintf(log, "pid=%lu thread=%lu appcontainer=%lu\n", GetCurrentProcessId(),
          GetCurrentThreadId(), contained);
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
            reinterpret_cast<unsigned long long>(hwnd), GetLastError());
    if (OpenClipboard(hwnd)) {
      EmptyClipboard();
      constexpr wchar_t canary[] = L"rb-isolation-controlled-clipboard";
      HANDLE data = GlobalAlloc(GMEM_MOVEABLE, sizeof(canary));
      memcpy(GlobalLock(data), canary, sizeof(canary));
      GlobalUnlock(data);
      if (!SetClipboardData(CF_UNICODETEXT, data))
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
      HANDLE data = GetClipboardData(CF_UNICODETEXT);
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
