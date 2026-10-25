# Task S7: oneVPL parameter probe on the UHD 630 (standalone program)
Deliverables (only these paths): `spikes/s7-vpl-probe/` (source + build script), report `research/s7-onevpl-probe.md`.
Background: design/03-video-pipeline.md section 3.2 (read it), research/e2-vpl-zero-copy.md (skim). Prior-art bridge:
`git -C C:\Users\Flysoft\Desktop\chromium show wip/e1-onevpl:prtsd/native/vpl.cpp` and `...:prtsd/build.rs`; the vendored
dispatcher is under `wip/e1-onevpl:prtsd/vendor/libvpl/` (extract with `git archive wip/e1-onevpl prtsd/vendor/libvpl | tar -x -C <dir>`
into spikes/s7-vpl-probe/vendor, it is OK to copy). The UHD 630 is served by the legacy MSDK runtime (libmfxhw64), not oneVPL GPU RT.

Build a small C++ console program (cl.exe via a .bat or CMake, static dispatcher, D3D11 device on the Intel adapter selected by LUID/vendor)
that:
1. Lists implementations (MFXEnumImplementations: impl name, ApiVersion, AccelerationMode, VendorImplID) and picks the hardware one.
2. For H.264 encode at 1920x1080@60 and 1280x720@60, runs a matrix and for each row records Query result, Init result
   (status codes incl. warnings) and GetVideoParam read-back of the fields below:
   - LowPower ON/OFF (VDEnc vs VME);  - RateControl CBR, VBR, and CBR+LowDelayBRC (mfxExtCodingOption3);
   - MaxFrameSize (ext CO2) set/unset;  - IntRefType=1 (rolling intra refresh) with IntRefCycleSize;
   - CodecLevel 4.0, 4.2, 5.1;  - input FourCC NV12 vs RGB4 (ARGB, with and without VPP needed);
   - AsyncDepth=1, GopRefDist=1, NumRefFrame=1, TargetUsage=7 (best speed), Baseline vs Main vs High profile.
   Use system or video memory as simplest works; note which IOPattern was used.
3. For up to 4 promising combos (at least: LowPower+CBR+LowDelayBRC NV12 1080p60 L4.2), encode 300 synthetic frames (moving gradient + frame
   counter block), write .h264 files to %TEMP%\s7, measure per-frame submit->output latency (P50/P99 ms) and bitrate accuracy at 12 Mbps,
   and verify decodability (MFT decoder or ffprobe if already installed; otherwise parse SPS/PPS and check the frame count of NAL units).
Report: the matrix table, the recommended combination for 03 section 3.2, surprises (silently overridden fields), build/run commands.
Time limit is hard; prioritize: build+enumerate (1), then the 1080p matrix (2), then (3).
