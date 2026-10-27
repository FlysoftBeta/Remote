# Task s10-link (timeout 40 min): Mojo link browser <-> prts_endpoint (spike S10a) + browser controller skeleton
Output dir: C:\src\tmp\m1\s10-link\

Context: prts_endpoint.exe (in-tree executable) creates a mojo::PlatformChannel, launches chrome.exe passing one end on the command line
(`--prts-mojo=<handle value>`, inherited via base::LaunchOptions handles_to_inherit) plus `--prts-remote --headless=new`. The browser then
connects and exposes prts.mojom.PrtsBrowser; the endpoint implements PrtsBrowserClient and PrtsFrameSink (see contract).
Chrome's browser process is already a Mojo broker for its own children.

Questions to answer in NOTES.md with source citations at the pin (mojo/public/cpp/system/invitation.h, mojo/core/ipcz_driver/*,
mojo/core/embedder/*, remoting/ for precedents):
1. Which invitation mode works between the Chrome browser (broker) and a separately launched endpoint: `SendIsolated`/`AcceptIsolated`
   (both brokers) vs normal Send/Accept with the endpoint as non-broker. Who sends, who accepts. Pick one and justify
   (the endpoint is the PARENT process here).
2. Can a `mojo::PlatformHandle` (a D3D11 shared NT handle) be sent browser->endpoint over that connection on Windows (ipcz driver handle
   transfer: which side duplicates, does it need a target process handle / trust flags)? If uncertain, design the fallback: send the raw
   handle value and let the endpoint `DuplicateHandle` it out of the browser process (endpoint owns the chrome.exe process handle).
   Make the code support the fallback behind a flag if cheap.
3. Endpoint-side Mojo init in a standalone in-tree executable: mojo::core::Init config, IO thread, ScopedIPCSupport, task environment
   (base::SingleThreadTaskExecutor + ThreadPool), as remoting or other in-tree tools do it.

Deliverables (draft code, must compile at the pin as far as you can verify by reading headers):
- chrome/browser/prts/mojom/BUILD.gn + prts.mojom (copy contract; adjust only if needed, explain).
- chrome/browser/prts/prts_switches.{h,cc} (`--prts-mojo`, `--prts-capture-fps`), prts_link.{h,cc}: browser side, reads the switch,
  connects, binds the PrtsBrowser receiver on the UI thread, holds the client remote, clears handle inheritability.
- chrome/browser/prts/prts_controller.{h,cc}: owns PrtsLink; implements PrtsBrowser by delegating to (to-be-written) PrtsWindowRegistry,
  PrtsCapture, PrtsInput, PrtsImeBridge. Declare the delegate method names you call in a header `prts_delegates.h` (pure virtual
  interfaces); other tasks implement them. Lifetime: created from ChromeBrowserMainExtraPartsHeadless (find the exact hook:
  PostBrowserStart / PostMainMessageLoopRun) only when ui::IsPrtsRemoteMode().
- upstream-hooks.diff (a/b style): chrome/browser/BUILD.gn sources/deps addition (or a prts BUILD.gn source_set added as dep), the headless
  extra-parts hook; verified with `git apply --check`.
- endpoint side: endpoint_link.{h,cc} (launch chrome with the channel, accept/send invitation, bind remotes, OnDisconnect handling;
  exposes a C++ delegate for the rest of the endpoint), as standalone files for //prts_endpoint (another task writes the rest).
- NOTES.md answering 1-3 with citations, plus risks.
Scope limits: no capture/input/webrtc logic. Max ~900 lines of code total.
