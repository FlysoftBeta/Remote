# Task e2: zero-copy D3D11 -> oneVPL (legacy Media SDK runtime) on Intel UHD 630 (bounded review task)
Output file: C:\Users\Flysoft\Desktop\remotekit\research\e2-vpl-zero-copy.md   Time limit: 25 min.
Context: the browser exports each frame as a BGRA8 D3D11 texture shared via NT handle + IDXGIKeyedMutex (prior art,
a-prior-art.md section 3). The endpoint (Rust, windows-rs 0.62) opens it on its own Intel D3D11 device. UHD 630 is
Gen9.5: the oneVPL dispatcher loads the legacy Media SDK runtime libmfxhw64.dll (API 1.x).
Prior art has a parked oneVPL encoder on branch wip/e1-onevpl of C:\Users\Flysoft\Desktop\chromium (read-only):
read with `git -C C:\Users\Flysoft\Desktop\chromium show wip/e1-onevpl:prtsd/native/vpl.cpp` (also
prtsd/src/vpl_encoder.rs, prtsd/build.rs; and the current prtsd/src/d3d.rs on the checked-out branch). Do not check out.
Questions:
1. Summarise exactly what the parked bridge does: session creation (MFXLoad/MFXCreateConfig filters, implementation
   selection), SetHandle(MFX_HANDLE_D3D11_DEVICE), external frame allocator (how textures are created: format,
   BindFlags, MiscFlags, arrays vs single), the encode loop (EncodeFrameAsync/SyncOperation), parameters set, how it
   gets NV12 (who converts BGRA->NV12 and how), and where copies happen. Cite function names / line ranges.
2. Copy count from the browser's shared BGRA texture to the H.264 bitstream in prior art (list every GPU copy /
   conversion / CPU sync), and which could be removed: e.g. (a) VideoProcessorBlt directly from the opened shared
   texture into the encoder's NV12 surface (keyed mutex held only during the Blt), (b) feeding RGB4 (BGRA) directly
   to the AVC encoder (does MSDK on Gen9 accept MFX_FOURCC_RGB4 encoder input with internal CSC? cite Intel docs or
   MSDK source _studio/mfx_lib/encode_hw/h264), (c) opening the shared texture as an encoder surface (requirements
   on bind flags; why it probably fails).
3. Synchronisation: keyed mutex acquire/release points, D3D11 query/fence use, multithread protection
   (ID3D10Multithread / ID3D11Multithread), and whether `SyncOperation` blocks on GPU completion of the VPP blit.
4. A concrete minimal-latency per-frame sequence (<= 12 steps) for the endpoint with expected GPU cost at 1080p on
   UHD 630 if any measurement exists in prior-art docs (C:\Users\Flysoft\Desktop\chromium\docs\prts\*, journals);
   mark measured vs estimated.
5. Risks/unknowns list (<= 10 bullets) to settle in a spike.
