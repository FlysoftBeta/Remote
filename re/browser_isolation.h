// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RE_BROWSER_ISOLATION_H_
#define RE_BROWSER_ISOLATION_H_

#include <windows.h>

#include "base/command_line.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/scoped_generic.h"

namespace re {

struct BrowserDesktopTraits {
  static HDESK InvalidValue() { return nullptr; }
  static void Free(HDESK desktop) { ::CloseDesktop(desktop); }
};
using ScopedBrowserDesktop = base::ScopedGeneric<HDESK, BrowserDesktopTraits>;

// Opt-in outer boundary. The caller owns the kill-on-close job. Failure never
// retries with weaker isolation; Chromium's own child sandbox stays enabled.
base::Process LaunchIsolatedBrowser(const base::CommandLine& command,
                                    base::LaunchOptions& options,
                                    ScopedBrowserDesktop& sandbox_desktop);

}  // namespace re

#endif  // RE_BROWSER_ISOLATION_H_
