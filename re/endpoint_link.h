// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RE_ENDPOINT_LINK_H_
#define RE_ENDPOINT_LINK_H_

#include "base/command_line.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "base/sequence_checker.h"
#include "base/win/scoped_handle.h"
#include "chrome/browser/rb/mojom/rb.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace re {

// Prerequisites: main-sequence task executor, ThreadPool, Mojo Init with
// is_broker_process=true, and ScopedIPCSupport on a live IO thread. See NOTES.
class EndpointLink {
 public:
  // The rest of the endpoint implements all RbBrowserClient notifications.
  // It also owns/binds RbFrameSink receivers passed via
  // browser()->StartCapture.
  class Delegate : public rb::mojom::RbBrowserClient {
   public:
    virtual void OnDisconnect() = 0;
  };

  explicit EndpointLink(Delegate* delegate);
  ~EndpointLink();
  // Once per instance; caller supplies executable and a fresh user-data-dir.
  // True means launched/bound, not handshaken: Hello confirms connectivity.
  bool Launch(base::CommandLine command_line);
  rb::mojom::RbBrowser* browser();
  const base::Process& browser_process() const { return browser_process_; }
  // Silent shutdown; does not terminate Chrome. Owner handles process policy.
  void Close();

 private:
  void OnDisconnect();
  raw_ptr<Delegate> delegate_;  // Outlives link; callbacks may destroy link.
  base::Process browser_process_;
  // Kill-on-close job: the browser dies with the endpoint, even when the
  // endpoint is killed, so a restarted endpoint finds its profile free.
  base::win::ScopedHandle job_;
  mojo::Remote<rb::mojom::RbBrowser> browser_;
  mojo::Receiver<rb::mojom::RbBrowserClient> client_receiver_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<EndpointLink> weak_factory_{this};
};

}  // namespace re

#endif  // RE_ENDPOINT_LINK_H_
