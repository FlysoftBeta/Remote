# 03 视频管线、帧节奏与实验计划

依据：`research/b1-frame-path.md`、`b2-capture.md`、`b3-pacing.md`、`e-video.md`、`e2-vpl-zero-copy.md`、prior art（`a-prior-art.md` §3、§6）。
目标：1080p，10-16 Mbps 可用带宽，RTT 50 ms + 抖动，一端 Wi-Fi；capture→display P80 < 100 ms、P99 < 200 ms；接收端下限 Chrome 89 + H.264。

## 1. 管线总览

```text
[browser process]                          [GPU process / viz]                        [endpoint]
PrtsCaptureSource (per Aura root)          FrameSinkVideoCapturerImpl
  ClientFrameSinkVideoCapturer  ─Start──►    OnFrameDamaged → oracle → CopyOutputRequest
  (ARGB | NV12, kPreferMappableSharedImage)  (blit in same DrawAndSwap, GMB pool, D3D11 keyed mutex key 0)
  OnFrameCaptured(handle, info, rects) ◄──  
  → FRAME{src,gen,token,ts,rects} ─────────── frame pipe ─────────────────────────►  FrameImport
                                                                                     (open-once cache by DXGIHandleToken)
                                                                                     Scene compose (shader, popups)
                                                                                     VPP BGRA→NV12 into encoder surface
  Done() ◄─ FRAME_RELEASE ◄─────────────────── cmd pipe ◄──────────────────────────  release after GPU submit
                                                                                     oneVPL AVC encode (AsyncDepth 1)
                                                                                     RTP (H.264 FU-A, playout-delay 0,0)
                                                                                     GCC/TWCC pacer → UDP → viewer
```

## 2. 浏览器侧捕获（PRTS 模块，`chrome/browser/prts/prts_capture_*`）

- 每个受管 Aura 根（浏览器窗口 + 每个可见弹层宿主）一个 `PrtsCaptureSource`：
  `root->GetHost()->compositor()->context_factory()->GetHostFrameSinkManager()->CreateVideoCapturer()`，
  `ChangeTarget(viz::VideoCaptureTarget(compositor->frame_sink_id()))`，持有 `WindowTreeHost::CreateVideoCaptureLock()`。
- 配置：`SetFormat(PIXEL_FORMAT_ARGB)`（MVP；NV12 为实验 E-NV12）、`SetMinCapturePeriod(1/fps)`、`SetMinSizeChangePeriod(0)`、
  `SetAutoThrottlingEnabled(false)`、`SetAnimationFpsLockIn(false, …)`、固定分辨率约束 = 根像素尺寸、`Start(consumer, kPreferMappableSharedImage)`；
  GPU 不可用时 `kDefault`（只读共享内存）。
- 静态页面：没有损伤就不产帧；endpoint 需要关键帧/新观众时发 `REFRESH` → `RequestRefreshFrame()`（可复活旧帧，无需新 GPU 拷贝）。
- 必带的 viz 修复：`MaybeDeliverFrame` 的失败/版本变化路径补 `CompleteCapture`（prior art 0013，pin 上 bug 仍在）。
- resize：每次尺寸变化 generation+1，丢弃 `metadata.capture.source_size`/`content_rect` 与当前根不符的帧（prior art 0027）。
- 每帧描述符：`source_id, generation, frame_seq, buffer_token(128-bit DXGIHandleToken), handle(仅首次出现该 token 时附带), coded_size,
  visible_rect, content_rect, timestamp_us(浏览器单调时钟，相对 HELLO), capture_begin/end(来自 VideoFrameMetadata)`。

## 3. endpoint 侧 GPU 交接、合成与编码

### 3.1 零额外拷贝交接（默认目标，spike S3 验证）
1. 首次见到某 token：`DuplicateHandle(browser, h)` → `ID3D11Device1::OpenSharedResource1` → 缓存 `{token → texture, keyed mutex}`；
   浏览器侧 capturer 池重建/销毁时发 `BUFFER_GONE{token}`，endpoint 释放缓存。
2. 每帧：`IDXGIKeyedMutex::AcquireSync(0, timeout=2ms)`（必须严格等于 `S_OK`；`WAIT_TIMEOUT/WAIT_ABANDONED` 非负但都是失败——prior art 教训）。
3. 合成：无可见弹层时跳过（常见情况），直接把共享源纹理作为 VPP 输入。有弹层时：弹层帧在到达时各自拷成 endpoint 私有的小纹理缓存
   （弹层更新稀少、面积小，且其 capture 租约可立即释放）；基础层**不拷贝**，一个像素着色器 pass 读共享基础纹理 + 弹层缓存
   （预乘 alpha、阴影/圆角、虚拟 z-order）写入 endpoint 的 BGRA 场景纹理，再作为 VPP 输入（e2 §2(a)、prior art `Pipeline::compose`）。
4. `ID3D11VideoContext::VideoProcessorBlt` BGRA→NV12（BT.709 limited，与 SPS VUI 一致；prior art 用的是 BT.601，需统一）写入编码器输入面：
   我们自己分配的可复用 NV12 纹理池（`D3D11_BIND_RENDER_TARGET`，单纹理，**不是** allocator 的 DECODER-only 内部面；prior art 已证明 RT|SRV NV12 可作外部编码输入）。
   1080→1088 对齐用 VP 的 source/dest/target rect + 背景色解决，取消 prior art 的 9 次填充拷贝（e2 §2）。视频处理器/视图在 setup/resize 时创建并缓存（prior art 每帧新建）。
5. `ReleaseSync(0)` → 立刻回 `FRAME_RELEASE`。不做 `D3D11_QUERY_EVENT` 忙等：keyed mutex 在 GPU 侧串行化 Viz 的下一次写
   （Chromium 自己的 `DXGISharedHandleState::ReleaseKeyedMutex` 也不做 EVENT 等待）。spike S3 同时实现“Blt 后等一次 EVENT 再释放”的保守变体做对照压力测试。
6. `MFXVideoENCODE_EncodeFrameAsync` → `MFXVideoCORE_SyncOperation`（在编码线程阻塞，不在导入线程；**不得**在持有 `ID3D11Multithread::Enter` 时调用，
   MSDK 工作线程可能需要该锁）。同一设备/上下文上 VP 写在前、编码读在后，编码完成即隐含 VP 完成，无需额外的主机等待（需在 Intel 驱动上验证）。
- 备选 E-RGB4：AVC 编码器直接吃 RGB4（Intel 确认 Skylake+ 的 legacy MSDK 支持内部 CSC，需 `Query` 实测）；它只是把 CSC 搬进编码器，
  且共享源要一直锁到编码完成，默认不用。
- 保底：prior art 路径（浏览器拷贝到 key0/1 导出池 + endpoint 私有拷贝 + 查询轮询）。能力位 `zero_copy_handoff` 协商。

### 3.2 编码器：oneVPL → legacy Media SDK runtime（UHD 630 / Gen9.5）
- dispatcher（静态链接 libvpl 2.x）`MFXLoad` + `MFXCreateConfig` 过滤：硬件实现、AVC 编码、`MFX_ACCEL_MODE_VIA_D3D11`；
  **不要**把最低 API 设为 2.x（会过滤掉 Gen9 的 1.35 实现）。记录实现索引、API 版本、runtime DLL 路径/版本、驱动版本、LUID。
- `SetHandle(MFX_HANDLE_D3D11_DEVICE, endpoint 的 Intel D3D11 设备)`（设备需 `VIDEO_SUPPORT`、multithread protected、LUID 与浏览器 GPU 一致）。
- 外部帧分配器：编码器输入面由我们分配（NV12，`D3D11_BIND_RENDER_TARGET`，单纹理非数组，供 VPP 写入），`MFX_IOPATTERN_IN_VIDEO_MEMORY`。
- 起始参数（全部需 `Query/Init/GetVideoParam` 回读校验，见 `e-video.md` 表）：
  constrained baseline；Level 由协商决定（1080p30 → 4.0，1080p60 → 4.2）；`TargetUsage` 7（对比 4）；`AsyncDepth=1`；`GopRefDist=1`、
  `NumRefFrame=1`；`GopPicSize=65535`、`IdrInterval=0`、`AdaptiveI/B=OFF`；`LowPower=ON`（VDEnc，失败退 OFF）；
  RC：A 组 CBR，B 组 VBR(target=max)+`LowDelayBRC`+`MaxFrameSize`；VBV 起点 1 帧（12 Mbps@60fps ≈ 25 KB），回退 2-3 帧/100 ms。
- IDR：PLI/FIR/新观众/resize → `mfxEncodeCtrl.FrameType = I|IDR|REF`，直到真的在输出中看到 IDR 为止；PLI 风暴合并。
- 码率变更：先尝试 `MFXVideoENCODE_Reset` 改 `TargetKbps`（不强制 IDR）；若驱动不生效（prior art 在 MF 上遇到过），重建会话 + IDR。
- 回退链：oneVPL → Intel MFT（prior art，CLSID `CoCreateInstance` 规避泄漏）→ OpenH264（CPU）。每帧统计中上报实际后端与输入路径。

### 3.3 RTP / WebRTC（webrtc-rs 0.21.1 + rtc 0.21.1）
- 编解码协商：offer `profile-level-id=42e01f;packetization-mode=1;level-asymmetry-allowed=1`。Chrome 的 answer 在双方都允许不对称时
  总是填它自己的接收 level（实际恒为 3.1），且接收端不按 level 拒绝码流（`research/g-h264-level.md`；Chrome 自己的发送端在 1080p 也写高于 3.1 的 SPS level）。
  因此：把协商出的 3.1 视为名义值，SPS 写真实 level（1080p30 → 4.0，1080p60 → 4.2）；**不**按 `e-video.md` 的建议降到 720p。
  spike S8 在 Chrome 89 与当前 Chrome、硬解与 `--disable-accelerated-video-decode` 软解上实测 1080p。
- `configure_congestion_control(Gcc, Twcc)` 打开发送端 BWE + pacer；pacer 不能用默认 4096 包/1 Mbps 初值：
  按 **字节 + 排队时长** 限界（如 ≤ 1.5 帧时长），溢出在帧边界丢弃并请求本端 IDR（编码后丢参考帧必须重同步）。
- 码率控制环：`target = min(GCC 估计 × 0.9, 实例上限, REMB)`，每 100 ms 或估计变化 > 10% 下发给编码器。
- playout-delay 扩展 `http://www.webrtc.org/experiments/rtp-hdrext/playout-delay`，值 `(0,0)`，每个视频包都带（M89 `VCMTiming::RenderTimeMsInternal`
  在 min=max=0 时返回 0，`e-video.md` 已验证）。viewer 侧：有 `jitterBufferTarget` 设 0，否则 `playoutDelayHint=0`，二者不同时设非零值。
- 可选 abs-capture-time 扩展（`HeaderExtension::Custom`），用于测量。
- NACK 开启；FEC 默认关（实验 E-FEC）；无音频。

### 3.4 “最新帧优先”的队列纪律
- 任何队列最多 1 个待处理原始帧：编码器忙时新捕获覆盖旧的（原始帧可丢，编码后的参考帧不可丢）。
- 捕获释放（`Done()`）不依赖编码完成：VPP 提交后立即释放，Viz 池不会被网络拥塞反压。

## 4. 帧节奏（pacing）

### 4.1 现状（已验证，b3）
- Windows 根 Display 的 BeginFrameSource：有 DComp 支持时是 `ExternalBeginFrameSourceWin`（订阅单例 `gl::VSyncThreadWin`，
  `WaitForVBlank` 等主显示器；失败或无显示器时退化为 `Sleep(interval)`）；无 DComp 时 `DelayBasedBeginFrameSource`；
  `--disable-frame-rate-limit` → BackToBack；设置了 external controller → `ExternalBeginFrameSourceMojo`。
- cloak 不改变时钟；renderer/OOPIF 的 sink 通过 `FrameSinkManagerImpl::RecursivelyAttachBeginFrameSource` 继承根的源，
  所以控制根的 BeginFrame 就控制了 rAF/合成节奏；`VideoFrameSubmitter` 可能手动 ack 提交（不完全受控）。
- 运行期“任意间隔”的现成开关不存在：`SetDisplayVSyncParameters` 对 Win DComp 根无效，`SetPreferredInterval` 只能半频。

### 4.2 方案
- **P0（MVP）**：不改节奏，Viz 按显示器 vsync（开发机 60 Hz）产帧；capturer `SetMinCapturePeriod` 限制导出帧率（30 或 60）；
  endpoint 只编码最新帧。代价：平均 ~½ vsync 的相位等待 + 偶发 1 帧排队。
- **P1（spike S5，小补丁）**：PRTS 模式下 `DesktopWindowTreeHostWin::Init` 以 `CreateCompositor(force_sw, /*use_external_begin_frame_control=*/true)`
  创建 compositor（~5 行，Views 层只读一个 `ui` 级开关，不依赖 chrome/）；`set_wait_for_all_frame_sinks(false)`（同 `DesktopWindowTreeHostPlatform`）。
  新增 per-root `PrtsFramePacer`（chrome/browser/prts，~150 行）：
  - fixed 模式：本地高精度定时器按 1/fps 调 `ui::Compositor::IssueExternalBeginFrame(args, ack_cb)`，一次只允许一个未 ack 请求（重叠会被 viz 判为 bad message）；
  - demand 模式：endpoint 在编码器即将空闲时发 `PACE_TICK{window}`，并有一个最低频率兜底（如 10 Hz）保证动画/视频不冻结；
  - 每个弹层根各自一个 pacer；args 用单调 frame_time、递增 sequence、`deadline = now + interval*0.8`。
- 否决：`--fake-vsync-rate`（GPU 进程全局、启动期固定、ms 取整）、`--disable-frame-rate-limit`（无上限空转，耗 GPU）、
  在 viz 里加锁定的 synthetic 间隔策略（改动大）。

## 5. 测量方法（glass-to-glass 与分段）

### 5.1 原则
- **同一时钟测端到端**：所有端到端指标都在 viewer 的 `performance.now()` 时钟里闭环，避免跨机时钟同步误差。
- 分段时间戳（浏览器单调时钟、endpoint QPC、viewer 时钟）只用于定位瓶颈；跨机映射用 data channel 上的 NTP 式往返（取最小 RTT 样本）。

### 5.2 指标
1. **input-to-photon（主观体验指标）**：viewer 发送输入（如 pointerdown）记 t0 → 测试页面在收到事件的同一帧把一个 64×64 标记块改为下一个编码值
   （颜色二进制码，抗压缩：每位 8×8 黑/白块）→ viewer 用 `requestVideoFrameCallback` 拿到每个已显示帧的 `expectedDisplayTime`，
   把该帧画到 canvas 读回标记块，首次出现新值记 t1；`t1 - t0`。完全同一时钟，无需同步。
2. **capture-to-display（验收指标，对应 P80/P99）**：测试页每个 rAF 在标记块写入“页面帧号”，同时通过一个 1×1 的 `fetch`/console 绑定把
   `(帧号, performance.timeOrigin+now)` 记录到浏览器侧日志；endpoint 把浏览器单调时钟映射到 viewer 时钟（经两段时钟同步），
   viewer 解出帧号后计算 `displayTime - pageFrameTime`。也可在非测试页面用 abs-capture-time 扩展做旁路估计。
3. **分段**：Viz capture 开始/结束（`VideoFrameMetadata.capture_begin_time/end_time`）、FRAME 描述符到达 endpoint、VPP 提交、编码完成、
   第一个/最后一个 RTP 包发送、viewer `getStats`（jitterBufferDelay、decode、framesDropped）、rVFC `presentationTime/expectedDisplayTime`。
4. 质量：每秒码率、QP、帧大小分布、IDR 次数、丢包/NACK、帧率、冻结（>200 ms 无新帧）次数、可选 VMAF/PSNR（离线，用 endpoint 旁路保存的 YUV）。

### 5.3 工具（`tools/`）
- `tools/latency-page/`：标记块测试页（滚动/视频/游戏三种模式都叠加标记块，标记块固定在视口角落）。
- viewer 内置“测量模式”：解码标记块、发 input 探针、导出 CSV（每帧一行）。
- `tools/netem/`：网络整形。Windows 上用 **clumsy**（WinDivert）或在 Linux 路由/Hyper-V 虚机上用 `tc netem` 做对称 RTT 50 ms、
  抖动 ±10/±20 ms（正态/Pareto）、带宽 10/16 Mbps、丢包 0/0.5/1%；同时作用于上行（输入）与下行。prior art 的 SRTP 单向整形器只作补充。
- 统计：每组 ≥ 2000 帧或 ≥ 2 分钟，报告 P50/P80/P95/P99/max；每组新起 instance，避免 BWE 状态污染（prior art 教训）。

## 6. 实验计划

每个实验只改一个变量；基线配置 B0 = 1080p30、ARGB 捕获 + VPP、oneVPL CBR 12 Mbps、VBV 1 帧、TU7、LowPower ON、IDR on demand、
playout-delay (0,0)、GCC on、RTT 50 ms ±10 ms、Wi-Fi 接收端。

| 场景 | 内容 | 关注点 |
|---|---|---|
| S-scroll | 长文本+图片页面，脚本驱动/真人触摸 fling 滚动；含 omnibox 输入与弹层 | 大面积运动导致帧大小暴涨、文字清晰度、input-to-photon |
| S-video | 1080p30/60 YouTube 类视频（本地 MP4，避免网络）+ 全屏/非全屏 | 持续高码率、帧率匹配（30 vs 60）、VideoFrameSubmitter 与节奏的交互 |
| S-game | WebGL/Canvas 实时游戏（如 three.js 场景或本地 HTML5 游戏）+ 键盘/触摸操作 | 高熵画面、input-to-photon 尾延迟、GPU 争用（Chromium 渲染与编码共用 UHD 630） |

| 编号 | 变量 | 取值 | 预期/判据 |
|---|---|---|---|
| E1 | 帧率 | 30 / 60 | 60 fps 减少采样等待但增加编码/网络负载；看 P99 |
| E2 | RC 模式 | CBR / VBR+LowDelayBRC+MaxFrameSize / VCM | 帧大小分布尾部、P99 串行化延迟 |
| E3 | VBV | 1 帧 / 3 帧 / 100 ms | 尾延迟 vs 画质（文字） |
| E4 | TargetUsage / LowPower | TU7/TU4 × LP on/off | 编码耗时 vs 画质；与 Chromium GPU 负载并发时的耗时 |
| E5 | 码率 | 8 / 10 / 12 / 14 Mbps（总带宽 10-16） | GCC 稳态利用率、丢包 |
| E6 | 帧内刷新 | IDR-only / 垂直 intra refresh（周期 1 s） | 丢包恢复时间、IDR 突发对 P99 的影响 |
| E7 | 交接路径 | 零额外拷贝 / prior art 双拷贝 | capture→encode-start 耗时、GPU 占用 |
| E8 | 捕获格式 | ARGB+VPP / NV12 直出 | GPU 时间、`CopyOutputNV12` 等待对浏览器的影响 |
| E9 | 节奏 | P0 vsync / P1 fixed external BF / P1 demand | capture 相位等待、input-to-photon P80 |
| E10 | 接收延迟 | playout-delay (0,0) / (0,20) / 无扩展 | 平滑度（冻结/抖动） vs 延迟；Chrome 89 与当前 Chrome 各测一次 |
| E11 | 网络 | RTT 50 ±10 / ±20 / 单向 50；丢包 0/0.5/1% | 尾延迟、恢复 |
| E12 | 分辨率自适应 | 固定 1080p / 带宽不足时降 720p | 低带宽下体验 |
| E13 | FEC | off / FlexFEC（若 webrtc-rs 可用） | 丢包时 P99 |
| E14 | profile | CB / High（若对端 SDP 提供） | 同码率画质 |

执行顺序：先 B0 在三场景各跑一次建立基线（M3 末）；再 E7/E8/E9（管线结构）→ E2/E3/E6（码控与尾延迟）→ E1/E5/E11（网络）→ 其余。
结果记录到 `design/experiments/E*.md`（配置、CSV 路径、P50/P80/P99、结论），只保留对决策有用的数字。

## 7. 主要风险
- R1 `CopyOutputRequest` 在 GPU 线程上等待 blit 完成（b2 §1：mappable blit 会 wait），在高负载时把浏览器合成拖慢 → E7/E8 测量；必要时改走非 mappable 共享图像或自定义 OutputDevice（b1 §5 的 10-16 文件方案）。
- R2 UHD 630 同时承担 Chromium 光栅/合成与编码：游戏场景 GPU 饱和导致编码排队 → 监控 GPU 引擎占用（3D vs VideoEncode 引擎分离，VDEnc 在独立固定功能单元上）。
- R3 legacy MSDK runtime 对 `LowDelayBRC/MaxFrameSize/IntRefType` 组合可能不支持 → Query 回读，A/B 两组 RC 都要有。
- R4 Wi-Fi 抖动下 playout-delay 0 导致卡顿 → E10。
- R5 webrtc-rs GCC/pacer 成熟度 → 若 E5/E11 不达标，换 str0m 或引入原生 libwebrtc（只做传输）。
