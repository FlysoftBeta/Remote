# NVENC header

`nvEncodeAPI.h` is unmodified from FFmpeg/nv-codec-headers tag **n12.1.14.0**:
https://github.com/FFmpeg/nv-codec-headers/blob/n12.1.14.0/include/ffnvcodec/nvEncodeAPI.h

SHA-256: `dbdaa8bcbc2b9325bd24bc68b36170a8432411518040e8d4c8421608cf217425`.
The header's MIT notice is retained in the header and copied to `LICENSE`.
`UPSTREAM_README` is the upstream tag's README. No SDK binary is redistributed.
The endpoint dynamically loads the installed driver's System32 `nvEncodeAPI64.dll`.
Requires an NVENC-capable NVIDIA adapter and driver supporting SDK API 12.1.

**Untested on NVIDIA hardware.** Compile and graceful-unavailability checks were
performed on an Intel-only host. Preset, rate-control, resource lifecycle, resize,
and bitstream behavior still require NVIDIA device verification.
