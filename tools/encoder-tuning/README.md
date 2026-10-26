# Standalone encoder tuning

Report: [`research/t1-encoder-tuning.md`](../../research/t1-encoder-tuning.md).
Artifacts: `C:/src/tmp/t1-encoder-tuning/` only. No Chromium builds or product edits.

Prerequisites: VS 2026 Community, Git's curl/bzip2, Node 24, Intel UHD 630 legacy Media SDK runtime, and the S7 dispatcher/header cache under `C:/src/tmp/s7/`. `s7_support.h` is a local copy of the S7 standalone GPU/allocator/parameter helpers, not a product encoder.

From the repository root in PowerShell:

```powershell
./tools/encoder-tuning/setup.ps1
# Optional alternate name permits rebuilding while an old binary is running.
cmd /c 'tools\encoder-tuning\build.bat tune-next'
node tools/encoder-tuning/experiment.mjs initial
./tools/encoder-tuning/run-batches.ps1
$env:TUNING_EXE='C:/src/tmp/t1-encoder-tuning/build/tune-next.exe'
node tools/encoder-tuning/followup.mjs headroom
node tools/encoder-tuning/followup.mjs scene
node tools/encoder-tuning/followup.mjs rescue
./tools/encoder-tuning/final-batches.ps1
node tools/encoder-tuning/followup.mjs software-idr
node tools/encoder-tuning/followup.mjs vme-repeat
node tools/encoder-tuning/followup.mjs vme30
node tools/encoder-tuning/analyze.mjs --show
node tools/encoder-tuning/inspect.mjs
node tools/encoder-tuning/audit.mjs
node tools/encoder-tuning/aggregate.mjs
node tools/encoder-tuning/report-data.mjs
```

The PowerShell batch wrappers restrict affinity to eight logical processors and set below-normal priority; the C++ encoder also enforces those limits. Experiments and quality passes are sequential. `experiment.mjs` resumes by unique run name: existing run metadata prevents accidental re-encoding. Use a new name for a fresh run. Rerunning `setup.ps1` regenerates the natural reference, so finish all runs against one fixed media set.

One explicit normalized default:

```powershell
C:/src/tmp/t1-encoder-tuning/build/tune-next.exe `
  --name example --input C:/src/tmp/t1-encoder-tuning/media/scroll-60.nv12 `
  --fps 60 --frames 600 --kbps 10000 --mode vbr --ldb 1 --cap 1 `
  --buffer 42 --initial 20 --mbbrc 32
```

Use `run(...)` from `experiment.mjs` to include decoded quality and a reproducible run manifest; invoking the binary alone only writes parameters, bitstream, and raw frame timings. A zero-frame VPL run probes Query/Init/readback without encoding. Binary exit 2 means initialization was rejected, not an experiment with valid output.

Main binary parameters (all supplied as `--key value`):

| Parameters | Meaning/default |
|---|---|
| `backend` | `vpl` or `openh264` |
| `input`, `name`, `frames`, `fps`, `kbps` | Packed 1920×1080 NV12 file; unique output stem; 600, 60, 12000 |
| `mode`, `quality` | VPL `cbr`, `vbr`, `qvbr`, `icq`; quality factor 26 |
| `lp`, `tu`, `refs`, `slices`, `profile` | 1, 7, 1, 1, `cb` (`main`/`high` alternatives) |
| `vbv`, `buffer`, `initial` | Requested VBV in nominal frames, or explicit SDK KB values |
| `ldb`, `cap`, `cap-i`, `cap-p` | LowDelayBRC boolean; frame-size caps as multiples of average frame budget |
| `gop`, `intra`, `cycle`, `intra-qp`, `intra-dist` | VPL 65535, refresh type 0/1/2, cycle fps, QP delta -2 when enabled, distance 0 |
| `adaptive-i`, `force-idr` | Adaptive-I boolean; input frame to force IDR, default none |
| `mbbrc`, `panic`, `maxqp`, `winbrc` | Raw SDK tri-state values for first two (0/16/32), QP limit, sliding-window frames |
| `paced` | 1 sleeps to nominal source cadence; 0 isolates unpaced service time |
| `usage`, `threads`, `rc`, `skip` | OpenH264 screen/camera, 4, numeric RC mode 1, frame-skip boolean 0 |
| `complexity`, `scene`, `background`, `aq` | OpenH264 defaults 0, 0, 1, 0; consult effective readback/encoder warnings |
| `idr-ratio`, `fix-overshoot` | OpenH264 target IDR budget in percent (400) and overshoot adjustment (1); not a hard frame bound |

Quality uses PSNR-Y/SSIM-Y plus per-frame logs. Skipped software frames are expanded with hold-last-picture before comparison against every source instant. Decoder intermediates for such runs stay in task results. `roi.mjs RUN ...` additionally measures the text column or the natural clip's active picture. `analyze.mjs` writes all per-frame modeled link metrics and summary tables; `inspect.mjs` parses SPS/NAL/slice headers and size-cap violations; `audit.mjs` validates counts, decoding, statuses and byte sums. `aggregate.mjs` pools four selected repeats, resetting the modeled link between independent runs.

All timing/queue definitions and known limitations are in the report. Retained SDK parameters, including `MaxFrameSize`, are not output guarantees on this driver.
