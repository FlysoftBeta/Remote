# M4 shared D3D11 fence investigation

**GO for an opt-in D3D11 implementation on this host.** Driver support and copy -> GPU Wait -> video processing -> oneVPL encode ordering passed, including decoded-content checks. This is an implementation gate, not an end-to-end latency acceptance pass. The proposed first implementation retains the endpoint's private BGRA copy and uses asynchronous GPU-read completion followed by `Done()` for capture-buffer reuse.

2026-10-26. Artifacts: `C:/src/tmp/m4/shared-fence/`. [V] = local source read / measurement; [I] = inference / proposed design. Chromium inspected at `d511e7e648162f4df099eb5c1e2e25ebab3fe411`, above the supplied Chromium 157 base pin `1aa07b638799e59656559ae6c6649d4b197f555e`. Chromium paths below are relative to `C:/src/chromium/src`; endpoint paths are relative to remotekit. No Chromium/endpoint edits, builds, browser/endpoint launches, commits, or `out/rel` operations were performed by this task.

## Probe

[V] `probe.cc` / `build.cmd` build a standalone EXE using VS 2026 MSVC 14.51.36231 and Windows SDK 10.0.26100.0. It reads current `endpoint/third_party/libvpl/api` headers and links the previously built standalone MSVC dispatcher `C:/src/tmp/s7/build/vpl.lib`, never Chromium build output. Explicit compiler/include/library paths bypassed a stalled vcvars bootstrap (`vswhere.exe` missing). Warnings: vendored anonymous structs and CRT fopen deprecations.

[V] The producer and consumer are **different processes with independently created D3D11 devices**, both Intel UHD Graphics 630, PCI `8086:9bc5`, LUID `00000000:0000e6f6`, driver **30.0.100.9805**. Both devices have BGRA/video support and multithread protection.

| Capability / operation | Observed |
| --- | --- |
| `D3D11_FEATURE_D3D11_OPTIONS5` | `S_OK`, `SharedResourceTier=3` |
| `IDXGIAdapter4::GetDesc3` monitored-fence flag | 1 |
| Device5 / Context4 interfaces | Available on both devices |
| `CreateFence(0, D3D11_FENCE_FLAG_SHARED)` | `S_OK` |
| Fence CreateSharedHandle / child OpenSharedFence | Successful |
| Shared NT BGRA texture, no keyed mutex; child OpenSharedResource1 | Successful |
| GPU Wait / Signal and direct shared-texture BGRA->NV12 VideoProcessorBlt | Successful |
| oneVPL hardware AVC with supplied consumer device | API 1.35; SetHandle / Init successful |

[V] Each 640x368 frame contains nine vertical black/white binary bars encoding frame numbers 1-500 uniquely. The child queues **Wait and VPP before the parent is allowed to submit**. Parent uploads a private source, runs two 2048x2048 fullscreen draws with 32 dependent shader iterations, copies into the shared texture, Signals N and Flushes. There is no producer CPU completion wait. The child checks every raw NV12 Y/UV byte (limited-range black/white/neutral chroma; tolerance 2), checksum and extracted tag. The single capture slot is reused after child readback: this tests incoming readiness, not asynchronous pool reuse.

[V] In encode mode VPP writes private NV12 on the same device supplied to `MFXVideoCORE_SetHandle`. EncodeFrameAsync is called **before** Map/GetData/CPU completion waits. Encoder: AsyncDepth=1, low-power AVC constrained baseline, CQP=10, all IDR for independent content checks. A separate invocation hardware-decodes the resulting elementary stream to system memory and checks count, tag, luma/chroma (lossy tolerance 12, excluding bar edges) and checksum. Successful encoder submission alone is not the ordering evidence.

| Artifact directory | Test | Result |
| --- | --- | --- |
| `vpp500/` | 500 loaded frames, Wait, VPP directly from shared texture | **500/500 raw pass**, 500 waits queued before Signal |
| `encode500/` | Same plus immediate oneVPL encode; no timing echo | **500/500 raw and decoded pass** |
| `encode500-echo/` | Same plus return-fence timing echo | **500/500 raw and decoded pass**; 499 encode calls began while ready fence was incomplete |
| `baseline500/` | 500 frames, no heavy draws, Wait+encode+echo | **500/500 raw and decoded pass**; 497 pre-completion encode calls |
| `negative/parent2.log` | 50 loaded frames, incoming Wait removed | **50/50 raw failures**, expected child exit 20 |
| `negative-encode/` | 50 loaded frames, incoming Wait removed, encode enabled | **50/50 raw and decoded frames contain N-1**; 50 pre-completion encode calls |

[V] The negative controls establish that the workload exposes stale data. An initial control had a probe coordination timeout: the fast unsynchronized child coalesced auto-reset events. The final probe includes a **post-readback** acknowledgement, and `negative/parent2.log` is the valid rerun. That acknowledgement does not precede VPP/encode or repair their missing GPU dependency.

### Timings and limits

[V] Echo instrumentation: producer timestamp immediately before Signal; consumer timestamp immediately after Wait followed by Signal on an echo fence; producer waits for echo and records a second timestamp. Final runs have no disjoint intervals and matching 12,000,048 Hz query frequencies.

| Measurement, ms | No-heavy-draw P50 / P80 / P95 / P99 | Loaded encode P50 / P80 / P95 / P99 |
| --- | --- | --- |
| Raw cross-device pre-Signal -> post-Wait GPU marker delta | .201 / .242 / .340 / .384 | .304 / 1.667 / 4.427 / 8.092 |
| Same-producer-context Signal->echo round trip | .389 / .441 / .557 / .703 | 1.336 / 8.813 / 17.024 / 19.269 |
| Producer draws+copy GPU interval | .065 / .093 / .094 / .095 | 30.056 / 36.684 / 38.871 / 40.282 |
| CPU Wait call duration | .087 / .120 / .132 / .539 | .135 / .298 / 1.078 / 10.681 |
| CPU Signal issue -> readback done | 3.469 / 3.939 / 4.214 / 10.771 | 44.124 / 51.928 / 66.348 / 84.361 |

[I] The raw cross-device delta is an **estimate**, not calibrated one-way GPU latency: this probe has no D3D11 cross-device clock-calibration guarantee. The same-context echo interval avoids that assumption and gives a forward-handoff upper bound including return scheduling/queue overhead. CPU Wait time is submission cost, not GPU completion latency. Markers around VideoProcessorBlt run on the 3D context and are not direct video-engine timings. Signal-to-readback includes producer backlog, VPP, encode sync, staging/Map and CPU scheduling. The baseline overlapped the short negative-encode control; no isolation or performance comparison is claimed. Loaded tails require real-chain measurement before default enablement.

[V] Microsoft specifies immediate-context Wait as ordering subsequent work behind a fence value: [ID3D11DeviceContext4::Wait](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_3/nf-d3d11_3-id3d11devicecontext4-wait). The content tests supply additional host-specific cross-engine evidence.

Reproduction, from a fresh subdirectory under the task directory:

```powershell
cmd /c C:\src\tmp\m4\shared-fence\build.cmd
..\probe.exe 500 2 1 > parent.log 2>&1
..\probe.exe --decode encoded.h264 > decode.log 2>&1
# Expected failures, in another subdirectory:
..\probe.exe 50 2 1 1 > parent.log 2>&1
..\probe.exe --decode encoded.h264 > decode.log 2>&1
```

Outputs: `parent.log`, `child.log`, `child-errors.log`, `timings.csv`, `encoded.h264`, `decode.log`, `decoded.csv`. Task-root `summary.json` independently counts tag/checksum failures and timestamp validity; `source-evidence.txt` / `source-hashes.json` preserve numbered excerpts and source identities. `summarize.cjs` and `capture-evidence.cjs` regenerate those summaries. The final executable includes echo timing; earlier no-echo artifacts remain separately identified.

## Chromium path at the pin

| Area | Verified evidence |
| --- | --- |
| Fence wrapper | [V] `ui/gfx/win/d3d_shared_fence.cc:47` creates a shared D3D11 fence; `:102` exports its handle; `:118` / `:151` check Device5 plus monitored fences; `:251` / `:268` / `:290` open per waiting device and issue Wait; `:299` / `:310` increment uint64 and Signal; `:245` only raises values. Header `ui/gfx/win/d3d_shared_fence.h:21` requires external synchronization. Snapshot the exact per-frame value. |
| Backing synchronization | [V] `gpu/command_buffer/service/shared_image/d3d_image_backing.h:291` uses cross-device fences for shared handles **without keyed mutexes**. `d3d_image_backing.cc:1233` gathers write/read fences; `:1523` ends Dawn access; `:1539` exports/caches DXGI fences; `:1704` waits pending fences; `:1756` / `:1775` signal at access end. These do not automatically reach the endpoint. |
| GMB allocation | [V] `gpu/command_buffer/service/shared_image/d3d_image_backing_factory.cc:351`, `:396`, `:410` allocate/export capture GMB textures with `SHARED_NTHANDLE | SHARED_KEYEDMUTEX`. `:743` is a **different**, internally allocated shared-image branch already choosing fences. Changing that branch alone does not affect capture GMBs. `:70` / `:128` show a separate IO-owned allocator device. |
| Capture pool | [V] `components/viz/service/frame_sinks/gmb_video_frame_pool_context_provider_impl.cc:62` creates the shared image, `:111` forces native handles; `video_capture/mappable_shared_image_video_frame_pool.cc:62` clones the native GMB and `:80` recreates the pool. `gpu/command_buffer/client/shared_image_interface.h:143` offers an existing-buffer import preserving client GMB access. |
| Handle transport | [V] `ui/gfx/gpu_memory_buffer_handle.h:74` and `ui/gfx/mojom/native_handle_types.mojom:72` contain texture handle, texture token and optional shared memory, **no fence/value**. `ui/gfx/mojom/gpu_fence_handle.mojom:8` carries a Windows HANDLE without a timeline value. A texture token is not readiness. |
| Capture Mojo | [V] `media/capture/mojom/video_capture_buffer.mojom:17` VideoFrameInfo has no fence; `:52` contains exported SharedImage+Chromium SyncToken; `:57` also has the native-GMB union arm used here. SyncToken is not an endpoint-importable D3D11 fence. `services/viz/privileged/mojom/compositing/frame_sink_video_capture.mojom:61` delivers data/info/callbacks; `:18` defines Done. A new optional payload can carry the fence. |
| RGBA / NV12 copy | [V] `components/viz/service/display_embedder/skia_output_surface_impl_on_gpu.cc:1073` waits for completion for mappable blits; `:1086` makes ReadbackContextTexture; `:1092` invokes FlushAndSubmitCopyOutput. The immediate-result branch at `:1099` ends write access before sending. `:1206` registers the finished callback, flushes/inserts recordings and conditionally submits. NV12 equivalents: `:1496`, `:1522`. |
| Callback / polling | [V] `components/viz/service/display_embedder/skia_render_copy_results.cc:179` / `:185` / `:192` delivers at completion. `components/viz/common/frame_sinks/copy_output_request.cc:133` / `:141` posts to the capturer runner. `skia_output_surface_impl_on_gpu.cc:2521` / `:2530` polls, `:2730` pumps Graphite/Ganesh async work. `skia_output_surface_dependency_impl.cc:168` / `:170` schedules checks at **2 ms**. These functions contain no explicit Present wait. |
| Writer device | [V] `gpu/command_buffer/service/dawn_context_provider.cc:893` selects ANGLE's adapter; `:901` / `:904` supply ANGLE's actual D3D11 device to Dawn D3D11. `:1404` exposes that device. D3D12 has a different queue accessor. `d3d_image_backing.h` documents separate texture/ANGLE devices. Backend identity must be checked. |
| Deferred submission | [V] `gpu/command_buffer/service/shared_image/shared_image_representation.cc:257` allows deferred Graphite submit; `d3d_image_backing.cc:1490` can keep persistent access open without an EndAccess fence. `gpu/command_buffer/service/graphite_shared_context.cc:525` submits; `:637` / `:644` additionally flushes the backend; `dawn_context_provider.cc:585` handles delayed D3D11 flush. EndAccess alone or allocator-device Signal is insufficient. |
| Delivery / lifetime | [V] `components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.cc:1274` handles results; `:1479` stamps capture-end and orders delivery; `:1524` does oracle completion; `:1577` clones handles; `:1594` creates the lease; `:1613` sends frames. `in_flight_frame_delivery.cc:19` / `:23` makes destruction invoke Done. `frame_sink_video_capturer_impl.cc:1668` caches a marked frame; `:1706` only invalidates the pool. Abandonment must also clear marked-frame resurrection. |
| Browser bridge | [V] `chrome/browser/prts/prts_capture.cc:81` receives frames; `:95` keeps the Viz lease in a default-invoked reply closure; `:139` registers textures once; `:153` sends per-frame data. `chrome/browser/prts/mojom/prts.mojom:42` NewBuffer has texture only; `:45` OnFrame has an empty reply. There are **two Mojo legs**, Viz->browser frame runner->endpoint. |
| Endpoint today | [V] `endpoint/d3d_native_frame.cc:202` acquires key 0 (20 ms timeout); `:213` copies to private BGRA; `:214` releases key 0; `:231` queues VPP into private NV12, query and Flush. `endpoint/d3d_frame_source.cc:263` converts and `:271` immediately replies, relying on keyed exclusion. Both native and CPU fallback need changed source-release semantics without the mutex. |
| oneVPL order | [V] `endpoint/vpl_encoder.cc:562` adopts/opens the device; `:694` supplies it to oneVPL; `:1014` submits encode. `:451` observes VPP completion for diagnostics, without a required pre-encode CPU wait. The probe reproduces same-device ordering, not WebRTC scheduling/all fallbacks. |

**API trap:** [V] The Windows `SharedImageInterface::UpdateSharedImage(...D3DSharedFence...)` declaration exists, but `gpu/command_buffer/client/shared_image_interface.cc:146` implements it as `NOTIMPLEMENTED_LOG_ONCE()`. The capture pool's in-process interface does not override it; its ordinary acquire-fence overload `DCHECK(!acquire_fence)` (`gpu/command_buffer/service/shared_image_interface_in_process_base.cc:396`). Returning a release fence requires real plumbing, not merely calling that overload.

## Concrete first implementation

[I] **Negotiate narrowly.** Add `BufferFormatPreference::kPrtsSharedFence` to `services/viz/privileged/mojom/compositing/frame_sink_video_capture.mojom`. Accept only on Windows with `ui::IsPrtsRemoteMode()`, explicit `--prts-shared-fence`, hardware Graphite/Dawn **D3D11**, and successful actual fence creation/export. Browser `PrtsCapture::Session` requests this for compatible native capture; otherwise use existing mappable/keyed capture. Limit v1 to ARGB/BGRA. Unsupported device/format or software encoder fallback must rebuild/renegotiate ordinary capture. All upstream hooks need `// PRTS:` and mode/switch guards.

[I] **Allocate only this pool without keyed mutexes.** Propagate an opt-in bool through MappableSharedImageVideoFramePool -> GmbVideoFramePoolContextProvider::CreateContext -> implementation. Extend D3DImageBackingFactory::CreateGpuMemoryBufferHandle / CreateGpuMemoryBufferHandleOnIO with a default-false option, threaded through the IO hop. After capability checks allocate `SHARED_NTHANDLE | SHARED` for the opted-in pool. Preserve ordinary callers. GmbVideoFramePoolContext::CreateSharedImage explicitly allocates this GMB and imports through the SII overload taking buffer usage + handle. Existing D3DImageBacking then uses its no-keyed-fence interop. Do not globally change all remote-mode GMBs or infer capture from SCANOUT_CPU_READ_WRITE. Do not mix modes within a pool.

[I] **Signal at the actual submission boundary.** Add `BlitRequest::prts_submit_with_fence`, set only by the negotiated capturer. In CopyOutputRGBAInTexture, after draw/scaling/letterbox/overlays and FlushSurface recording insertion:

1. Force `graphite_shared_context()->submitAndFlushBackend(SyncToCpu::kNo)` even if the representation permits deferred submit. Validate DawnContextProvider::GetD3D11Device and the writer/device generation. Never Signal before snap/insert/submit.
2. End destination write access. Any EndAccess-generated commands must precede the export Signal. Persistent Graphite access does not waive explicit submission/flush. Retain representation/resources through this sequence.
3. On the **actual Dawn writer's immediate context**, increment and Signal a gfx::D3DSharedFence, then Flush without CPU completion. Do not Signal the independent GMB allocator device. Dawn D3D11 requests ANGLE's device at this pin, but use the real accessor and fail closed on incompatible backend/device.
4. Snapshot a duplicated fence handle, separate fence token, exact uint64 value, adapter LUID and fresh generation token. Attach the immutable snapshot to CopyOutputSharedImageResult and SendResult immediately. Do not install a completion callback or increment num_readbacks_pending_ on this path. Failed requests still complete oracle accounting exactly once.

[I] CopyOutputResult and FrameCapture need an optional immutable, reference-counted Windows fence snapshot: FrameCapture is copied into the ordered-delivery heap, so a bare move-only HANDLE member is insufficient. DidCopyFrame carries it through validation; MaybeDeliverFrame transports it. Do not read the current value of a mutable shared fence after newer frames have signalled it. Static-frame resurrection must retain the corresponding last-ready snapshot, or perform a new fenced copy; it must never send a fence-mode frame without its readiness dependency.

[I] **Concrete Mojo fields.** Keep the native GMB union and OnFrameCaptured signature. In `media/capture/mojom/video_capture_buffer.mojom` add a Windows-only struct and optional VideoFrameInfo field, with native-handle and unguessable-token imports:

```mojom
// [EnableIf=is_win]
struct PrtsD3D11FrameFence {
  handle<platform> fence_handle;
  gfx.mojom.DXGIHandleToken fence_token;
  uint64 value;
  uint64 adapter_luid;
  mojo_base.mojom.UnguessableToken generation;
};
// Inside VideoFrameInfo, [EnableIf=is_win]:
PrtsD3D11FrameFence? prts_ready_fence;
```

[I] In `chrome/browser/prts/mojom/prts.mojom`, add an equivalent optional `FrameInfo.ready_fence` payload (or import the media struct), and `FrameConsumer::OnFrameCaptured` forwards it. NewBuffer continues to register the texture. Missing fence means ordinary keyed mode, not implicit readiness for an opted-in no-keyed buffer. Cache imported fences by generation+fence token and validate texture/device generation and adapter LUID. Use Mojo platform handles; never transport raw HANDLE integers. Per-frame handle duplication is acceptable for v1; registration-once is a later optimization.

[I] **Endpoint queue and reuse, chosen v1.** On the device supplied to oneVPL enqueue:

```text
GPU Wait(ready, N)
  -> CopyResource(private_bgra, capture_bgra)
  -> End(dedicated_source_read_EVENT_query)
  -> VideoProcessorBlt(private_bgra -> private_nv12)
  -> existing VPP query / Flush
  -> immediate oneVPL encode(private_nv12)
```

[I] Retain the existing private BGRA/NV12 slot lifetime and remove AcquireSync/ReleaseSync only in the negotiated fence mode. Poll the **dedicated source-read query** asynchronously on the source/device sequence (roughly 0.5-1 ms delayed tasks, DONOTFLUSH, bounded outstanding leases). Return OnFrame's existing reply / Viz Done only after this query completes. Encode must not wait for that callback. Keep the lease for GPU-submitted reads even when WebRTC drops a frame or overwrites a pending job. CPU fallback needs the same protected copy or ordinary-pool restart. This replaces keyed exclusion with the incoming fence plus a completed-read Done barrier. It does not add source-lease polling time to encoding, but may increase capture-pool occupancy; trace pressure. This exact asynchronous production reuse loop is not tested by the probe.

[I] **Required disconnect/teardown fix.** Browser's default reply wrapper and InFlightFrameDelivery's destructor currently return buffers on pipe loss. Without keyed exclusion, a queued GPU read may still be using the allocation. Use **callback-pipe closure as abandonment**, preserving the existing Mojo methods. Fence-mode InFlightFrameDelivery takes separate release/abandon closures: explicit Done releases a completed read, whereas destructor without Done abandons. In the browser, replace the default-invoke wrapper with `mojo::WrapCallbackWithDropHandler` (`mojo/public/cpp/bindings/callback_helpers.h:163`): successful endpoint reply sends Done; dropped reply closes the lease remote without Done. Abandon retires the pool generation and clears marked_frame_/resurrection eligibility before dropping outstanding VideoFrames, so old allocations cannot be recycled while imported GPU references remain. Reject late results/deliveries from retired generations at DidCopyFrame/MaybeDeliverFrame. Ordinary leases keep existing behavior. Done is allowed after no-read rejection or verified read completion; failures after GPU submission must finish the source query or close/abandon the lease. GPU reset creates fresh fence/pool/generation and cancels old work; never wait indefinitely for an old process's value. No new Abandon Mojo method is needed.

[I] **Optional later release-fence version.** Endpoint can Signal a separate shared release timeline after its private BGRA copy and reply `{handle, token, value, generation}`; browser forwards `DoneWithFence(...)`. Viz must register a read fence on the backing, with scheduler ordering, before releasing the frame to the pool; next BeginWrite waits for all readers. This needs a real in-process GPU-sequence implementation, not the NOTIMPLEMENTED SII overload. Two independent one-writer timelines avoid Signal races. Do not overwrite all prior dependencies with one generic last-writer fence. Direct VPP from the shared texture is supported by the incoming-readiness probe; its release barrier must follow **completed VPP input reading**, not command submission, while encoding independently retains private NV12. Neither GPU-release variant is verified here; v1 uses completed-read Done.

### Files and patch size

| Files | Change / estimated handwritten lines |
| --- | --- |
| `services/viz/privileged/mojom/compositing/frame_sink_video_capture.mojom`; `media/capture/mojom/video_capture_buffer.mojom` | Preference and ready payload; 40-60 |
| `components/viz/common/frame_sinks/blit_request.h`, `copy_output_result.{h,cc}` | Submit flag, immutable snapshot; 50-90 |
| `components/viz/service/display_embedder/skia_output_surface_impl_on_gpu.{h,cc}` | Writer fence/generation, submit/flush, immediate delivery; 100-150 |
| `components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.{h,cc}` | Negotiate, pass snapshots, release/abandon, marked-frame handling; 90-140 |
| `components/viz/service/frame_sinks/video_capture/in_flight_frame_delivery.{h,cc}` | Distinguish completion and abandonment; 30-50 |
| `components/viz/service/frame_sinks/video_capture/mappable_shared_image_video_frame_pool.{h,cc}`; `components/viz/service/frame_sinks/gmb_video_frame_pool_context_provider.h`, `gmb_video_frame_pool_context_provider_impl.{h,cc}` | Explicit pool allocation choice; 40-70 |
| `gpu/command_buffer/service/shared_image/d3d_image_backing_factory.{h,cc}` | Default-false allocation option through IO hop; 25-45 |
| `chrome/browser/prts/mojom/prts.mojom`, `prts_capture.cc` (header if needed) | Forward payload, opt in, safe drop/disconnect; 60-100 |
| `endpoint/d3d_frame_source.cc`, `d3d_native_frame.{h,cc}` | Fence imports/cache, Wait, async source-read leases, fallback/reset; 140-220 |
| Relevant BUILD.gn files | Dependencies/new helpers as needed; 10-25 |

[I] Budget **700-1,050 handwritten lines, about 20-25 Chromium files plus 3 endpoint files**, allowing helper placement/build dependencies; generated Mojo code excluded. Generic Viz/media/GPU APIs change, with narrowly opt-in execution. No generic DXGIHandle/GpuMemoryBufferHandle ABI extension or D3DImageBacking rewrite is required for Done-after-read-completion v1. GPU-release plumbing is outside this estimate. This is materially more intrusive than polling.

## Expected savings, polling alternative, and acceptance

[V] M3 t6 measured the CopyOutput slice-end -> capture_end gap as P50 **12.82 ms**, P80 **13.25 ms**, P95 **13.63 ms** (`C:/src/tmp/m3/trace/t6-render.txt`). **Evidence correction:** `rend.mjs:6` selects `ph === 'X'` for SkiaOutputSurfaceImplOnGpu::CopyOutput, and uses `ts + dur`. This is a CPU function slice on the GPU thread, not a GPU timestamp/completed-copy query. The old label "readback done -> Mojo send" overstates the evidence. Correlation with present is real in that summary; an explicit wait-for-Present dependency, or 12.8 ms consisting entirely of post-completion idle time, is not established.

[I] Submit-time delivery may advance notification/endpoint queueing by **about 12-13 ms** if deferred submission/callback dispatch dominates that gap. It cannot remove actual copy work or GPU backlog. Endpoint can overlap imports and VPP/encode submission with pending GPU execution. The existing **~0.8 ms CPU/Mojo transport remains**: this design moves its two legs earlier, rather than removing them. Thus **13.6 ms is an optimistic opportunity, not a guaranteed saving**; early notification does not directly equal capture-to-decoded improvement if encoding still starts at the same true GPU completion time. Flush/fence cost, scheduling tails and capture-pool pressure can offset gains. Treat changes below ~20 ms as noise unless repeated with phase alignment.

[I] **Completion-polling alternative:** retain keyed allocation, ordinary Mojo and lease semantics. In the PRTS mappable RGBA copy/FlushAndSubmitCopyOutput path, force immediate nonblocking Graphite submission/backend flush after insertion, and pump CheckAsyncWorkCompletion with an initial short PRTS delayed check (0.5-1 ms), then the existing 2 ms backoff. Ganesh needs actual submit/GL flush. Deliver exclusively through ReadbackContextTexture::OnMailboxReady after genuine completion; preserve write-access/keyed release order, exactly-once callback/counters and context-loss handling. Do not poll an unrelated device's fence or send a completed result before work is done. Estimate **50-120 lines in skia_output_surface_impl_on_gpu.{h,cc}**, possibly the dependency delay helper. This may recover nearly the same dispatch gap if that is the cause, at lower integration risk, but cannot overlap endpoint submission before completion. Actual recoverable time is unmeasured.

[I] **Next task:** implement the opt-in shared-fence v1 described above; retain completion polling as the smaller fallback if integration cost or real-chain data do not justify the lifecycle change. GO is limited to Graphite/Dawn D3D11, compatible adapter, BGRA capture, and the same-device oneVPL path. D3D12, multi-adapter, source-query release, concurrent pool reuse, resize/static-frame resurrection, abandon/reset and fallback transitions remain production integration tests. Values are monotonic per owning timeline; fresh fence identity/generation is required after GPU/pool restart. Never compare raw HANDLE values across processes or generations, and never let a Viz critical thread CPU-wait for GPU completion.

| End-to-end metric on d25j5 | Result in this task |
| --- | --- |
| Capture-to-decoded and input-to-decoded, primary, rVFC presentationTime | **Not measured**. P80 <100 ms / P99 <200 ms acceptance remains unverified. |
| Capture-to-display and input-to-display, old glass estimate, rVFC expectedDisplayTime | **Not measured**. |

[I] Validate with per-frame submit/fence-value/receive/VPP/encode/decode joins, capture-pool reuse instrumentation and at least three repeats per scenario with harness phase alignment held fixed. Always report both decoded and display metrics. No chain was launched here; there is no end-to-end pass or measured latency saving.
