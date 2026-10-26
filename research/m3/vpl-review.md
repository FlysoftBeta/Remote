# M3 VPL / D3D native path review (read-only, static)

Files: endpoint/vpl_encoder.{h,cc}, d3d_native_frame.cc, d3d_frame_source.cc, peer.cc.
Paths below are under C:\Users\Flysoft\Desktop\remotekit\endpoint. Not built or run.
Confidence: H = traced end to end in code, M = plausible from code plus API semantics, L = depends on driver or libwebrtc behaviour I could not check.

Checked and found OK (no finding):
- Every OnFrame path replies exactly once.
- AcquireSync/ReleaseSync are paired on every path that acquired.
- The Slot lease and use_count()==1 scheme is sound: the encoder syncs synchronously, so Locked is 0 before reuse.
- MFX Close ordering is correct, and MFXLoad/MFXUnload and implementation-description releases are paired.
- Query/Init/Reset Parameters copies rebind their extension pointers.
- NV12ToI420 is a pure plane copy, so BT.709 limited range is preserved. The UV plane offset of RowPitch*desc.Height is the usual convention.
- No HANDLE leak found. NewBuffer's PlatformHandle closes at scope exit.
- RTP timestamp, capture_time and ntp propagation is consistent with libwebrtc encoders.

## 1. [HIGH, conf M-H] Zero-copy path is disabled for the rest of the process after one transient native-pool error, and a GPU crash reliably triggers it
- d3d_native_frame.cc:51-56 (Fail sets healthy_=false, never reset), :195-199, :171, :184, :207-209, :227.
- d3d_frame_source.cc:126-128 (the pool is rebuilt only if luid_ changed), :254-255 (gate on healthy()), :310-329 (Reset() only calls the no-op ClearImports).
- Scenario: Chrome's GPU process crashes or restarts while it holds keyed-mutex key 0. The next AcquireSync returns WAIT_ABANDONED or DEVICE_REMOVED (not WAIT_TIMEOUT), so Fail("AcquireSync") runs. The same happens for any ReleaseSync != S_OK, a VideoProcessorBlt failure, or an odd or out-of-range visible rect ("source descriptor").
- The LUID is unchanged after the GPU restart, so Bind() keeps native_pool_ with healthy_=false. Every later frame takes the CPU staging/ARGBToI420 path (slower, higher latency) with no further log.
- Impact: silent permanent loss of the main latency win after exactly the recovery scenario the last commits targeted.
- Fix:
  - On Reset() and Bind(), recreate the pool (native_pool_ = make_unique<D3DNativeFramePool>(device_, fps_)) or add a pool Revive().
  - Treat WAIT_ABANDONED and mutex errors as per-buffer failures: erase that buffer and wait for NewBuffer. Do not poison the pool.
  - Reserve healthy_=false for device-level failures (CreateVideoProcessor, format unsupported, device removed), and retry those on a timer.

## 2. [HIGH, conf M-H] Rate decreases ignored by the throttle are never re-applied
- vpl_encoder.cc:817-836 (ResetRate), :1162-1176 (SetRates), :823-826.
- ResetRate drops any request when frames_ < 2*fps, within 2 s of last_reset_ (set in Start), or with |delta| < 20%. It stores nothing.
- libwebrtc only calls SetRates when the allocation changes. If GCC steps from 8 to 3 Mbps at t=1.5 s and stays there, the call is dropped and the encoder stays at kbps_=12000 (Start uses max) until some later rate change. The same applies to a decrease of less than 20% that stays flat.
- Impact: sustained overshoot (12 Mbps VBR on a 3 Mbps path), so queuing, loss and latency under congestion. This is the case that matters most.
- Fix:
  - Keep pending_kbps_ in ResetRate and remember the last wanted value.
  - In Encode(), once the gate opens (2 s elapsed, or frames_ >= 2*fps), apply pending_kbps_ if it is still outside the 20% band.
  - Let decreases of 30% or more bypass the 2 s limit, if the no-IDR Reset is truly seamless.
  - Consider applying the first SetRates (frames_==0) before the first frame, or initializing with codec->startBitrate, instead of encoding the first 2 s at max_kbps (line 1141). That startup burst (12 Mbps vs the 8 Mbps start estimate) is itself a loss risk on constrained links.

## 3. [MEDIUM-HIGH, conf M] Close() on one encoder instance clears the shared encoder_device_ of another
- vpl_encoder.cc:757-759 (state_->SetEncoderDevice(nullptr) unconditionally), :751. NativeActive is at :406-410.
- Scenario: a session restart or renegotiation creates a new VplEncoder (factory Create) and calls InitEncode, while the previous instance is still being torn down on the old encoder queue or worker. The old instance's Close() runs after the new Start() and sets encoder_device_ to null.
- NativeActive then stays false until the next Start, so the capture side stays on the CPU I420 bridge.
- Fix: in Close(), clear only if the state still holds this instance's device (compare against device_.Get()). Better, a compare-and-clear: ClearEncoderDevice(ID3D11Device* mine).

## 4. [MEDIUM-HIGH, conf M] Transient conditions are treated as fatal: Fail() sends this encoder instance to OpenH264 for good
- vpl_encoder.cc:959-962 (device busy > 100 ms), :936-938 (no unlocked slot), :967-968, :970-972 (Sync timeout 1000 ms), :976-981 (DataLength==0).
- The SoftwareFallbackWrapper does not retry the primary encoder until the next InitEncode, which happens on resolution change or renegotiation.
- This is a remote-desktop host that can be GPU-saturated by a game. A 100 ms busy period permanently demotes the session to OpenH264. A zero-length AU can also come from legitimate BRC skip with MaxFrameSize plus LowDelayBRC.
- Impact: unexpected quality or CPU cliff, with no recovery.
- Fix:
  - Device busy, no free surface, DataLength==0 and the first Sync timeout: drop the frame (callback_->OnFrameDropped, return OK) and set force_idr_ if a reference may be lost.
  - Fail only after N consecutive failures, or on DEVICE_LOST, GPU_HANG or NOT_INITIALIZED.

## 5. [MEDIUM, conf M] Hybrid-GPU hosts: the encoder dies when the capture device is not Intel, although its own Intel device would work
- vpl_encoder.cc:529-561 (OpenDevice requires vendor 0x8086 for the supplied device), :917-930 (Start(..., capture_device)), :1144-1146.
- Scenario: laptop where Chrome renders on an NVIDIA or AMD GPU, so the capture LUID is not Intel. IsAvailable() succeeds (it probes Intel). InitEncode uses state_->capture_device() (non-Intel), so OpenDevice returns false, Fail, and the session uses OpenH264. Alternatively the encoder starts on its own Intel device and the first I420 frame sees capture_device != device_ (:924-929), which calls Start(non-Intel) and then Fail. A working VPL encoder is torn down by the first frame.
- Fix: if the capture device is not an Intel adapter (or lacks VIDEO_SUPPORT), ignore it and use the encoder's own Intel device on the I420 upload path. Never restart the encoder just because capture_device differs for I420 frames. A native frame is the only case needing device equality.

## 6. [MEDIUM, conf M] Native frame from a previous capture device restarts the encoder onto the OLD device, then again onto the new one
- vpl_encoder.cc:917-930 and :861-864.
- Encode() overwrites capture_device with the frame's own device, so a queued frame from device A while device_==B (new) calls Start(A) (full MFXLoad, session, Init, IDR). The next frame from B restarts again.
- Impact: two full re-inits (hundreds of ms each, blocking the encoder queue), plus Fail risk (#4) on each.
- Fix: if a native frame's device != device_, convert it with ToI420() and take the I420 upload path (or drop it). Only restart when the state's capture_device differs AND native frames from it are arriving.

## 7. [MEDIUM, conf L-M] VplNativeFrameBuffer::ToI420 failure returns nullptr to libwebrtc, and the slow path is expensive and noisy
- vpl_encoder.cc:463-499; also CropAndScale :511-513 and GetMappedFrameBuffer :519.
- It creates a staging NV12 texture per call (:478). NV12 staging with CPU read is not supported on every driver, so CreateTexture2D can return E_INVALIDARG. It then returns nullptr, and callers such as the OpenH264 wrapper or CropAndScale may dereference or reject it.
- It does a blocking Map on the encoder thread and an INFO log per frame (:465). At 60 fps that is 60 log lines/s plus a GPU stall.
- Correctness otherwise OK: NV12ToI420 is a copy, and range and matrix are preserved because VPP output is BT.709 limited and the frame color space is declared the same.
- Fix: cache one staging texture per buffer size, log once, and make failure explicit (Fail and have the capture side drop to I420).

## 8. [LOW-MEDIUM, conf M] Keyframe, VBV and MaxFrameSize interplay
- vpl_encoder.cc:240-242, :1064.
- BufferSizeInKB = one frame time at max rate (~50 KB at 12 Mbps/30 fps), and MaxFrameSize is the same. IDRs are therefore hard-capped to about 50 KB. The runtime may re-encode at higher QP (extra latency) or produce a blurry first picture and slow text refinement after keyframe requests (PLI and on_reset_).
- Not a crash; a quality and latency risk. cap_violations_ is counted but only logged at VERBOSE.
- Suggest: allow a larger VBV (2-3 frames) while keeping MaxFrameSize for P frames. Measure first.

## 9. [LOW-MEDIUM, conf M] codec->maxBitrate == 0 becomes a 1 kbps encoder
- vpl_encoder.cc:1136-1137: clamp(codec->maxBitrate, 1u, 20000u).
- If libwebrtc passes an unset max, the stream is created with MaxKbps=1/TargetKbps=1. Peer sets 12 Mbps first, so this is only latent.
- Fix: if maxBitrate==0, fall back to state_->max_bitrate_bps/1000.

## 10. [LOW, conf M] Start-during-Encode cost
- vpl_encoder.cc:662 (Start calls Close first) and :928.
- Start() blocks the encoder queue for a full MFXLoad, enumeration and Init (hundreds of ms) when triggered inside Encode() by resize or device change. Frames queue behind it and are dropped by libwebrtc. During the gap NativeActive is false, so capture switches to the CPU path and back. Acceptable for rare events, but combined with #5/#6 it can repeat.

## 11. [LOW, conf H] LowDelayOutput timing_ eviction erases the smallest RTP timestamp, not the oldest
- vpl_encoder.cc:336-337. With a random RTP offset and wraparound, begin() is not the oldest, so stats can be dropped or mis-attributed. Latency stats only.
- Fix: use a small ring or erase by insertion order.

## 12. [LOW, conf M] Silent drops and a retry stall in the import path
- d3d_frame_source.cc:178-184: NewBuffer erases the old id first, then silently drops the new buffer beyond 64 entries or on a descriptor mismatch (no log). OnFrame then drops with "known=0" logged only every 100th time.
- d3d_frame_source.cc:275-279: AcquireSync WAIT_ABANDONED in the CPU path leaves the buffer in buffers_ and retries every frame (20 ms stall each). Erase the buffer on non-timeout errors, same as #1.
