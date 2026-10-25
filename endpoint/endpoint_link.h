// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRTS_ENDPOINT_ENDPOINT_LINK_H_
#define PRTS_ENDPOINT_ENDPOINT_LINK_H_

#include "base/command_line.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "base/sequence_checker.h"
#include "chrome/browser/prts/mojom/prts.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace prts_endpoint {

// Prerequisites: main-sequence task executor, ThreadPool, Mojo Init with
// is_broker_process=true, and ScopedIPCSupport on a live IO thread. See NOTES.
class EndpointLink {
 public:
  // The rest of the endpoint implements all PrtsBrowserClient notifications.
  // It also owns/binds PrtsFrameSink receivers passed via
  // browser()->StartCapture.
  class Delegate : public prts::mojom::PrtsBrowserClient {
   public:
    virtual void OnDisconnect() = 0;
  };

  explicit EndpointLink(Delegate* delegate);
  ~EndpointLink();
  // Once per instance; caller supplies executable and a fresh user-data-dir.
  // True means launched/bound, not handshaken: Hello confirms connectivity.
  bool Launch(base::CommandLine command_line);
  prts::mojom::PrtsBrowser* browser();
  const base::Process& browser_process() const { return browser_process_; }
  // Silent shutdown; does not terminate Chrome. Owner handles process policy.
  void Close();

 private:
  void OnDisconnect();
  raw_ptr<Delegate> delegate_;  // Outlives link; callbacks may destroy link.
  base::Process browser_process_;
  mojo::Remote<prts::mojom::PrtsBrowser> browser_;
  mojo::Receiver<prts::mojom::PrtsBrowserClient> client_receiver_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<EndpointLink> weak_factory_{this};
};

}  // namespace prts_endpoint

#endif  // PRTS_ENDPOINT_ENDPOINT_LINK_H_
