# S10a Mojo link and browser-controller draft

## Status and evidence

All changes are confined to `C:\src\tmp\m1\s10-link`. No builds, checkout mutations, commits, or repository edits were performed.

[V] = source/header read in `C:\src\chromium\src` at HEAD `1aa07b638799e59656559ae6c6649d4b197f555e`, version 157.0.8103.15 (`chrome/VERSION:1-4`). [I] = inference or integration requirement, not a compile/runtime result. Source paths below are relative to that checkout unless otherwise stated.

This is a link-only skeleton. Its headless hook supplies null delegates deliberately: Hello works, while window/capture/input/IME operations await the other M1 tasks. The Mojo contract is copied byte-for-byte, with no names or wire fields changed.

## File list

- `chrome/browser/prts/BUILD.gn`: Windows browser source_set plus independently reusable switches target.
- `chrome/browser/prts/mojom/BUILD.gn`, `prts.mojom`: shared generated bindings and unchanged supplied contract.
- `chrome/browser/prts/prts_switches.{h,cc}`: `prts-mojo`, `prts-capture-fps` constants.
- `chrome/browser/prts/prts_link.{h,cc}`: inherited endpoint recovery, inheritability clearing, isolated invitation acceptance, UI receiver/client bindings, disconnect cleanup.
- `chrome/browser/prts/prts_controller.{h,cc}`: PrtsBrowser implementation and delegate routing.
- `chrome/browser/prts/prts_delegates.h`: pure virtual interfaces for the other tasks, with explicit lifetime/sequence requirements.
- `prts_endpoint/endpoint_link.{h,cc}`: parent launcher, isolated sender, browser remote, endpoint-client receiver, single disconnect callback.
- `upstream-hooks.diff`: four upstream files; includes both browser aggregation and the direct dependency of headless_extra_parts.
- `a/`, `b/`: original/edited upstream copies used to make the diff. They are evidence, not extra files to install.
- `NOTES.md`: this file.

## 1. Invitation topology: endpoint sends; browser accepts; both brokers

**Decision:** initialize the standalone endpoint with `mojo::core::Init({.is_broker_process = true})`. The endpoint PARENT creates PlatformChannel, inherits its remote end into chrome.exe through the custom `--prts-mojo`, launches Chrome, then calls `OutgoingInvitation::SendIsolated(channel.TakeLocalEndpoint(), {}, chrome.Handle())`. Chrome calls `IncomingInvitation::AcceptIsolated` on the inherited end and exposes PrtsBrowser on the returned single pipe. The endpoint binds its PrtsBrowser remote to its returned pipe and calls SetClient.

- [V] API pairing and isolated-connection caveats: `mojo/public/cpp/system/invitation.h:115-144,199-202`. Only one initial pipe/one concurrent isolated connection per process pair; no application pipe-name token is needed.
- [V] Isolated sender requires a broker and connects TO_BROKER: `mojo/core/ipcz_driver/invitation.cc:242-269`. Isolated accept CHECKs broker status; normal accept CHECKs that the accepting node is NOT a broker: `:379-397`. The invitation direction does not have to follow the OS parent/child relationship.
- [V] Normal broker Send -> non-broker Accept is a viable alternative if Chrome sends and endpoint accepts. It is NOT valid to let the already-broker Chrome accept a normal invitation from its parent. This draft picks isolated so endpoint owns the only explicit target process handle and Chrome neither opens nor inherits a handle to its parent. Chrome keeps its independent child-process graph.
- [V] `content/app/initialize_mojo_core.cc:35-43,67` makes the browser a broker UNLESS `--disable-mojo-broker` or the standard PlatformChannel handle switch is present. Therefore use ONLY `--prts-mojo` for this link. EndpointLink rejects those standard flags and `--type`. Do not call PrepareToPassRemoteEndpoint's command-line overload: that inserts the standard switch. Use its string-returning LaunchOptions overload instead (`mojo/public/cpp/platform/platform_channel.h:74-98`).
- [V] The custom string uses the exact upstream signed decimal HANDLE encoding: `mojo/public/cpp/platform/platform_channel_endpoint.cc:100-105,190-205`. No unsigned/hex reinterpretation is needed.
- [V] Existing Mojo tests exercise isolated broker startup (`mojo/core/invitation_unittest.cc:348-350,651-676`), and MultiBrokerNetwork exercises shared-memory/OS-handle traffic involving a second broker (`:784-817,838-855`). These tests were read, NOT run.
- [V] `remoting/host/host_main.cc:342-353` chooses broker role explicitly. Its normal parent-send precedent is `remoting/host/win/wts_session_process_delegate.cc:545-559,589-592`; child acceptance is `remoting/host/desktop_process_main.cc:129-148`. No isolated Send/Accept precedent was found in remoting; the isolated choice follows Mojo's implementation/tests directly.
- [I] This selection differs from `remotekit/design/01-architecture.md:59-60`, which described browser-send / non-broker endpoint-accept. An integrator should update D3's two roles if adopting this spike. This task does not edit that design document.

## 2. Windows NT handle transfer and fallback

**Source-supported answer:** yes, a browser-owned D3D11 shared NT handle can be carried as `mojo::PlatformHandle` over the chosen connection, with the endpoint doing the browser -> endpoint duplication in the current initial topology. Actual D3D OpenSharedResource1 remains untested.

1. [V] `handle<platform>` generates `mojo::PlatformHandle`: `mojo/public/tools/bindings/generators/mojom_cpp_generator.py:1025-1026`. The wrapper takes ownership (`mojo/public/cpp/platform/platform_handle.h:29-47,61-65`) and Windows serialization is a normal transmissible handle (`mojo/core/ipcz_driver/wrapped_platform_handle.cc:151-189`).
2. [V] Endpoint Send clones its provided chrome process handle and stores it in its transport: `mojo/core/ipcz_driver/invitation.cc:234-239,305-315`; the clone preserves access rights (`mojo/core/platform_handle_utils.cc:38-45`). LaunchProcess returns its real process handle (`base/process/launch_win.cc:448-451,480`); do not replace it with a PID or a reduced QUERY-only handle. PROCESS_DUP_HANDLE is needed for pulling handles from Chrome.
3. [V] Ordinary isolated Accept creates the browser transport WITHOUT a remote process handle; only elevated Accept tries to open one (`mojo/core/ipcz_driver/invitation.cc:420-448`). Browser serialization therefore selects `HandleOwner::kSender` (`mojo/core/ipcz_driver/transport.cc:479-490`). It releases the source HANDLE into its serialized value (`:150-155`), and the endpoint uses its known chrome process handle in `DuplicateHandle(chrome_process, handle_value, GetCurrentProcess(), ..., FALSE, DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE)` (`:222-232`). The endpoint owns the local duplicate; Mojo consumes/closes the transmitted browser handle.
4. [V] If a sender DOES have a target process handle and the peer is not elevated, brokers normally duplicate INTO the recipient and mark `kRecipient` (`transport.cc:158-178,479-490`). The receiver must accept recipient-owned values: ordinary isolated broker peers do so unless explicitly untrusted/elevated (`transport.h:119-130`). No extra ELEVATED, SHARE_BROKER, or UNTRUSTED_PROCESS flag is needed for this same-user, same-integrity executable pair. Endpoint-to-browser transfers use the sender's known Chrome handle; Accept marks the inviting peer trusted (`invitation.cc:424-432`).
5. [V] `Transport::CanTransmitHandles()` allows a transport with a broker at either end (`transport.cc:788-795`). This is stronger pin-specific evidence than the generic public header's warning that isolated platform handles *may* not transfer (`invitation.h:123-128`).

**Fallback implementation decision:** no extra raw-handle flag or contract extension is necessary: step 3 IS the requested raw-value/pull-DuplicateHandle fallback, already implemented by this pin's Windows ipcz driver. The draft enables it by passing the live Chrome process handle to SendIsolated. There is no public switch to force HandleOwner in the driver, and adding a parallel raw-value method would change the shared contract and duplicate the existing mechanism.

[I] Contingency if the real texture test fails despite successful control traffic: add a separately reviewed/versioned frame-sink method carrying `(buffer_id, uint64 browser_handle, coded_size)` with an acknowledgement. Browser retains an owning NT handle until that acknowledgement; endpoint validates non-sentinel values and DuplicateHandles OUT of `browser_process()` with SAME_ACCESS and non-inheritable destination, without CLOSE_SOURCE; browser closes its retained handle after ack, cancellation, or disconnect. Do not send a handle value owned only by the GPU process. This contingency is designed here but not implemented because native Mojo handle transfer already follows this route and there is no capture implementation in this task.

[I] Capture integration must create/duplicate a handle owned in the browser process before moving it into NewBuffer. Keep any separate pool owner handle alive as needed by capture. Send NT handles from CreateSharedHandle, never legacy DXGI shared-handle tokens or pseudo-handles. Endpoint should cache/open each buffer once and release on Reset. These are capture/endpoint responsibilities, not link logic.

## 3. Standalone endpoint Mojo initialization

EndpointLink does NOT initialize process-global facilities itself: the endpoint's main owns them once, together with WebRTC's other services. Required order for the other endpoint task:

1. Create `base::AtExitManager`, initialize CommandLine, then create a production `base::SingleThreadTaskExecutor` on the endpoint main sequence (DEFAULT is enough for these bindings; choose UI only if the rest of endpoint needs it). Start `base::ThreadPoolInstance::CreateAndStartWithDefaultParams("PrtsEndpoint")`.
2. Call `mojo::core::Init({.is_broker_process = true})` ONCE before using PlatformChannel/invitations. Never call this inside the browser; Content already initialized its Mojo core. Both nodes use local shared-memory allocation as brokers (`mojo/core/embedder/embedder.cc:64-75`). No test TaskEnvironment is appropriate for the executable.
3. Start a dedicated `base::Thread io_thread("PrtsMojoIO")` using a `base::Thread::Options` with `message_pump_type = base::MessagePumpType::IO`; check StartWithOptions. Create `mojo::core::ScopedIPCSupport(io_thread.task_runner(), ShutdownPolicy::FAST)` before EndpointLink. Keep the IO thread running while Mojo objects exist.
4. Implement `EndpointLink::Delegate` (all PrtsBrowserClient notifications plus OnDisconnect), create EndpointLink on that same main sequence, then Launch a command line with chrome.exe and a unique `--user-data-dir`. Run a `base::RunLoop`; delegate OnDisconnect should stop capture/frame sinks, cancel frame acknowledgements, and invoke the owner's quit closure. Hello(version=1) is successful handshake; Launch returning true means only launched/bound. Apply a handshake deadline in the endpoint owner.
5. Close/destroy EndpointLink and every other Mojo binding before releasing process facilities. If explicitly shutting down Mojo, use `mojo::core::ShutDown()` while the IO thread still runs; never use it per connection or from a library sharing Mojo with other services. Then destroy ScopedIPCSupport, stop/join the IO thread, and shut down ThreadPool on its starting sequence. AtExitManager outlives these services.

[V] Initialization headers: `mojo/core/embedder/configuration.h:17-33`; `embedder.h:25-37` (Init/ShutDown); `scoped_ipc_support.h:21-28,95-115`; `base/task/single_thread_task_executor.h:27-58`; `base/threading/thread.h:89-106,147-162,197-215,241-245`; `base/task/thread_pool/thread_pool_instance.h:148-165,190-211,245`; `base/run_loop.h:85,146`; `base/at_exit.h:20-48`; `base/command_line.h:116-129`.

[V] At this pin ScopedIPCSupport's shutdown-policy argument has NO effect: its header says legacy Mojo is gone (`scoped_ipc_support.h:24-28`), implementation only installs the IO runner and has a default destructor (`scoped_ipc_support.cc:12-18`). Do not claim FAST/CLEAN will flush frame acks or close the ipcz node. Explicit ShutDown destroys the process node (`mojo/core/embedder/embedder.cc:83-85`, `mojo/core/ipcz_api.cc:56-59`).

[V] Production precedents: `remoting/host/desktop_process_main.cc:86-110,129-148` (ThreadPool, task executor, IO thread, ScopedIPCSupport, Accept); `remoting/host/peer_connection_process_main.cc:45-64` (IO main/IO thread pattern); `remoting/host/remote_open_url/remote_open_url_main.cc:41-44,60-63` (AtExitManager/executor/Init/IPC support). None uses a test TaskEnvironment.

## API audit for delivered C++

| API used | Header/source read at pin [V] |
| --- | --- |
| PlatformChannel construction, TakeLocalEndpoint, string-returning PrepareToPassRemoteEndpoint, RemoteProcessLaunchAttempted, RecoverPassedEndpointFromString | `mojo/public/cpp/platform/platform_channel.h:43-71,74-98,110-119` |
| PlatformChannelEndpoint validity and platform_handle | `mojo/public/cpp/platform/platform_channel_endpoint.h:44-70` |
| Windows PlatformHandle::GetHandle and ScopedHandle::get | `mojo/public/cpp/platform/platform_handle.h:129-142`; `base/win/scoped_handle.h:81,107-110` |
| SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0) | Native Windows API, not a Chromium declaration: pin's exact clear call `base/test/launcher/test_launcher.cc:546`; flag-setting call `base/process/launch_win.cc:336-340`. Also read installed SDK `C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/um/handleapi.h:83-90` (SDK evidence, not pin evidence). |
| CommandLine::ForCurrentProcess, HasSwitch, GetSwitchValueASCII, AppendSwitch, AppendSwitchASCII, RemoveSwitch | `base/command_line.h:129,198-223` |
| base::LaunchOptions default inheritance whitelist, LaunchProcess | `base/process/launch.h:151-174,391-392` |
| base::Process construction, move assignment, IsValid, Handle, destruction policy | `base/process/process.h:61-73,104-109` (destructor does not terminate) |
| OutgoingInvitation::SendIsolated / IncomingInvitation::AcceptIsolated | `mojo/public/cpp/system/invitation.h:139-144,199-202` |
| PostTaskAndReplyWithResult and MayBlock | `base/task/thread_pool.h:140-167`; `base/task/task_traits.h:165` (reply returns to posting UI sequence) |
| PendingReceiver(pipe), PendingRemote(pipe, version), is_valid | `mojo/public/cpp/bindings/pending_receiver.h:66-97`; `pending_remote.h:65-107` |
| Receiver(impl), Bind, BindNewPipeAndPassRemote, set_disconnect_handler, reset | `mojo/public/cpp/bindings/receiver.h:53-74,88-97,112,128-145,163-185` |
| Remote::Bind, get/operator->, is_bound, set_disconnect_handler, reset | `mojo/public/cpp/bindings/remote.h:98-120,149-152,218-220,280-302` |
| WeakPtrFactory constructor/GetWeakPtr/InvalidateWeakPtrs | `base/memory/weak_ptr.h:423-444,464-466` |
| BindOnce/Unretained, OnceClosure bool and consuming Run | `base/functional/bind.h:58-65,92-115`; `base/functional/callback.h:110,133-150` |
| UI DCHECK / endpoint sequence checker | `content/public/browser/browser_thread.h:28-35`; `base/sequence_checker.h:75-83` |
| Generated PrtsBrowser virtual signatures, struct pointers, pending remotes, const string references | `mojo/public/tools/bindings/generators/cpp_templates/interface_declaration.tmpl:92,112`; `interface_macros.tmpl:3-8`; `mojom_cpp_generator.py:957-960,992-995,1008-1014,1152-1157,1168-1182` |
| Generated HelloInfo::New() | `mojo/public/tools/bindings/generators/cpp_templates/wrapper_class_declaration.tmpl:23-27` |
| PostBrowserStart / PostMainMessageLoopRun overrides | `chrome/browser/chrome_browser_main_extra_parts.h:43-47` |
| ui::IsPrtsRemoteMode() | [I] Required patch-01 API supplied by the task; ABSENT in pristine pin's `ui/base/ui_base_switches.h`. Build requires patch 01 first. No substitute function invented here. |

## Exact integration steps

1. Install patch 01 first: it must declare/define/export `ui::IsPrtsRemoteMode()` from `ui/base/ui_base_switches.h` for Windows and the ui/base target. This draft intentionally does not duplicate that upstream patch.
2. Copy the new `chrome/browser/prts/` directory to the same path in an integrator-owned writable checkout. `prts.mojom` remains identical to `C:\src\tmp\m1\contract\prts.mojom`. Generated include path is `chrome/browser/prts/mojom/prts.mojom.h`, target `//chrome/browser/prts/mojom:mojom`.
3. Apply `upstream-hooks.diff` in that writable integration checkout. The four edited upstream files are `chrome/browser/BUILD.gn`, `chrome/browser/headless/BUILD.gn`, and `chrome/browser/headless/chrome_browser_main_extra_parts_headless.{h,cc}`. Windows-only `//chrome/browser/prts:prts` is referenced as `//chrome/browser/prts`. The browser core's dependency is explicit aggregation; headless_extra_parts' direct dependency is necessary for GN include checking. There is no reverse dependency on browser core, so no target cycle.
4. Exact lifetime: Headless extra parts is already added by `chrome/browser/chrome_browser_main.cc:851-854`. PostBrowserStart is dispatched on UI at `:1725-1729` after startup (`:2163`); PostMainMessageLoopRun dispatch is `:2255-2256`, before subsequent browser service teardown. New executable hooks each check ui::IsPrtsRemoteMode(); added includes/declarations are Windows compile guards and carry PRTS comments. GN comments use `# PRTS:` because GN cannot use `//` comments.
5. Replace the no-argument controller construction in the PostBrowserStart hook with `std::make_unique<prts::PrtsController>(prts::PrtsDelegates{.windows = ..., .capture = ..., .input = ..., .ime = ...})` when other tasks land. Concrete PrtsWindowRegistry/PrtsCapture/PrtsInput/PrtsImeBridge implement the correspondingly named *Delegate interfaces. Owners MUST outlive controller; if extra-parts members, declare owners before controller and reset controller before owners in PostMainMessageLoopRun. Delegates receive/call UI-bound client proxies only on UI; post worker results back to UI. Capture moves PendingRemote<PrtsFrameSink> to its worker before binding.
6. Registry SetClient(non-null) enumerates existing windows because PostBrowserStart runs after startup window creation; IME SetClient publishes current state. Hello is sent first. FillHelloInfo supplies real GPU/software facts before Hello; the link-only default is version=1, gpu_luid=0, software_compositing=true. Input and IME validate values/target identity in their implementations. Capture validates FPS and owns `--prts-capture-fps` policy; this task defines the switch but adds no capture policy.
7. Place `prts_endpoint/endpoint_link.{h,cc}` into the other task's `//prts_endpoint` directory. Add these sources to its existing executable target and add direct deps: `//base`, `//chrome/browser/prts:switches`, `//chrome/browser/prts/mojom`, `//mojo/core/embedder`, `//mojo/public/cpp/bindings`, `//mojo/public/cpp/platform`, `//mojo/public/cpp/system`. Never depend on the browser controller source_set from endpoint. Implement main's facilities as section 3, and implement Delegate and frame-sink receivers in the rest of endpoint.
8. Shared mojom deps are `//mojo/public/mojom/base` and `//ui/gfx/geometry/mojom`; verified target definitions are `mojo/public/mojom/base/BUILD.gn:8` and `ui/gfx/geometry/mojom/BUILD.gn:10`. The browser source_set's direct/public deps are recorded in its supplied BUILD.gn. Existing bindings exports system dependencies (`mojo/public/cpp/bindings/BUILD.gn:273-280`). No root BUILD.gn changes are part of this task.

## Validation and remaining integration proof

- `git -C C:\src\chromium\src apply --check C:\src\tmp\m1\s10-link\upstream-hooks.diff` passed against pristine pin. Regenerate/recheck after any hook edit. This check does NOT validate new-file compilation or patch-01 availability.
- Copied the pin's clang-format executable temporarily into the task directory, formatted only draft/new b/ C++ copies with `-style=Chromium`, then removed the temporary executable. No formatter ran with write targets in upstream.
- No gn/ninja/siso, compiler, unit test, or live Chrome/endpoint run was attempted; the upstream build continues undisturbed. All external C++ APIs were reviewed against pin headers/templates as listed above; the runtime predicate is explicitly a patch-01 prerequisite.
- [I] Required S10a runtime proof after integration: both nodes start with the stated broker roles; endpoint receives exactly one Hello; endpoint passes a frame-sink remote to browser; browser delivers a real shared NT texture handle; endpoint opens it on matching adapter; both directions' disconnection resets state and normal Chrome child-process Mojo continues to function. This draft does not claim those tests passed.

## Open risks

- [I] Public isolated API documentation limits forwarding pipes across independent graphs. The delivered contract terminates all PRTS pipes in the two processes, with worker threads allowed. D3's optional direct GPU->endpoint capturer remote is a separate topology spike; do not assume this draft proves it. Mojo's MultiBrokerNetwork test is promising evidence, not coverage of Viz/D3D capture.
- [I] Same account/integrity, unsandboxed endpoint, normal non-elevated LaunchProcess are assumptions. An endpoint sandbox or changed process rights can break sender-owned handle pulls. No broad trust/elevation flags are added to compensate.
- [I] The inherited handle is cleared when controller Start runs in PostBrowserStart, not at process entry. Chromium already whitelists child handle inheritance (`base/process/launch.h:151-174`); if another integration adds broad-inherit launch before this hook, move only handle recovery/clearing to an earlier headless phase and retain UI controller binding at PostBrowserStart.
- [I] Process singleton forwarding can redirect launch to an existing Chrome and lose the inherited channel. Caller must supply a fresh profile; successful Launch is not successful Hello. Handshake timeout, process monitoring, Job Object policy and Chrome termination are owned by the endpoint main/supervisor. Close/destruction intentionally only closes bindings/process handle.
- [I] Null delegates are the intended spike default. Repeated SetClient is ignored (first client is retained), and no in-place reconnection is supported. New EndpointLink/browser session is required after disconnection. FrameSink disconnect handling and completion of pending frame acks belong to capture/endpoint implementations.
- [I] Delegate cleanup must tolerate already-closed UI windows; StopAll must release capture callbacks even if Mojo Reset cannot be delivered after disconnect. SetClient(nullptr) must not use the already-reset proxy. Those semantics are declared in prts_delegates.h but await implementations.

Code size: 708 new-file lines plus 57 upstream added lines = 765, including comments, blank lines, contract and GN; original a/b file contents counted only through their added diff lines. Final apply check with --whitespace=error passed; task-copy git diff --no-index --check emitted no whitespace diagnostics (exit 1 denotes differing copies). Contract SHA256 equals the supplied contract.
