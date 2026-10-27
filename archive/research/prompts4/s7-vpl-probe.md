# Task S7: oneVPL / Media SDK low-delay H.264 encoder probe on the UHD 630 (standalone C++ program)
Deliverables (only these paths): `tools/s7-vpl-probe/` (source, `build.bat`, README-free: put commands in the report), report
`research/s7-onevpl-probe.md`. Temp/output files: `C:\src\tmp\s7\`. Hard limit 45 minutes; write the report skeleton first.

Read first: design/03-video-pipeline.md section 3.4 (encoder parameters) and 3.3 (D3D11 zero-copy), 5 minutes max.
Runtime facts: `C:\Windows\System32\libmfxhw64.dll` (legacy Intel Media SDK runtime, API 1.x) serves Gen9.5 UHD 630; the oneVPL GPU runtime
does NOT support Gen9. Use the libvpl 2.x dispatcher (it loads libmfxhw64 for API 1.x). Dispatcher source/headers: the prior-art vendored copy
(`git -C C:\Users\Flysoft\Desktop\chromium archive wip/e1-onevpl prtsd/vendor/libvpl | tar -x -C C:\src\tmp\s7\vendor`), or the official
intel/libvpl release from GitHub if that is incomplete. Build with cl.exe from `vcvars64.bat` (VS 2026 at C:\Program Files\Microsoft Visual
Studio\18\Community); no CMake is installed (install it via winget only if the dispatcher really needs it). Do not copy prior-art code
other than the dispatcher; write the probe yourself.

Program (`s7probe.exe`):
1. Enumerate implementations (impl name, ApiVersion, AccelerationMode, VendorImplID, DXGI adapter LUID). Create the session with
   MFXLoad + MFXCreateConfig filters (hardware, AVC encode, D3D11 accel mode; do NOT require API 2.x). Create a D3D11 device on the Intel
   adapter (VIDEO_SUPPORT flag, ID3D10Multithread protected) and SetHandle(MFX_HANDLE_D3D11_DEVICE).
2. Parameter matrix, H.264 1920x1080, 30 and 60 fps. Base: AsyncDepth=1, GopRefDist=1 (no B-frames), NumRefFrame=1, GopPicSize=65535,
   IdrInterval=0, ext CO2 AdaptiveI=OFF/AdaptiveB=OFF, TargetUsage=7, Constrained Baseline, Level 4.0 (30) / 4.2 (60), IOPattern IN_VIDEO_MEMORY
   with D3D11 NV12 textures you allocate yourself (external frame allocator; BIND_RENDER_TARGET; this is the zero-copy layout of 03 3.3);
   fall back to IN_SYSTEM_MEMORY only if video memory fails, and say so. Rows vary one thing at a time:
   - LowPower ON (VDEnc) vs OFF (VME);
   - RC: CBR 10/12/16 Mbps; VBR target=max 12 Mbps; CBR+LowDelayBRC (CO3); VBR+LowDelayBRC+MaxFrameSize (CO2/CO3 MaxFrameSize ~ 1.5x avg frame);
   - VBV BufferSizeInKB = 1 frame vs 3 frames; 
   - Intra refresh: CO2 IntRefType=1 (vertical) and 2 (horizontal) with IntRefCycleSize=fps (1 s), IntRefQPDelta;
   - Slices: NumSlice 1 / 2 / 4 (and CO2 MaxSliceSize if supported);
   - Profile Main and High (CAVLC/CABAC as default) for comparison; input FourCC RGB4 (encoder-internal CSC) vs NV12.
   For each row record: Query status, Init status (keep warnings, e.g. MFX_WRN_INCOMPATIBLE_VIDEO_PARAM = 5), and GetVideoParam read-back of
   every field you set (LowPower, RateControlMethod, kbps, BufferSizeInKB, LowDelayBRC, MaxFrameSize, IntRefType, NumSlice, profile, level,
   GopRefDist, NumRefFrame). Flag silently overridden fields.
3. Latency: for at least 4 accepted combos (must include: LowPower+CBR 12 Mbps+LowDelayBRC NV12 1080p60; LowPower+VBR+LowDelayBRC+MaxFrameSize
   1080p30; intra-refresh variant; the best non-LowPower fallback), encode 600 synthetic frames (moving gradient + scrolling stripes + frame
   counter), uploading each frame into the D3D11 NV12 texture (UpdateSubresource; exclude the upload time), and measure per-frame
   EncodeFrameAsync->SyncOperation latency with QPC: P50/P95/P99/max ms; frame-size distribution (avg, P99, max bytes, IDR size); achieved
   bitrate vs target. Write .h264 files to C:\src\tmp\s7\ and verify decodability: Windows Media Foundation H.264 decoder MFT, or count
   access units/IDR via a minimal NAL parser if MFT is too slow to wire up.
Report: environment (driver version, runtime DLL version, API version), matrix table (row, Query, Init, read-back diffs), latency table,
the recommended starting combination for 03 section 3.4 (and what to change there), surprises, exact build/run commands.
Priority under the time limit: build+enumerate, then the 1080p matrix, then latency.
