# Task b2: FrameSinkVideoCapturer as the frame-export primitive (verification task)
Output file: C:\Users\Flysoft\Desktop\remotekit\research\b2-capture.md   Time limit: 25 min.
Prior art captured the Aura root FrameSinkId with viz::ClientFrameSinkVideoCapturer, ARGB,
BufferFormatPreference kPreferMappableSharedImage, and got a DXGI shared handle (see a-prior-art.md section 3 only;
prior patches C:\Users\Flysoft\Desktop\chromium\patches\0015*, 0021*, 0013* if needed).
Questions, at the pin:
1. In components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.cc: the exact sequence from a
   new CompositorFrame on the target sink (OnFrameDamaged / OnBeginFrame?) to the CopyOutputRequest being issued and the
   result delivered (which Display draw / which frame does the copy observe; is the copy executed during the same
   DrawAndSwap; any extra frame of latency?). Name functions.
2. Supported pixel formats and buffer types at the pin (media::PIXEL_FORMAT_ARGB / NV12 / I420; mojom
   BufferFormatPreference values; which path yields GPU-resident output on Windows; is NV12 GPU output supported so an
   encoder could take NV12 directly?). How is the D3D11 handle produced (shared image backing type, keyed mutex or not)?
3. Is the "failed/invalidated capture never calls VideoCaptureOracle::CompleteCapture" bug that prior patch 0013 fixed
   still present at the pin? Quote the relevant code.
4. Pacing knobs: SetMinCapturePeriod, SetMinSizeChangePeriod, SetAutoThrottlingEnabled, RequestRefreshFrame, oracle
   behaviour on static content; can the consumer force a capture of the next frame with low delay?
5. CEF's current accelerated OSR ("shared texture", OnAcceleratedPaint): confirm from the CEF source on GitHub
   (chromiumembedded/cef, libcef/browser/osr/*video_consumer_osr*, render_widget_host_view_osr.cc) whether it uses
   FrameSinkVideoCapturer and which format/buffer settings, and how CEF does external BeginFrames
   (OnBeginFrame / SendExternalBeginFrame). Cite file names. Max 10 minutes on this question.
