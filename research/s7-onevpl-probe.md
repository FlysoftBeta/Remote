# S7 oneVPL / Media SDK encoder probe on UHD 630

## Scope and evidence
- [V] Standalone C++ implementation; design/03-video-pipeline.md sections 3.3 and 3.4 read first.
- [V] Report skeleton created before building. No product files or prior-art checkout modified.
- [V] Only the upstream dispatcher/headers were taken from prior art; probe code was written independently.
- [I] Recommended settings below are a starting point for real browser/receiver testing.

## Environment and enumeration
| Item | [V] Measured / verified value |
|---|---|
| OS | Windows 11 IoT Enterprise LTSC, 10.0.26100 |
| Adapter | Intel UHD Graphics 630, PCI 8086:9bc5, DXGI adapter 0 |
| Driver | 30.0.100.9805 |
| Implementation | mfxhw64; ApiVersion and session MFXQueryVersion both 1.35 |
| Acceleration / VendorImplID | D3D11 (768 / 0x300); VendorImplID=0 |
| LUID | 00000000:0000e6f6; extended-device LUID valid and matches DXGI |
| Loaded runtime | C:/Windows/System32/DriverStore/FileRepository/iigd_dch.inf_amd64_22c2c7607995c1e1/libmfxhw64.dll |
| Runtime version | File 11.21.6.23; product 11.0.0.003; same versions as System32 DLL |
| Dispatcher | libvpl v2.17.0; vendored upstream commit d77f9195cf495b937631607333288fd917ae8939 |
| Toolchain | VS 2026 Community; MSVC tools 14.51.36231; x64 cl.exe; direct build, /MP4 |
| Packages downloaded | None; no CMake needed |

## Implementation and prior-art evaluation
- [V] git archive extracted only prtsd/vendor/libvpl from wip/e1-onevpl into task temp.
- [V] That copy identifies unmodified Intel API/dispatcher source and MIT LICENSE; used only for dispatching to API 1.x.
- [V] This matches section 3.4: MFXLoad plus hardware, AVC-encoder and D3D11 filters; no API-2 minimum.
- [V] D3D11 device on the Intel adapter: VIDEO_SUPPORT and ID3D10Multithread protection, then SetHandle.
- [V] External allocator returns a texture/subresource pair; separate DEFAULT textures, ArraySize=1, BIND_RENDER_TARGET.
- [V] Input is NV12 1920x1088, crop 1920x1080; all 42 rows initialized with IN_VIDEO_MEMORY. No system fallback used.
- [V] Latency runs allocated the QueryIOSurf-requested single input texture and completed hardware encoding from it.
- [V] This validates the section 3.3 encoder input layout; no shared-handle import, keyed mutex, VPP or BT.709 VUI configuration is exercised.
- [V] Probe includes explicit system-memory fallback rows only after a failed video-memory Init.
- [V] Upstream source was not edited. /FIobjbase.h supplies StringFromGUID2 declaration needed by the direct build.

## Parameter matrix
- [V] Each row ran Query, Init with ORIGINAL requested parameters, then GetVideoParam; Query edits are logged separately.
- [V] 0=success; 5=MFX_WRN_INCOMPATIBLE_VIDEO_PARAM; 16=CODINGOPTION_ON, 32=OFF.
- [V] Every explicitly set value is persisted in matrix.csv requested/query_result/readback columns.
- [V] Common values retained unless listed: AsyncDepth=1, LowPower=ON, CBR, target=max=12000 kbps, TU7.
- [V] Also retained: profile=322 (Constrained Baseline), level=40/42 at 30/60 fps; GopPicSize=65535, GopRefDist=1, NumRefFrame=1, IdrInterval=0.
- [V] CO2 AdaptiveI/B=OFF; CO3 LowDelayBRC=OFF; NumSlice=1; IntRefType/Cycle/QPDelta=0; MaxFrameSize/MaxSliceSize=0 unless varied.
- [V] FrameInfo retained: NV12, width=1920, height=1088, crop=1920x1080, fps/1, progressive, YUV420 (RGB4 exception below).
- [V] VBV request=ceil(kbps*frames/(8*fps)), InitialDelayInKB=floor(BufferSizeInKB/2). IntRef variants set cycle=fps and QPDelta=-2.
- [V] Row names specify fps and variation; all unlisted read-back fields match the request. Diffs flag driver overrides/defaults, including zero-to-default changes.

| Row | Query | Init | Get | Read-back differences (requested -> actual) |
|---|---:|---:|---:|---|
| 30_base_lp_cbr12 | 5 | 5 | 0 | BufferSizeInKB:50->101;InitialDelayInKB:25->50; |
| 30_vme_cbr12 | 5 | 5 | 0 | BufferSizeInKB:50->101;InitialDelayInKB:25->50; |
| 30_cbr10 | 5 | 5 | 0 | BufferSizeInKB:42->84;InitialDelayInKB:21->41; |
| 30_cbr16 | 5 | 5 | 0 | BufferSizeInKB:67->134;InitialDelayInKB:33->66; |
| 30_vbr12 | 5 | 5 | 0 | BufferSizeInKB:50->101;InitialDelayInKB:25->50;MaxFrameSize:0->783360; |
| 30_cbr12_ldb | 5 | 5 | 0 | BufferSizeInKB:50->101;InitialDelayInKB:25->50;LowDelayBRC:16->32; |
| 30_vbr12_ldb | 5 | 5 | 0 | BufferSizeInKB:50->101;InitialDelayInKB:25->50;MaxFrameSize:0->50000; |
| 30_vbr12_ldb_maxframe | 5 | 5 | 0 | BufferSizeInKB:50->101;InitialDelayInKB:25->50; |
| 30_vbv3 | 0 | 0 | 0 | None |
| 30_intra_vertical | 5 | 5 | 0 | BufferSizeInKB:50->101;InitialDelayInKB:25->50; |
| 30_intra_horizontal | 5 | 5 | 0 | BufferSizeInKB:50->101;InitialDelayInKB:25->50; |
| 30_slices2 | 5 | 5 | 0 | BufferSizeInKB:50->101;InitialDelayInKB:25->50; |
| 30_slices4 | 5 | 5 | 0 | BufferSizeInKB:50->101;InitialDelayInKB:25->50; |
| 30_maxslice1400 | 5 | 5 | 0 | BufferSizeInKB:50->101;InitialDelayInKB:25->50; |
| 30_main | 5 | 5 | 0 | BufferSizeInKB:50->101;InitialDelayInKB:25->50; |
| 30_high | 5 | 5 | 0 | BufferSizeInKB:50->101;InitialDelayInKB:25->50; |
| 30_rgb4 | 5 | 5 | 0 | BufferSizeInKB:50->101;ChromaFormat:1->3;InitialDelayInKB:25->50; |
| 30_tu4 | 5 | 5 | 0 | BufferSizeInKB:50->101;InitialDelayInKB:25->50; |
| 30_vme_cbr12_ldb | 5 | 5 | 0 | BufferSizeInKB:50->101;InitialDelayInKB:25->50;LowDelayBRC:16->32; |
| 30_vme_vbr12_ldb_maxframe | 5 | 5 | 0 | BufferSizeInKB:50->101;InitialDelayInKB:25->50; |
| 30_vme_intra_vertical | 5 | 5 | 0 | BufferSizeInKB:50->101;InitialDelayInKB:25->50; |
| 60_base_lp_cbr12 | 5 | 5 | 0 | BufferSizeInKB:25->51;InitialDelayInKB:12->25; |
| 60_vme_cbr12 | 5 | 5 | 0 | BufferSizeInKB:25->51;InitialDelayInKB:12->25; |
| 60_cbr10 | 5 | 5 | 0 | BufferSizeInKB:21->42;InitialDelayInKB:10->20; |
| 60_cbr16 | 5 | 5 | 0 | BufferSizeInKB:34->67;InitialDelayInKB:17->33; |
| 60_vbr12 | 5 | 5 | 0 | BufferSizeInKB:25->51;InitialDelayInKB:12->25;MaxFrameSize:0->783360; |
| 60_cbr12_ldb | 5 | 5 | 0 | BufferSizeInKB:25->51;InitialDelayInKB:12->25;LowDelayBRC:16->32; |
| 60_vbr12_ldb | 5 | 5 | 0 | BufferSizeInKB:25->51;InitialDelayInKB:12->25;MaxFrameSize:0->25000; |
| 60_vbr12_ldb_maxframe | 5 | 5 | 0 | BufferSizeInKB:25->51;InitialDelayInKB:12->25; |
| 60_vbv3 | 0 | 0 | 0 | None |
| 60_intra_vertical | 5 | 5 | 0 | BufferSizeInKB:25->51;InitialDelayInKB:12->25; |
| 60_intra_horizontal | 5 | 5 | 0 | BufferSizeInKB:25->51;InitialDelayInKB:12->25; |
| 60_slices2 | 5 | 5 | 0 | BufferSizeInKB:25->51;InitialDelayInKB:12->25; |
| 60_slices4 | 5 | 5 | 0 | BufferSizeInKB:25->51;InitialDelayInKB:12->25; |
| 60_maxslice1400 | 5 | 5 | 0 | BufferSizeInKB:25->51;InitialDelayInKB:12->25; |
| 60_main | 5 | 5 | 0 | BufferSizeInKB:25->51;InitialDelayInKB:12->25; |
| 60_high | 5 | 5 | 0 | BufferSizeInKB:25->51;InitialDelayInKB:12->25; |
| 60_rgb4 | 5 | 5 | 0 | BufferSizeInKB:25->51;ChromaFormat:1->3;InitialDelayInKB:12->25; |
| 60_tu4 | 5 | 5 | 0 | BufferSizeInKB:25->51;InitialDelayInKB:12->25; |
| 60_vme_cbr12_ldb | 5 | 5 | 0 | BufferSizeInKB:25->51;InitialDelayInKB:12->25;LowDelayBRC:16->32; |
| 60_vme_vbr12_ldb_maxframe | 5 | 5 | 0 | BufferSizeInKB:25->51;InitialDelayInKB:12->25; |
| 60_vme_intra_vertical | 5 | 5 | 0 | BufferSizeInKB:25->51;InitialDelayInKB:12->25; |

## Latency and bitstream validation
- [V] 600 unique synthetic frames per combination: translating gradient, scrolling stripes, binary frame counter.
- [V] QPC interval starts immediately before EncodeFrameAsync and ends after SyncOperation; includes busy retries if any.
- [V] CPU synthesis, UpdateSubresource, upload GPU-event completion, parsing and file writes are excluded.
- [V] Unpaced throughput run, timestamps at nominal fps. Bitrate = total bytes * 8 / (600/fps), not wall-clock throughput.
- [V] No warm-up frames discarded; nearest-rank percentiles include first frame. Target=12 Mbps for every run below.
- [V] Annex-B parser checks forbidden_zero_bit and counts first_mb_in_slice=0 AU starts, IDR AUs, SPS and PPS.
- [V] This is the requested minimal-NAL validation alternative; it does not establish decoded pixels or receiver compatibility.

| Combination (fps prefix) | Frames in/out | P50 / P95 / P99 / max ms | Avg / P99 / max bytes | IDR max bytes | Mbps | AU / IDR | Valid |
|---|---|---|---|---:|---:|---|---|
| 60_cbr12_ldb | 600/600 | 5.8924 / 10.6184 / 11.6766 / 41.3842 | 24968 / 39854 / 47710 | 26368 | 11.9846 | 600/1 | 1 |
| 30_vbr12_ldb_maxframe | 600/600 | 5.9602 / 10.9242 / 13.268 / 20.7475 | 41515.7 / 74083 / 74882 | 47833 | 9.96376 | 600/1 | 1 |
| 60_intra_vertical | 600/600 | 6.1197 / 10.6299 / 13.4197 / 46.5426 | 25058.2 / 42398 / 58945 | 26368 | 12.0279 | 600/1 | 1 |
| 60_vme_cbr12 | 600/600 | 10.763 / 20.9425 / 29.0461 / 74.3736 | 24997.2 / 26366 / 48855 | 48855 | 11.9986 | 600/1 | 1 |
| 30_base_lp_cbr12 | 600/600 | 6.1813 / 11.6025 / 18.3642 / 35.9131 | 49915.1 / 55455 / 86283 | 47933 | 11.9796 | 600/1 | 1 |
| 60_rgb4 | 600/600 | 12.6473 / 23.0833 / 28.8818 / 49.5557 | 25008.8 / 41187 / 56024 | 24341 | 12.0042 | 600/1 | 1 |
| 60_vbr12_ldb_maxframe | 600/600 | 9.7389 / 11.4863 / 16.4235 / 26.5716 | 19011.5 / 27683 / 35618 | 26911 | 9.12552 | 600/1 | 1 |
| 60_vbv3 | 600/600 | 5.8966 / 10.7813 / 14.03 / 31.031 | 24936.7 / 58573 / 66714 | 26368 | 11.9696 | 600/1 | 1 |
| 60_vme_vbr12_ldb_maxframe | 600/600 | 10.9144 / 24.0482 / 29.6919 / 40.4545 | 16614.6 / 25200 / 33251 | 33251 | 7.97499 | 600/1 | 1 |
- [V] Each run: 1 SPS, 600 PPS, 0 invalid NAL headers, 0 busy retries, 0 MORE_DATA input delays; all 600 outputs synchronized successfully.
- [V] CBR+LowDelayBRC request is included as required, but effective LowDelayBRC=OFF; this driver does not honor ON with CBR.
- [V] VBR+LowDelayBRC+MaxFrameSize retained ON and a 75000-byte limit at 30 fps; measured maximum=74882 bytes.
- [V] Intra vertical retained type=1, cycle=60, QPDelta=-2. VME fallback retained LowPower=OFF.
- [V] Saved .h264 lengths match the sum of all 600 per-frame byte counts for all nine runs (artifact-check.csv).

## Recommendation for design section 3.4
- [I] Keep LowPower ON, TU7, AsyncDepth 1, no B frames, one reference, Constrained Baseline and the 4.0/4.2 level selection.
- [I] Start with CBR 12 Mbps and LowDelayBRC OFF for predictable target bitrate; use the read-back VBV minimum (101 KB at 30 fps, 51 KB at 60 fps), or explicit 3-frame VBV if warning-free initialization is preferred.
- [V] One-frame VBV is not honored: the read-back capacity is about two frames (67.3 ms at 30 fps / 34 ms at 60 fps), not the requested 33.3 / 16.7 ms. This is VBV capacity in nominal stream time, not measured encoder delay.
- [I] Document CBR+LowDelayBRC as unsupported in effective settings; Init >= 0 alone is insufficient for capability selection.
- [I] Keep the alternative VBR target=max 12 Mbps, LowDelayBRC ON, CO2 MaxFrameSize=1.5x average; benchmark quality/bitrate on browser content before selecting it.
- [V] VBR achieved 9.964 Mbps on this content and stayed below its 75000-byte frame cap; CBR achieved 11.985 Mbps.
- [I] Prefer CBR for the LowPower-OFF fallback among the two measured VME modes: CBR achieves 12.0 Mbps and P99=29.05 ms; VBR+LowDelayBRC+MaxFrameSize achieves 7.97 Mbps and P99=29.69 ms. These are single runs, not a statistically established ranking.
- [I] Keep BGRA-to-NV12 VPP as the default; RGB4 performance below excludes upload and does not measure VPP versus encoder-internal CSC end to end.
- [V] No edits to design/03-video-pipeline.md were authorized by the task path constraints; suggested changes are recorded here.

## Surprises and limitations
- [V] All rows accept external video input, including vertical/horizontal refresh, 2/4 slices, MaxSliceSize=1400, Main, High and RGB4.
- [V] RGB4 read-back changes input ChromaFormat from YUV420 (1) to YUV444 (3); FourCC stays RGB4. Actual encoded stream chroma was not parsed.
- [V] Plain VBR defaults MaxFrameSize to 783360 bytes; enabling LowDelayBRC without an explicit cap defaults it to one average frame (50000/25000 bytes at 30/60 fps).
- [V] Three-frame VBV rows alone have Query=Init=0; all other rows return warning 5, which the probe preserves.
- [V] At 60 fps, RGB4 P99=28.88 ms; the selected NV12 LowPower runs span 11.68-16.42 ms. RGB4 input CSC has no measured advantage here.
- [V] At 60 fps, 3-frame VBV P99=14.03 ms and max frame=66714 bytes versus 11.68 ms / 47710 bytes with driver-minimum VBV; one run per condition.
- [I] Read-back is not proof that intra-refresh or slice-size limits take effect in produced macroblocks/slices; nine selected rows were encoded, not every matrix row.
- [I] Synthetic unpaced measurements omit capture, VPP, network, receiver and browser GPU contention; they do not establish end-to-end SLOs or visual quality.

## Exact build and run commands
Run these from C:/Users/Flysoft/Desktop/remotekit in PowerShell; all outputs stay under C:/src/tmp/s7.
```powershell
New-Item -ItemType Directory -Force C:/src/tmp/s7/vendor | Out-Null
git -C C:/Users/Flysoft/Desktop/chromium archive -o C:/src/tmp/s7/libvpl.tar wip/e1-onevpl prtsd/vendor/libvpl
# This machine resolves tar to Git's MSYS tar (use /c paths).
tar -xf /c/src/tmp/s7/libvpl.tar -C /c/src/tmp/s7/vendor
cmd /c 'tools\s7-vpl-probe\build.bat > C:\src\tmp\s7\build.log 2>&1'
C:/src/tmp/s7/build/s7probe.exe --enumerate | Tee-Object C:/src/tmp/s7/enumerate.log
C:/src/tmp/s7/build/s7probe.exe --matrix | Tee-Object C:/src/tmp/s7/matrix.log
C:/src/tmp/s7/build/s7probe.exe | Tee-Object C:/src/tmp/s7/run.log
foreach ($row in '60_vme_vbr12_ldb_maxframe','60_vbr12_ldb_maxframe','30_base_lp_cbr12','60_rgb4','60_vbv3') {
  C:/src/tmp/s7/build/s7probe.exe --row $row | Tee-Object "C:/src/tmp/s7/$row.log"
}
```
- [V] Default run repeats the matrix, then encodes the four required accepted combinations; process exit=0.
- [V] --row runs one named matrix/latency combination and writes <row>-matrix.csv / <row>-latency.csv; --frames N overrides the default 600.
- [V] Outputs: matrix.csv, latency.csv, <row>-frames.csv, <row>.h264, build/s7probe.exe and logs under the task temp directory.
- [V] build.bat optionally accepts the extracted libvpl root as its first argument; cached vpl.lib avoids rebuilding the dispatcher.
- [V] Pre-existing and concurrent changes outside the two allowed deliverable paths were left untouched; this task made no git commits.
