# e2: D3D11 to legacy oneVPL on UHD 630

## 1. What the parked bridge does

Evidence scope: C paths are relative to Chromium src/, read at `1aa07b638799e59656559ae6c6649d4b197f555e`.
P code paths are relative to the prior-art repository, read with git show at parked commit
`7c57d400220f267ca5f33a80e01c0524778163ce`, unless explicitly identified as the current worktree.
P documentation is from the current worktree (HEAD `26fb18cbb897642981086969d063f834b0fbf168`);
the oneVPL journal entry is from the parked commit. Documentation-only timings are [I], not rerun measurements.
[V] means code directly verified at the stated source pin; a P verification is not an M157 verification.
[I] means inference, proposed change, documentation-only result, or external-runtime evidence.
Intel/Microsoft evidence below is identified separately; the installed proprietary DLL was not inspected.
Symbol shorthand: `Device::*` = P `prtsd/src/d3d.rs`; `Encoder::*`/`Allocator::*` = P `prtsd/native/vpl.cpp`;
`VplEncoder::*` = P `prtsd/src/vpl_encoder.rs`; `Resources::*` = P `prtsd/src/browser_frames.rs`;
`Scenes::*` = P `prtsd/src/scene_frames.rs`; `Pipeline::*` = P `prtsd/src/d3d_composition.rs`.

**Session and build.** [V] P `prtsd/native/vpl.cpp:14-28,101-125`, `Loader`, `filter`, `open_session`:
`MFXLoad`, one `MFXCreateConfig` per U32 property, filters hardware implementation, vendor `0x8086`, D3D11 acceleration.
`MFXCreateSession(loader,0)` selects the first filtered implementation; there is no explicit API-version,
codec, adapter-LUID, device-ID, or DLL-name filter, and no enumeration/selection loop.
It calls `MFXVideoCORE_SetHandle(session,MFX_HANDLE_D3D11_DEVICE,device)` before initialization,
then `MFXQueryVersion`/`MFXQueryIMPL`; failure closes the session. The probe only establishes session/device acceptance.
[V] P `prtsd/build.rs:1-47`, `main`: builds the C++17 bridge and vendored official dispatcher sources
into static `prts_onevpl`, including `mfx_dispatcher_vpl_msdk.cpp` and Windows loader code; links D3D11/DXGI.
[V] P `prtsd/vendor/libvpl/libvpl/src/mfx_dispatcher_vpl_msdk.cpp:132-159`, `LoaderCtxMSDK::OpenSession`:
the dispatcher has an API-1.x route, mapping the requested acceleration mode and calling MFXInitEx2.
[I] P `docs/prts/STATUS.md:62-69` records dispatcher 2.17 and runtime API 1.35; this is compatible with
the task's legacy-runtime context, but `open_session` itself does not prove `libmfxhw64.dll` is the DLL selected.

**Allocator.** [V] P `prtsd/native/vpl.cpp:36-96`, `Allocator::{alloc,free,lock,unlock,handle}`:
registered with `MFXVideoCORE_SetFrameAllocator` in `Encoder::open:170`; accepts NV12 only,
1..64 suggested frames, nonzero dimensions <=4096. Each frame is its own DEFAULT texture,
`DXGI_FORMAT_NV12`, MipLevels=ArraySize=SampleDesc.Count=1, no CPU access and MiscFlags=0.
BindFlags always includes `D3D11_BIND_DECODER`; adds `D3D11_BIND_VIDEO_ENCODER` only for a request
having both `MFX_MEMTYPE_VIDEO_MEMORY_ENCODER_TARGET` and `MFX_MEMTYPE_INTERNAL_FRAME`.
There are no texture arrays or shared/keyed handles. `mfxMemId` points to a `Surface` owning a ComPtr.
`GetHDL` returns `mfxHDLPair{texture,nullptr}` (subresource zero); Lock/Unlock return unsupported.
Free erases the allocation group keyed by the response mids pointer; it is a GPU-only allocator.

**Important input distinction.** [V] P `prtsd/native/vpl.cpp:143-144,214-215,234-242`, `Encoder::{open,encode}`:
the application input is a separate `Surface input`, whose texture is assigned from Rust each frame;
it is not copied into one of `Allocator::alloc`'s textures. No application QueryIOSurf/input-pool allocation occurs.
[V] P `prtsd/src/d3d.rs:86-125,236-389`, `Device::{texture,nv12_aligned}`:
the actual encode input NV12 is DEFAULT, single texture/slice/mip/sample, RT|SRV bindings, MiscFlags=0,
created by Rust's converter, without DECODER or VIDEO_ENCODER binding.
Consequently the allocator's internal-surface flags must not be mistaken for mandatory input flags.

**Requested settings.** [V] P `prtsd/native/vpl.cpp:150-216`, `Encoder::{bitrate,open,verify}`:
AVC Baseline, best-speed TargetUsage, AsyncDepth=1, IN_VIDEO_MEMORY, progressive NV12/YUV420,
VBR with TargetKbps=MaxKbps=ceil(bps/1000), GOP=2*fps, GopRefDist=NumRefFrame=1.
Width/Height align to 16; CropW/H round upward to even; frame rate fps/1; CodecLevel and LowPower remain zero/default.
BufferSizeInKB=max(2,ceil(bps/80000)) (100 ms), InitialDelayInKB=half that buffer.
CodingOption: NalHrdConformance=ON, VuiNalHrdParameters=ON, MaxDecFrameBuffering=1.
CodingOption2: BRefType=OFF, RepeatPPS=ON. Optional intra refresh sets GOP=65535,
IntRefType=VERTICAL, IntRefCycleSize=max(2,fps). CodingOption3: LowDelayBRC=ON.
Query and Init accept nonnegative warnings; GetVideoParam then requires AsyncDepth=1,
GopRefDist=1, IN_VIDEO_MEMORY, VBR, LowDelayBRC=ON, plus requested intra-refresh type/cycle.
It logs rate/buffer readback but does not require exact bitrate, profile, dimensions, or buffer equality.

**Encode loop.** [V] P `prtsd/native/vpl.cpp:234-272`, `Encoder::encode`:
requires input unlocked; validates NV12, aligned dimensions and the identical endpoint D3D11 device.
Retains the input ComPtr; assigns 90-kHz timestamp `sequence*90000/fps` and FrameOrder.
First/requested/reset frame requests I|IDR|REF; EncodeFrameAsync retries DEVICE_BUSY with Sleep(1), bounded at 1 s.
It rejects negative status or a missing sync point; SyncOperation(sync,1000) must return exactly NONE.
Checks nonempty bounded bitstream, no B frame, timestamp equality, DTS equal PTS or unknown, and input unlocked.
No delayed-output/drain pipeline is implemented. Optional DataOffset causes CPU memmove, then drops input reference.
[V] P `prtsd/src/vpl_encoder.rs:122-154`, `VplEncoder::{encode_nv12,encode_texture}`:
the reusable Rust bitstream buffer is supplied directly to C++; returned bytes are copied with `to_vec()`.
[V] P `prtsd/native/vpl.cpp:218-232`, `Encoder::reset`: bitrate Reset, Close/Init fallback on failure,
settings re-read, next frame forced IDR; the session and D3D device persist.

**Who converts.** [V] P `prtsd/src/vpl_encoder.rs:144-154`, `VplEncoder::encode_texture` calls
`Device::nv12_aligned(...,16)` while holding the Rust device mutex, then releases it before native encode.
P `prtsd/src/d3d.rs:236-389`, `Device::nv12_aligned` pads BGRA by CopySubresourceRegion/edge replication
when needed, creates a video processor/enumerator/input/output views and NV12 texture every call,
then VideoProcessorBlt converts RGB full-range to BT.601 limited-range, auto-processing disabled.
The processor content descriptor hardcodes 30/1 frame rates, even if the encoder uses another fps.
`Device::finish` waits for GPU completion before returning NV12. No MFX VPP/RunFrameVPPAsync is used.
[V] P `prtsd/src/vpl_encoder.rs:157-188`, `VideoEncoder::encode`: the CPU-input alternative first
UpdateSubresource uploads BGRA to a cached DEFAULT texture, then uses the same GPU conversion and encode.
[V] Current prior-art HEAD `26fb18c`, `prtsd/src/d3d.rs:233-375`, `Device::nv12`, was also read:
it uses four-pixel alignment for MF. The parked branch adds `nv12_aligned(...,16)` for VPL;
the 1080->1088 copy count below concerns the parked VPL path, not current MF's already-four-aligned 1080 height.

## 2. Copy count and removable work

[V] P `prtsd/src/d3d.rs:191-210`, `Device::copy_shared`: one full BGRA CopyResource into private memory,
AcquireSync(1), EVENT query/Flush/GetData wait, ReleaseSync(0). Opening the NT handle itself copies no pixels.
[V] P `prtsd/src/scene_frames.rs`, `Scenes::render`; `prtsd/src/d3d_composition.rs:127-219`, `Pipeline::compose`:
no visible popup => reuse private base; visible GPU popups => another full base CopyResource plus one Draw per popup,
then another finish wait. Each newly imported source has its own copy_shared cost.
[V] P `prtsd/src/d3d.rs:251-320`, `Device::nv12_aligned`: 1920x1080 becomes 1920x1088,
so one visible-image copy plus eight last-row copies precedes the one BGRA->NV12 VideoProcessorBlt.
The converter then performs its own finish wait; native encode finally waits with SyncOperation.
[I] Count from already-exported shared BGRA, no popup, 1080p: 10 explicit GPU copy commands
(one import + nine padding), one conversion, one encode; no CPU raw-pixel copy/readback.
Internal driver/runtime transfers remain uncounted until traced. See the functions above and `Encoder::encode:234-272`.

| Stage from exported BGRA | GPU work | CPU synchronization / byte movement | Evidence |
| --- | --- | --- | --- |
| Duplicate/open NT handle, register | No pixel copy | COM/handle work; validates LUID/descriptor | [V] P `prtsd/src/browser_frames.rs`, `Resources::process:77-187`; `Device::open:127-156` |
| Shared -> private BGRA | 1 full CopyResource per arriving source | AcquireSync(1,1000), finish EVENT+Flush+poll, ReleaseSync(0), FRAME_RELEASE | [V] P `Resources::process:245-295`; `Device::copy_shared:191-210` |
| Optional popup scene | 1 full base copy + N alpha/shadow Draws; constants uploaded | finish EVENT wait | [V] P `prtsd/src/scene_frames.rs`, `Scenes::render:120-225`; `Pipeline::compose:127-219` |
| Align BGRA to 16 | 0 if already aligned; otherwise 1 image copy + edge strips/corners | No separate wait here | [V] P `Device::nv12_aligned:251-320` |
| BGRA -> NV12 | 1 VideoProcessorBlt writing a fresh NV12 texture | finish EVENT+Flush+poll | [V] P `Device::nv12_aligned:324-389` |
| NV12 -> AVC | Hardware encoding; no application input CopyResource | DEVICE_BUSY Sleep(1) if needed, then SyncOperation(1000) | [V] P `Encoder::encode:234-272` |
| Encoded output | Driver output transfer not observable here | Optional DataOffset memmove; always Rust bitstream to_vec | [V] P `Encoder::encode:269`; `VplEncoder::encode_nv12:122-143` |

[I] Thus an aligned, popup-free frame uses one import copy + one CSC; 1080p uses ten copy commands + one CSC.
Counts stop at returned H.264 bytes, excluding RTP packetization and opaque encoder reconstruction/reference traffic.
Starting upstream of the shared export adds the browser's one visible-rect CopySubresourceRegion and EVENT wait:
[V] P `patches/0015-Add-native-GMB-capture-and-keyed-D3D11-texture-expor.patch:441-510`,
Chromium-added `chrome/browser/prts/prts_d3d.cc`, `PrtsD3D::{Create,Copy}`; later patch 0019 changes signatures/probing.
No claim that these custom files exist in unpatched M157; capture/compositor work before export is outside this count.

**(a) Direct VP input: preferred spike.** [I] P `Device::{open,nv12_aligned}`, `Encoder::encode`:
cache the opened BGRA input view and reusable NV12 RT surface, acquire key 1 only around the shared-source VP read,
Blt directly into that surface, release key 0, then pass its MemId/GetHDL pair to EncodeFrameAsync.
This removes the private import copy; explicit source/destination/target rects plus defined background can remove
the BGRA padding texture/copies without scaling the visible image. Qualify coded-border behavior before adopting it.
[I] Microsoft `ID3D11VideoDevice::CreateVideoProcessorInputView`: DEFAULT, single-sample RT|SRV BGRA is eligible;
shared/keyed status is not listed as a prohibition. Actual Intel view-format support must still be queried.
See [input-view requirements](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11videodevice-createvideoprocessorinputview).
[I] Microsoft `CreateVideoProcessorOutputView` requires RENDER_TARGET. The parked allocator's DECODER-only
surfaces cannot simply become VP output views; use the existing Rust RT-input pattern or qualify changed flags.
See [output-view requirements](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11videodevice-createvideoprocessoroutputview).
This means direct CSC into the **application encode input**, not into an arbitrary internal reconstruction surface.
[I] With separately captured popups, retain private cached layers/composition or qualify multi-stream VP alpha/masks;
one direct base Blt cannot reproduce the current shadow/corner shader. Anchors: P `Scenes::render`, `Pipeline::compose`.

**(b) RGB4 directly into AVC: supported feature on Skylake+, exact shared import still unqualified.** [I]
Intel engineer Artem S. confirms in the Windows legacy-MSDK discussion that direct RGB input to AVC is
supported from Skylake onward, and recommends MFXVideoENCODE_Query rather than API-version gating.
Thus **yes, Gen9/9.5 is in the supported generation range**; API 1.x does not itself rule out internal CSC.
This is vendor support evidence, not a test of the installed UHD 630 DLL or keyed-NT allocation.
See [Intel RGB4-to-H.264 confirmation, 2018-10-05 and 2018-10-10](https://community.intel.com/t5/Media-Intel-Video-Processing/RGB4-to-H264-Encode-Support/m-p/1136593).
Intel MediaSDK source was read at `7a72de33a15d6e7cdb842b12b901a003f7154f0a` (external evidence, not Chromium pin).
`_studio/mfx_lib/shared/src/mfx_h264_enc_common_hw.cpp:2985-3043`,
`MfxHwH264Encode::CheckVideoParamQueryLike`, permits RGB4/BGR4, requires/normalizes YUV444 input metadata,
then rejects 422/444 when the driver's `ddi_caps.Color420Only` is true.
See [Intel AVC format validation](https://github.com/Intel-Media-SDK/MediaSDK/blob/7a72de33a15d6e7cdb842b12b901a003f7154f0a/_studio/mfx_lib/shared/src/mfx_h264_enc_common_hw.cpp#L2985).
`_studio/mfx_lib/encode_hw/h264/src/mfx_h264_encode_hw.cpp:996-1000`, `ImplementationAvc::Init`,
still allocates NV12/YUV420 reconstructed frames for RGB/YUY2 input. This supports a capability-gated internal CSC
path, not RGB AVC output or universal Gen9 RGB acceptance.
See [Intel reconstruction allocation](https://github.com/Intel-Media-SDK/MediaSDK/blob/7a72de33a15d6e7cdb842b12b901a003f7154f0a/_studio/mfx_lib/encode_hw/h264/src/mfx_h264_encode_hw.cpp#L996).
[I] These sources do not expose the installed Windows DLL's caps or certify the precise sharing/binding combination.
Probe Query/Init/GetVideoParam and encode/decode with RGB4/YUV444; compare LowPower OFF/ON if supported,
since the parked bridge leaves LowPower unspecified. This is runtime qualification of a supported feature.
The parked bridge itself rejects RGB4 in `Encoder::encode:240` and uses NV12/YUV420 in `Encoder::open:184-185`.
Even if accepted, internal CSC still performs color conversion; it relocates that work and may retain shared BGRA
until encoding completes. No speedup or short source-mutex lifetime follows just from RGB4 acceptance.

**(c) Shared BGRA as an encode surface.** [I] A successful OpenSharedResource1 only opens the allocation;
it does not convert BGRA to NV12, realign height, or add bindings. Current bridge rejects both format and 1080-vs-1088
dimensions before reaching the driver: P `Device::open`, `Encoder::encode:234-242`.
[V] C `gpu/command_buffer/service/shared_image/d3d_image_backing_factory.cc:351-417`,
`D3DImageBackingFactory::CreateGpuMemoryBufferHandle`: native GMB uses DEFAULT RT|SRV, single mip/slice/sample,
NT-handle plus keyed-mutex sharing. This verifies an eligible VP source layout at M157, not an AVC input contract.
[V] P `PrtsD3D::Create` (patch 0015 above) uses the same RT|SRV sharing flags; no VIDEO_ENCODER binding.
[I] Do not infer failure solely from absence of VIDEO_ENCODER: the parked working input is also RT|SRV.
Intel `samples/sample_common/src/d3d11_allocator.cpp`, `D3D11FrameAllocator::AllocImpl:451-476`, likewise
uses DECODER|VIDEO_ENCODER for internal encoder targets and RENDER_TARGET for VP output/RGB textures.
Its `D3D11FrameAllocator::ConverColortFormat:548-559` maps RGB4 to DXGI_FORMAT_B8G8R8A8_UNORM (BGRA byte order).
See [Intel allocator flags](https://github.com/Intel-Media-SDK/MediaSDK/blob/7a72de33a15d6e7cdb842b12b901a003f7154f0a/samples/sample_common/src/d3d11_allocator.cpp#L451).
Direct BGRA encode probably fails with the unchanged NV12 configuration; a reconfigured RGB4 path depends on
caps, alignment, actual input bindings, same-device registration, and keeping the key until the last GPU reader.
[I] External NV12 video input need not cause another runtime copy: Intel
`_studio/mfx_lib/encode_hw/h264/src/mfx_h264_encode_hw_utils_new.cpp:2543-2595,2687-2725`,
`CopyRawSurfaceToVideoMemory` copies only system-memory
inputs; `GetNativeHandleToRawSurface` uses GetExternalFrameHDL for IN_VIDEO_MEMORY. Driver-private work remains unknown.
See [Intel external-input handling](https://github.com/Intel-Media-SDK/MediaSDK/blob/7a72de33a15d6e7cdb842b12b901a003f7154f0a/_studio/mfx_lib/encode_hw/h264/src/mfx_h264_encode_hw_utils_new.cpp#L2543).

## 3. Synchronization and completion

[V] P `prtsd/src/d3d.rs:159-208,380-388`, `Device::{finish,copy_shared,nv12_aligned}`:
holds source keyed mutex through copy completion; finish creates EVENT, End, Flush, polls raw HRESULT
until S_OK, sleeps 1 ms on S_FALSE, deadline 1 s. It correctly avoids windows-rs Result discarding S_FALSE.
No ID3D11Fence/shared fence is used in this path; SyncOperation occurs after the explicit VPP completion wait.
[V] P `prtsd/src/d3d.rs:42-81`, `Device::new`: creates Intel hardware device with BGRA_SUPPORT|VIDEO_SUPPORT,
no SINGLETHREADED flag; QIs immediate context for ID3D11Multithread and enables protection.
Rust Arc<Mutex<Device>> serializes application context use; the bridge does not itself set multithread protection.

[V] P `PrtsD3D::Copy`, patch 0015: browser acquires Viz key 0 then export key 0, copies,
waits for EVENT, releases Viz key 0 and export key 1; endpoint `copy_shared` acquires 1 and returns 0.
P `Resources::process:287-295` emits FRAME_RELEASE after private-copy completion, independently of encoding.
[V] C `gpu/command_buffer/service/dxgi_shared_handle_manager.cc:129-184`,
`DXGISharedHandleState::{AcquireKeyedMutex,ReleaseKeyedMutex}` uses the Chromium acquire-key constant for both
transitions, nesting per device; requires exactly S_OK on acquire, not merely SUCCEEDED.
Its ReleaseKeyedMutex calls ReleaseSync without an application EVENT wait. This is Chromium's local sharing
convention, not the custom export protocol's alternating 0/1 keys.
[I] Keep NT-resource mutex ownership separate from CPU Rust locking: MSDK worker threads do not take the Rust mutex.
Use protected immediate context and one ordered submission owner; never hold a D3D multithread Enter lock
across SyncOperation, because runtime workers may need it. P `Device::new`, `VplEncoder::encode_texture` are anchors.
ID3D10Multithread is an older protection interface used by some D3D11 code; this endpoint actually uses ID3D11Multithread.
Its SetMultithreadProtected return is the previous state, not a success flag:
[Microsoft method contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_4/nf-d3d11_4-id3d11multithread-setmultithreadprotected).

**Does SyncOperation cover VP?** [V] In parked code VP is already finished by `Device::finish:388`
before `Encoder::encode:261` calls SyncOperation, so it does not currently replace that VP wait.
[I] `MFXVideoCORE_SyncOperation` waits for its specified MFX task; it is not a general D3D11 fence and
does not discover an arbitrary external VP command or signal the source keyed mutex.
See [Intel CORE contract](https://intel.github.io/libvpl/latest/API_ref/VPL_func_vidcore.html#MFXVideoCORE_SyncOperation).
With a correctly ordered VP write then encoder read on the same protected endpoint device/context and resource,
successful encode completion necessarily follows the dependent VP write. That permits eliminating a redundant
host VP wait after qualifying driver ordering; different contexts/devices need an explicit dependency.
Microsoft `ID3D11DeviceContext::Flush` submits asynchronously, not a completion wait
([Flush contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush)).
Keep input/views alive and do not reuse NV12 until unlocked.
Source key release after queued Blt must preserve DXGI GPU handoff ordering; do not equate Blt's HRESULT with completion.
[I] Minimal path can ReleaseSync(0) immediately after the queued source read, using keyed-mutex ordering;
qualify this on the Intel driver. Conservative spike variant waits for one post-Blt EVENT before ReleaseSync/ACK.
The mutex then covers only conversion/source-read completion, not EncodeFrameAsync or SyncOperation.
Microsoft `IDXGIKeyedMutex::ReleaseSync` releases exclusive rendering access; check its HRESULT
([ReleaseSync contract](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgikeyedmutex-releasesync)).
Microsoft documents VP EVENT/TIMESTAMP accounting limitations below feature level 11; record FL>=11 for query timing.
See [VideoProcessorBlt remarks](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11videocontext-videoprocessorblt).

## 4. Minimal-latency per-frame sequence and cost

[I] Proposed sequence for a complete root BGRA frame; cache device/session, shared-handle imports/views,
VP/enumerator, NV12 input surfaces and bitstream storage at setup/resize. Same-device NV12 RT input follows
P `Device::nv12_aligned` + `Allocator::handle` + `Encoder::encode`; rect/pool changes are proposals, not a tested patch.

1. Validate source generation/LUID/descriptor; choose latest frame and a free, unlocked NV12 input slot.
2. Enter endpoint submission serialization; AcquireSync(shared key 1), require exactly S_OK, use a bounded deadline.
3. Set VP source rect to visible BGRA, destination rect to visible output, target to aligned NV12 extent;
   use a defined padding background and explicit color space, with auto-processing disabled.
4. Submit one VideoProcessorBlt(shared BGRA input view -> that NV12 output view); keep COM resources alive.
5. ReleaseSync(0) with validated GPU handoff ordering, then FRAME_RELEASE; conservative variant first waits on EVENT.
6. Leave application submission lock; populate NV12 FrameInfo/MemId/timestamp/order and optional IDR control.
7. Call EncodeFrameAsync, retry DEVICE_BUSY only within the latency deadline; require a sync point.
8. Call SyncOperation for this frame; require success and bounded output, correct timestamps, no B frame.
9. After input unlock, recycle NV12; pass encoded bytes to transport, avoiding the optional output memmove/copy if practical.

[I] For separate popup captures, keep the existing private layer-cache/composition branch until equivalent VP
multi-stream composition is proven. The fast sequence applies when no separately composited popup is needed;
it does not authorize dropping popup/UI pixels. P `Scenes::render:120-225`, `Pipeline::compose:127-219`.

| Prior-art result | Reported measurement | Interpretation / source |
| --- | --- | --- |
| 1080p, fixed 4 Mbps, 30-fps GPU/MF harness | 13.874 ms conversion+encode; 29.87 fps | [I] Documented measurement, not GPU-only; P `docs/prts/phase2-m4.md:140-160`, Measurements; `prtsd/src/mf_encoder.rs`, `MfEncoder::encode_texture` |
| 1080p actual adaptive daemon/WebRTC GPU/MF | 14.496 ms encode; 23.25 encoded fps | [I] Documented measurement, not oneVPL or GPU-only; P `docs/prts/phase2-m4.md:163-177`, actual release measurements |
| 720p parked oneVPL GPU input, 900 immediate decoded frames | mean 9.45 ms, p95 10.89 ms | [I] Documented measurement; P `docs/prts/journal-mvp.md:1249`, 2026-10-21 18:43:50; `prtsd/tests/vpl_encoder.rs`, `direct_hardware_low_latency_and_live_caps:15-102` |
| 720p tuned MF GPU input | mean 9.281 ms, p95 10.737 ms | [I] Documented measurement; P `docs/prts/mvp-mf-latency.md:37-58`, Reproduction and measurements |

[V] P `prtsd/src/model.rs`, `Size::default` is 1280x720@1; parked VPL fixture starts its timer
after stimulus generation/UpdateSubresource and ends before decode (`direct_hardware_low_latency_and_live_caps:50-81`).
[I] Therefore 9.45 ms includes conversion, waits, encode and output copy, but is not 1080p or isolated GPU time.
No isolated 1080p VP/encode GPU timestamps or optimized direct-shared-to-VPL measurement were found in the inspected
docs/journal evidence; `encoder-bench.md` is not present on the parked branch. Do not promise a sub-ms CSC result.
[I] Expected optimized GPU work: **one full-frame CSC write plus AVC encoding**, zero endpoint BGRA image copies,
zero CPU pixel transfers; numeric GPU milliseconds remain unmeasured. The 13.874/14.496 ms MF results are historical
host wall-time context, not a predicted floor/ceiling for VPL. Instrument VP and encode separately and end-to-end.

## 5. Risks and unknowns for the spike

- [I] Exact runtime/adapter: log DLL path/version, API and adapter LUID; index 0 plus VendorID is insufficient
  on multi-Intel systems. P `open_session:101-125`, `Device::new:42-81`.
- [I] Shared BGRA VP eligibility: create/test input view and enumerate BGRA-input/NV12-output support on UHD 630;
  opening a handle is not capability validation. P `Device::open`, `Device::nv12_aligned`; Microsoft input-view contract.
- [I] Input versus internal bindings: prove reusable RT NV12 can be both VP output and external encode input
  with no hidden driver copy; don't redirect VP into DECODER-only allocator surfaces. P `Allocator::alloc`, `Encoder::encode`.
- [I] RGB4 alternative: Query/Init/readback/decode RGB4/YUV444, LowPower OFF and ON on the installed DLL;
  capture actual CSC/input-retention behavior. Intel `CheckVideoParamQueryLike:2985-3043`, `ImplementationAvc::Init:996-1000`.
- [I] GPU ordering/release: prove post-Blt key-0 release plus ACK never permits overwrite before the source read;
  compare query-backed and query-free paths under stress. P `Device::copy_shared`, C `DXGISharedHandleState::ReleaseKeyedMutex`.
- [I] Threading/lifetime: protected context, no D3D lock held through SyncOperation, slot reuse only after unlock,
  cancellation/device-loss teardown without fabricated key transitions. P `Device::new`, `Encoder::encode`, `Resources::process`.
- [I] Crop/colors: 1080->1088, odd sizes/even crop, BT.601 versus intended BT.709, range/VUI and padded-border quality;
  compare decoded pixels. P `Device::nv12_aligned`, `Encoder::open`; existing odd-size gate is not direct-Blt qualification.
- [I] Popup ownership: private scene caches currently survive browser credit release; direct source use must preserve
  popup-only updates, alpha, corners/shadows, and close restoration. P `Scenes::render`, `Pipeline::compose`.
- [I] Rate control/backpressure: warnings/readback changes, small-VBV rounding, busy/no-sync/delayed output,
  reset/recreate and forced-IDR behavior. P `Encoder::{verify,reset,encode}`; journal-mvp:1249 records 150->473 kbps readback.
- [I] Cost: measure 1080p VP/encode GPU time and host p50/p95/p99, source-key hold time, 30/60 fps under UI load;
  separate object creation, polling sleeps and copies. P `Device::finish`, VPL latency fixture, phase2-m4 Measurements.
