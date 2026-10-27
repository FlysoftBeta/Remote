# 03 视频管线、帧节奏与实验计划

版本：clean-room 复核版（2026-10-25）。依据：`research/r-chromium-map.md`、`b1-frame-path.md`、`b2-capture.md`、`b3-pacing.md`、`g-h264-level.md`、
上游 `remoting/` 与 libwebrtc 源码（同 pin）。目标：1080p，10-16 Mbps，RTT 50 ms + 抖动，一端 Wi-Fi；capture→display P80 < 100 ms、P99 < 200 ms；
接收端下限 Chrome 89 + H.264。所有数字在 M3 测量前都是估计。

## 1. 管线总览

```text
[GPU process / viz]                        [browser process]                 [rb_legacy_endpoint (C++, in-tree)]
FrameSinkVideoCapturerImpl (per browser root)
  OnFrameDamaged → oracle → CopyOutputRequest (blit into D3D11 keyed-mutex pool, same DrawAndSwap)
  OnFrameCaptured(NT handle, info, rects) ──(Mojo; directly to endpoint if S10b, else via browser)──► FrameImport
                                                                                     open-once cache per buffer
                                                                                     AcquireSync(0) → VPP BGRA→NV12
                                                                                     ReleaseSync(0) → Done()
                                                                                     oneVPL AVC encode (AsyncDepth 1)
                                                                                     libwebrtc: RTP, pacer, GCC/TWCC, NACK/RTX
                                                                                     → UDP → viewer
```
D4b 成立时每个浏览器窗口只有一个源，endpoint 不做弹层合成。多窗口时 endpoint 只编码“当前显示的窗口”（切换时请求刷新 + IDR）。

## 2. 浏览器/viz 侧捕获
- 配置见 02 §2.2。静态页面无损伤不产帧：新观众/关键帧需要时 `RequestRefreshFrame()`（可复活旧帧，无需新 GPU 拷贝，b2 §4）。必带补丁 04（CompleteCapture）。
- resize：尺寸变化时丢弃 `coded_size/visible_rect/content_rect` 与当前根不符的帧，重新配置编码器并 IDR。
- 每帧元数据：`frame_seq`、`buffer id`（GMB `DXGIHandleToken`）、coded size、visible/content rect、`capture_begin/end_time`、`reference_time`。
- 投递延迟来源（r-chromium-map §2）：mappable blit 结果在 GPU 完成后才投递（finished-proc + ≤2 ms 轮询）。S4 测量 `capture_end - DrawAndSwap` 分布。

## 3. endpoint：语言、WebRTC 库、编码器与零拷贝（重新决策）

### 3.1 候选与评估

| 方案 | 传输成熟度（P99 关键） | 编码器/零拷贝集成 | 与 Chromium 侧耦合 | 构建/维护 | 结论 |
|---|---|---|---|---|---|
| **C++，Chromium 树内，libwebrtc（pin 版）** | 最高：与 Chrome 接收端同源；GCC/TWCC + 探测 + pacer、NACK/RTX、ULP/FlexFEC、playout-delay、abs-capture-time、frame dropper 均为 Chrome 默认路径 | oneVPL/D3D11 都是 C/C++ 原生；native `VideoFrameBuffer` 可携带纹理；`media::VideoEncodeAccelerator`（MF）现成回退 | 共享 `.mojom`、`gfx::GpuMemoryBufferHandle`、同 pin 同构建 | 复用现有 checkout 与工具链；增量构建只编 endpoint 目标；每次 rebase 一起重建 | **采用** |
| C++，独立 libwebrtc 检出 | 同上 | 同上 | 无 Mojo（需自定义 IPC） | 第二套 gclient（~20 GB）、工具链与 libc++ ABI 对齐、版本漂移 | 否决 |
| Rust + webrtc-rs | 发送端 BWE/pacer 的成熟度与 Chrome 互操作在此无源码级验证；Stage 1 自己把它列为风险 R5/S9 | oneVPL 需 C++ 桥（Rust 方案本来就要写 `vpl.cpp`）；D3D11 经 windows-rs 可行 | 需自定义 IPC 与协议重复定义 | 独立 cargo 构建，迭代快 | 否决 |
| Rust + str0m | sans-IO，带 BWE/pacer，生产使用较少 | 同上 | 同上 | 同上 | 候选（若 libwebrtc 集成受阻） |
| 把 endpoint 放进浏览器进程 | 同 libwebrtc | 可直接拿 SharedImage | 最紧 | 补丁面最大；编码/网络进入将来要沙箱化的进程；一个 instance 崩溃面扩大 | 否决 |

上游证据：`remoting/`（CRD host）就是树内原生主机：`remoting/protocol/chromium_socket_factory.*`/`chromium_port_allocator_factory.*`（libwebrtc + Chromium 网络栈）、
`remoting/codec/webrtc_video_encoder_gpu.cc`（Windows MF H.264）、`remoting/protocol/webrtc_video_encoder_wrapper.cc:412-413`（`VideoPlayoutDelay::Minimal()`、`SCREENSHARE`）。
它是结构参照（PeerConnectionFactory/线程/socket 工厂/编码器包装的写法），不是要复用的产品代码。

### 3.2 libwebrtc 集成要点
- `PeerConnectionFactory` 用自定义 `VideoEncoderFactory`（只给 H.264 CB/`packetization-mode=1`），无音频设备（`AudioDeviceModule` 用 dummy）。
- 视频源：自写 `VideoTrackSource`，每帧推 `webrtc::VideoFrame`，buffer 为 native 类型（携带已转换的 NV12 编码器输入面 + 元数据）；
  `content_type = SCREENSHARE`（让接收端最小化缓冲）、`SetPlayoutDelay({0,0})`。
- 分辨率/帧率不让 libwebrtc 自适应降级（`DegradationPreference::MAINTAIN_RESOLUTION` 起步，E12 再评估），码率由 GCC 目标经 `SetRates` 下发编码器。
- socket：起步用 libwebrtc 自带 `BasicPacketSocketFactory`/`PhysicalSocketServer`；若在 `build_with_chromium` 配置下不可用，按 `remoting/` 做法基于 `net::UDPSocket`。（spike S9 第一问。）
- SDP：offer `profile-level-id=42e01f;packetization-mode=1;level-asymmetry-allowed=1`；Chrome answer 的 level 视为名义值，SPS 写真实 level
  （1080p30 → 4.0，1080p60 → 4.2，`g-h264-level.md`），S8 在 Chrome 89 与当前 Chrome 上验证。

### 3.3 零拷贝交接（默认，S3 验证）
1. 首见某 buffer：打开共享纹理（`ID3D11Device1::OpenSharedResource1`）并缓存；capturer 池重建时清缓存。
2. 每帧 `IDXGIKeyedMutex::AcquireSync(0, 短超时)`，只接受 `S_OK`（`WAIT_TIMEOUT`/`WAIT_ABANDONED` 是非负失败码）。
3. `ID3D11VideoContext::VideoProcessorBlt` BGRA→NV12（BT.709 limited，与 SPS VUI 一致）写入 endpoint 自有的可复用 NV12 纹理池
   （`D3D11_BIND_RENDER_TARGET`，单纹理非数组），1080→1088 对齐用 VP 的 source/dest/target rect；VP 与视图在 setup/resize 时创建缓存。
4. `ReleaseSync(0)` → 立即 `Done()`；不做 CPU 忙等（keyed mutex 在 GPU 侧串行化 Viz 的下一次写）。保守变体（S3 对照）：Blt 后等一次 `D3D11_QUERY_EVENT` 再释放。
5. 编码：同一设备/上下文上 VP 在前、编码读在后；`SyncOperation` 在编码线程阻塞，不得在持有 `ID3D11Multithread` 锁时调用。
- 变体 E8：capturer 直出 NV12（viz 在 GPU 上做 Rec.709 limited 转换），endpoint 跳过 VPP，但必须持有该池缓冲直到编码器读完（池深度 vs 延迟，E8 测）。
- 变体：AVC 编码器直接吃 RGB4（内部 CSC），需 `Query` 实测，默认不用。

### 3.4 编码器：oneVPL（UHD 630 / Gen9.5 → legacy Media SDK runtime）
- dispatcher（libvpl 2.x）`MFXLoad` + `MFXCreateConfig` 过滤：硬件实现、AVC 编码、`MFX_ACCEL_MODE_VIA_D3D11`；**不**把最低 API 设为 2.x（Gen9 runtime 是 1.x API）。
  记录实现、API 版本、runtime DLL、驱动版本、LUID。`SetHandle(MFX_HANDLE_D3D11_DEVICE)`（设备需 VIDEO_SUPPORT、multithread protected、LUID 与浏览器 GPU 一致）。
- 外部分配器：输入面由我们分配（见 3.3），`MFX_IOPATTERN_IN_VIDEO_MEMORY`。
- 起始参数（全部 `Query/Init/GetVideoParam` 回读校验，S7）：constrained baseline；Level 按分辨率/帧率；`TargetUsage` 7（对照 4）；`AsyncDepth=1`；
  `GopRefDist=1`、`NumRefFrame=1`；无周期 IDR（`GopPicSize=65535`、`IdrInterval=0`）、`AdaptiveI/B=OFF`；`LowPower=ON`（VDEnc，失败退 OFF）；
  RC：A 组 CBR，B 组 VBR(target=max)+`LowDelayBRC`+`MaxFrameSize`；VBV 起点 1 帧，对照 2-3 帧/100 ms。
- IDR：PLI/FIR/新观众/resize/切换窗口 → `mfxEncodeCtrl.FrameType = I|IDR|REF`，直到输出中确认 IDR；PLI 风暴合并。
- 码率：`SetRates` → `MFXVideoENCODE_Reset` 改 `TargetKbps`（不强制 IDR），驱动不生效则重建 + IDR。
- **S7 实测（UHD 630，驱动 30.0.100.9805，MSDK runtime API 1.35，`research/s7-onevpl-probe.md`）**：42 组参数全部可用视频内存输入初始化；
  LowPower(VDEnc) 1080p60 CBR 12 Mbps 编码 P50 5.9 / P99 11.7 ms（VME 约 2 倍）。**CBR + LowDelayBRC 不被采纳**（回读为 OFF）；
  VBR + LowDelayBRC + MaxFrameSize 生效（帧上限被遵守）。VBV 1 帧不被采纳，驱动最小约 2 帧（30 fps 101 KB / 60 fps 51 KB）。
  RGB4 直接输入无收益（P99 28.9 ms）→ 保留 VPP BGRA→NV12。起点：LowPower ON、TU7、AsyncDepth 1、CBR 12 Mbps、驱动最小 VBV；
  B 组 VBR+LowDelayBRC+MaxFrameSize=1.5×均值待真实浏览器内容对比。能力判定必须看 GetVideoParam 回读，不能只看 Init>=0。
- **S8（`research/s8-chrome89.md`）**：Chrome 89 把 `playoutDelayHint` 当作下限，设 0 不能把抖动缓冲压到其自身估计以下（回环 35-42 ms，
  156 为 7-9 ms）→ endpoint **必须**发送 RTP playout-delay (0,0)；JS 侧只设 0 或不设。品牌 Chrome 89 的 H.264 1080p 解码仍未验证。
- 回退链：oneVPL → `media::VideoEncodeAccelerator`（MF，树内现成）→ OpenH264（`rtc_use_h264`）。每帧统计上报实际后端与输入路径。

### 3.5 队列纪律
- 任何环节最多 1 个待处理原始帧：编码器忙时新帧覆盖旧帧（原始帧可丢，编码后的参考帧不可丢）。
- `Done()` 不等编码完成（VPP 提交即释放），Viz 池不被网络拥塞反压。
- libwebrtc 的 `VideoStreamEncoder` 内部还有一次线程跳转与 frame dropper：在 S9 中测量其排队，必要时关闭 drop（我们自己做“最新帧”）。

## 4. 帧节奏（pacing）
- 现状（b3）：DComp 根用 `ExternalBeginFrameSourceWin`（`VSyncThreadWin`，失败退化 Sleep）；子 sink 继承根的源；没有现成的“任意间隔”运行期开关。
- **P0（M1）**：不改节奏；capturer `SetMinCapturePeriod` 限导出帧率（30/60）；endpoint 只编码最新帧。代价：平均 ½ vsync 相位等待 + 偶发 1 帧排队。
- **P1（S5，补丁 10）**：浏览器根 external BeginFrame；pacer 两种模式：fixed（本地高精度定时器）与 demand（endpoint 在编码器即将空闲时 `PaceTick`，最低频率兜底，如 10 Hz）。
  每次只允许一个未 ack 请求（重叠会被 viz 判为 bad message）。
- 否决：`--fake-vsync-rate`、`--disable-frame-rate-limit`、在 viz 加 synthetic 间隔策略（理由见 01 D6）。

## 5. 测量方法
- **验收口径（M4，用户变更）**：input→decoded 与 capture→decoded，终点 = viewer 端解码帧可用（rVFC `presentationTime`：接收 + 抖动缓冲 + 解码），
  不含客户端呈现/显示；d25j5 上 P80 < 100 ms、P99 < 200 ms。旧的显示口径（rVFC `expectedDisplayTime`）作为 “glass 估计” 一并报告。
- **同一时钟闭环**：端到端指标都在 viewer `performance.now()` 时钟里计算；分段时间戳（浏览器单调时钟、endpoint QPC、viewer）只用于定位，跨机映射用 data channel 往返（取最小 RTT 样本）。
- 指标：
  1. input-to-photon：viewer 发输入记 t0 → 测试页同帧把标记块改为下一个编码值（8×8 黑白块二进制码，抗压缩）→ viewer 用 `requestVideoFrameCallback` 解码已显示帧，首次出现新值记 t1。
  2. capture-to-display（验收）：测试页每个 rAF 在标记块写页面帧号并记录页面时间；endpoint 经两段时钟同步映射到 viewer 时钟；也可用 abs-capture-time 扩展旁路估计。
  3. 分段：capture begin/end、endpoint 收到、VPP 提交、编码完成、首/末 RTP 包、viewer `getStats`（jitterBufferDelay、decode、framesDropped）、rVFC。
  4. 质量：码率、QP、帧大小分布、IDR 次数、丢包/NACK、帧率、冻结（>200 ms 无新帧）。
- 工具（`tools/`）：标记块测试页（滚动/视频/游戏三模式）、viewer 测量模式（CSV 导出）、网络整形（Windows 上 clumsy/WinDivert，或 Linux/Hyper-V 虚机 `tc netem`：
  对称 RTT 50 ms、抖动 ±10/±20 ms、带宽 10/16 Mbps、丢包 0/0.5/1%，同时作用上下行）。
- 统计：每组 ≥ 2000 帧或 ≥ 2 分钟，报告 P50/P80/P95/P99/max；每组新起 instance（避免 BWE 状态污染）。

## 6. 实验计划
基线 B0 = 1080p30、ARGB 捕获 + VPP、oneVPL CBR 12 Mbps、VBV 1 帧、TU7、LowPower ON、IDR on demand、playout-delay (0,0)、GCC on、RTT 50 ms ±10 ms、Wi-Fi 接收端。

| 场景 | 内容 | 关注点 |
|---|---|---|
| S-scroll | 长文本+图片页，脚本/触摸 fling 滚动；含 omnibox 输入与菜单 | 大面积运动帧大小暴涨、文字清晰度、input-to-photon |
| S-video | 本地 1080p30/60 MP4（需 `proprietary_codecs`），全屏/非全屏 | 持续高码率、帧率匹配、`VideoFrameSubmitter` 与节奏交互 |
| S-game | WebGL/Canvas 实时游戏 + 键盘/触摸 | 高熵画面、尾延迟、渲染与编码 GPU 争用 |

| 编号 | 变量 | 取值 | 判据 |
|---|---|---|---|
| E1 | 帧率 | 30 / 60 | P99 |
| E2 | RC | CBR / VBR+LowDelayBRC+MaxFrameSize | 帧大小尾部、串行化 P99 |
| E3 | VBV | 1 帧 / 3 帧 / 100 ms | 尾延迟 vs 文字画质 |
| E4 | TU / LowPower | TU7/TU4 × LP on/off | 编码耗时、与 Chromium GPU 负载并发 |
| E5 | 码率 | 8/10/12/14 Mbps | GCC 稳态利用率、丢包 |
| E6 | 帧内刷新 | IDR-only / 垂直 intra refresh（1 s） | 丢包恢复、IDR 突发对 P99 |
| E7 | 交接 | 零拷贝 / 保守变体（EVENT 等待或私有拷贝） | capture→encode-start、GPU 占用 |
| E8 | 捕获格式 | ARGB+VPP / NV12 直出 | GPU 时间、池占用 |
| E9 | 节奏 | P0 / P1 fixed / P1 demand | 相位等待、input-to-photon P80 |
| E10 | 接收延迟 | playout-delay (0,0) / (0,20) / 无扩展 | 平滑度 vs 延迟（Chrome 89 与当前 Chrome） |
| E11 | 网络 | RTT 50 ±10 / ±20 / 单向 50；丢包 0/0.5/1% | 尾延迟、恢复 |
| E12 | 自适应 | 固定 1080p / 带宽不足降 720p（libwebrtc degradation） | 低带宽体验 |
| E13 | FEC | off / ULPFEC / FlexFEC（libwebrtc 现成） | 丢包时 P99 |
| E14 | profile | CB / High | 同码率画质 |

顺序：B0 三场景基线（M3 末）→ E7/E8/E9 → E2/E3/E6 → E1/E5/E11 → 其余。结果写 `design/experiments/E*.md`。

## 6a. M4 调优后的默认值（2026-10-27，`research/m4/*.md`）
- 捕获交接：**共享 D3D11 fence**（补丁 19，`--capture-sync=fence` 默认，keyed mutex 为回退）：viz 提交 copy 后立即 Signal 并投递 {token, value, LUID}，
  endpoint `ID3D11DeviceContext4::Wait` 后直接 VPP + 编码；consumed 回执门控 `Done()`/池复用。Viz capture 段 P50 14.8 → 1.3 ms（`fence-impl.md`）。
- 编码：oneVPL VBR + LowDelayBRC，LowPower ON，TU7，AsyncDepth 1，CB profile，1 slice，MaxFrameSize = 1.0 × 最大码率下的平均帧，VBV 请求 1 帧（驱动取整约 34 KB），
  无周期 IDR、intra refresh 关；回读容差（驱动取整不再触发 OpenH264 回退）。
- 码率：**min/start/max = 2000/8000/8000 kbps**（E5：12 Mbps 链路上 max = 链路速率会填满瓶颈队列；8 Mbps 消除 c12 丢包，S capture P99 123 → 88 ms）。
- 帧率 60（E1：30 fps 使 G 变慢约 28 ms）；playout-delay (0,0)；FEC 关（E13 无收益）；pacer 试验 `WebRTC-Video-Pacing/factor:2.5` + 精确 60 Hz fake vsync（补丁 17）。
- 场景覆盖（开关，非默认）：视频为主且链路 ≥ 14 Mbps 时可 `--max-kbps=10000-12000` 换画质（以队列丢包为代价，先测）；高丢包 Wi-Fi 可试 `--vpl-intra-refresh=30`
  （E6 在 c10 上无延迟收益，恢复性未验证）；`--fps=30` 仅用于带宽极低的情况。
- 否决：S5 外部 BeginFrame（fixed/demand）：标记页相位稳定到 1.4 ms，但真实滚动/GPU 恢复后内容子 sink 仍滞后 1-2 帧（相位回到约 33 ms），已回退
  （`research/m4/s5-pacing.md`，diff 留在 `research/m4/s5-rejected/`）；viewer 呈现杠杆（overlay/desynchronized canvas）无收益（`presenter.md`）。

## 7. 主要风险
- R1（已降级）capturer 成本：一次全帧 blit + GPU 完成后投递；不阻塞 GPU 主线程。S4 测量；不可接受时 S12（自定义 OutputDevice）。
- R2 UHD 630 渲染与编码争用：监控 3D 与 VideoEncode 引擎占用；VDEnc 是固定功能单元。
- R3 legacy MSDK runtime 不支持 `LowDelayBRC/MaxFrameSize/IntRefType` 某些组合 → S7 矩阵，A/B 两组 RC。
- R4 Wi-Fi 抖动下 playout-delay 0 卡顿 → E10。
- R5 libwebrtc 在 `build_with_chromium` 配置下的独立可执行集成（socket、线程、field trials）→ S9 第一问；参照 `remoting/`。
