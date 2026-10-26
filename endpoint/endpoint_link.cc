// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#pragma allow_unsafe_libc_calls
#endif

#include "prts_endpoint/endpoint_link.h"

#include <d3d11_4.h>
#include <wrl/client.h>
#include <utility>
#include "base/logging.h"

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/process/launch.h"
#include "chrome/browser/prts/prts_switches.h"
#include "mojo/public/cpp/platform/platform_channel.h"
#include "mojo/public/cpp/system/invitation.h"

namespace prts_endpoint {

EndpointLink::EndpointLink(Delegate* delegate)
    : delegate_(delegate), client_receiver_(delegate) {
  CHECK(delegate);
}
EndpointLink::~EndpointLink() {
  Close();
}

bool EndpointLink::Launch(base::CommandLine command_line) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Chrome must stay a broker: its standard channel switch changes Mojo init.
  if (browser_process_.IsValid() ||
      command_line.HasSwitch(mojo::PlatformChannel::kHandleSwitch) ||
      command_line.HasSwitch("disable-mojo-broker") ||
      command_line.HasSwitch("type")) {
    return false;
  }
  mojo::PlatformChannel channel;
  base::LaunchOptions options;
  command_line.AppendSwitchASCII(prts::switches::kPrtsMojo,
                                 channel.PrepareToPassRemoteEndpoint(options));
  // PRTS: select timeline capture by default only if the consumer supports it.
  auto mode = base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
      "capture-sync");
  if (!mode.empty() && mode != "fence" && mode != "mutex") {
    LOG(ERROR) << "--capture-sync must be fence or mutex";
    return false;
  }
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  Microsoft::WRL::ComPtr<ID3D11Device5> device5;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context4;
  Microsoft::WRL::ComPtr<ID3D11Fence> fence;
  const bool supported =
      mode != "mutex" &&
      SUCCEEDED(D3D11CreateDevice(
          nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
          D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
          nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)) &&
      SUCCEEDED(device.As(&device5)) && SUCCEEDED(context.As(&context4)) &&
      SUCCEEDED(device5->CreateFence(0, D3D11_FENCE_FLAG_SHARED,
                                     IID_PPV_ARGS(&fence)));
  LOG(INFO) << "PRTS capture sync=" << (supported ? "fence" : "mutex")
            << " requested=" << (mode.empty() ? "auto" : mode);
  command_line.AppendSwitchASCII("prts-capture-sync",
                                 supported ? "fence" : "mutex");
  command_line.AppendSwitch("prts-remote");
  command_line.AppendSwitchASCII("headless", "new");
  browser_process_ = base::LaunchProcess(command_line, options);
  channel.RemoteProcessLaunchAttempted();  // Required on success AND failure.
  if (!browser_process_.IsValid()) {
    return false;
  }
  // Pass the real Chrome process handle: the Windows driver needs DUP_HANDLE
  // access to pull browser-owned handles received on this isolated connection.
  auto pipe = mojo::OutgoingInvitation::SendIsolated(
      channel.TakeLocalEndpoint(), {}, browser_process_.Handle());
  if (!pipe.is_valid()) {
    return false;
  }
  browser_.Bind(
      mojo::PendingRemote<prts::mojom::PrtsBrowser>(std::move(pipe), 0));
  browser_.set_disconnect_handler(
      base::BindOnce(&EndpointLink::OnDisconnect, weak_factory_.GetWeakPtr()));
  auto client = client_receiver_.BindNewPipeAndPassRemote();
  client_receiver_.set_disconnect_handler(
      base::BindOnce(&EndpointLink::OnDisconnect, weak_factory_.GetWeakPtr()));
  browser_->SetClient(std::move(client));
  return true;
}

prts::mojom::PrtsBrowser* EndpointLink::browser() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return browser_.is_bound() ? browser_.get() : nullptr;
}
void EndpointLink::Close() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  client_receiver_.reset();
  browser_.reset();
}
void EndpointLink::OnDisconnect() {
  Close();
  delegate_->OnDisconnect();  // Last operation: delegate may delete this.
}

}  // namespace prts_endpoint
