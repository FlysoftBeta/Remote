# re-mojo

Windows x64, Rust 1.88+. Safe wrapper for the existing `rb.mojom` contract through
`re_native.dll`. No build script, GN invocation, C++ ABI or Mojo dependency in Cargo.
The checked-in C declarations live only in `src/ffi.rs`.

From a prepared Chromium checkout (`src/re_native` links to `re/native`):

```powershell
# From remotekit; incremental only. Shared native target includes the RTC shim.
.\browser\cr.cmd build dev "re_native chrome rb_legacy_endpoint"
cargo build --manifest-path re/Cargo.toml -p re-mojo --example re-mojo-probe
re/target/debug/examples/re-mojo-probe.exe --dll C:/src/chromium/src/out/dev/re_native.dll --chrome C:/src/chromium/src/out/dev/chrome.exe --profile C:/src/tmp/m6/ipc/probe-profile --frames 600 --csv C:/src/tmp/m6/ipc/probe.csv
```

The DLL directory must contain its Chromium component dependencies for out/dev.
The Windows loader searches that directory explicitly. Non-component distribution
uses the same API with dependencies linked into the DLL. Cargo does not build it.
Both common ABI and Mojo API must be version 1. A mojom change rebuilds the DLL;
an incompatible C API change increments its version and updates checked-in bindings.

`Client::load` creates the owner-thread task executor and isolated-Mojo runtime.
Pass UTF-8 Chrome argv, including a dedicated `--user-data-dir`, to `launch`.
Poll promptly, observe protocol-1 hardware `Hello` and `WindowAdded`, then call
`start_capture(window, fps, false)` outside the callback. The shim preserves all
windows and does not choose an active window or start capture implicitly.
`Pointer`, `Wheel`, `Key`, resize, focus, IME, touch, new-window and refresh map
directly to the existing mojom. `stats` and `now_us` are local bridge probes.

`Client` stays on its creation thread. All application callbacks run in `poll`;
each callback borrows its strings, metadata and NT handles. Import the texture
once per buffer and shared fence once per token. Never close borrowed handles.
Wait for the frame's fence value on the matching D3D adapter before reading, or
use keyed mutex key 0 if there is no fence. Frame callbacks allocate no storage.

Return `FrameAction::Consumed` only when no GPU read remains. For asynchronous
GPU work, retain `frame.id`, return `Defer`, and call `release_frame(id, true)`
after GPU completion. Use `Abandon` / false when a submitted read cannot safely
be completed. An ordered `Reset` invalidates that capture's handles, imports and
pending IDs; `Recovered` identifies its first replacement frame. The wire
contract does not distinguish GPU restart from resize or initial pool setup.
The shim supports 16 captures, each with 64 buffers, 8 fences and 64 leases.

Drop/shutdown closes Mojo and terminates only Chrome launched by that client.
The common native runtime and DLL remain alive until process exit. Callback
panics are caught at the FFI boundary and resumed after `poll` returns.

The probe validates real NT handles, prints texture sizes, fence tokens/values,
capture-to-receive latency and shim-to-Rust overhead. It acknowledges without
reading GPU memory. Pointer/wheel/key input must update the page title; resize
must produce a new pool. `--sync mutex` tests the negotiated fallback;
`--sync fence` explicitly requests the shared-fence path. `--timeout` defaults
to 180 seconds, allowing cold component startup.
For transport timing with the same page as the legacy harness, pass
`--url http://127.0.0.1:PORT/tools/latency/remote-page.html?scenario=marker --steady --size 1904x1024`.
This disables the probe page's title-based input assertion and the resize;
the default probe remains the bidirectional input/recovery acceptance check.
