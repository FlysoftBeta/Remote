# 01 总体架构

版本：Stage 1 设计，2026-10-25。输入：`00-brief.md`、`research/*.md`。所有 Chromium 路径相对 `src/`，
基线为 Stable **157.0.8103.15**，commit **`1aa07b638799e59656559ae6c6649d4b197f555e`**（下称“pin”）。
证据来源：`research/a-prior-art.md`（用户早期项目 PRTS，M158）、`b1-frame-path.md`、`b2-capture.md`、`b3-pacing.md`、
`c1-input-layer.md`、`c2-ime-osk.md`、`d-branding.md`、`e-video.md`、`e2-vpl-zero-copy.md`。

## 1. 一句话

**打过小补丁的 Chrome `--headless=new`（产品名 PRTS）把每个 Aura 根窗口（浏览器窗口 + 每个弹出层宿主）通过 Viz
自带的 `FrameSinkVideoCapturer` 导出为 D3D11 共享纹理；Rust 写的 browser-endpoint 在同一块 Intel GPU 上合成、
转 NV12、用 oneVPL（Gen9.5 上即 legacy Media SDK runtime）编 H.264，经 WebRTC 直接发给用户；用户的输入以
`ui::Event` 形式注入到对应 `aura::WindowTreeHost` 的 event sink；control-plane（Node 示例）只做实例生命周期和信令。**

## 2. 组件与进程

```text
                           (per tenant / instance)
 ┌──────────────────────────────── PRTS browser process tree ───────────────────────────────┐
 │ chrome.exe --headless=new --prts-pipe=<h_cmd>,<h_evt>,<h_frm> --user-data-dir=<inst>     │
 │   browser process                                                                        │
 │     chrome/browser/prts/*  (PRTS 模块：控制器、窗口/弹层注册表、捕获、输入、IME 观察)       │
 │     Views/Aura + HWNDMessageHandlerHeadless (cloaked HWND)                               │
 │   GPU process (viz display compositor, D3D11 on Intel UHD 630)                           │
 │   renderers / utility / network                                                          │
 └───────▲ cmd pipe (endpoint→browser)   │ evt pipe (browser→endpoint)   │ frame pipe ───────┘
         │                               ▼                               ▼  (descriptors only;
 ┌───────┴───────────────── browser-endpoint (Rust, one process per host) ────────────────┐  pixels
 │ instance manager (Job Object launcher) · browser link · frame import (D3D11 keyed mutex) │  stay on
 │ scene compositor (D3D11 shader) · BGRA→NV12 (VideoProcessor) · encoder (oneVPL / MF /    │  GPU as
 │ OpenH264) · pacing controller · WebRTC sessions (H.264 track + data channels) · metrics   │  NT
 └───────▲ framed JSON stdin/stdout                         │ WebRTC (UDP, direct)          │  handles)
         │                                                  ▼
 ┌───────┴──────── control-plane (Node.js sample) ──┐   ┌──────── viewer (browser, Chrome 89+) ───────┐
 │ HTTP static + WebSocket signalling, instance API │◄──┤ <video> + input capture + soft keyboard     │
 └──────────────────────────────────────────────────┘   └─────────────────────────────────────────────┘
```

- **一个 instance = 一个浏览器进程树 + 一个 profile 目录**；同 instance 的多个窗口/标签不是不同租户。
- endpoint 是每台主机一个进程，管理多个 instance（沿用 prior art：`prtsd`，Tokio，Job Object 原子加入）。
- control-plane 不在视频/输入快路径上：WebRTC 建立后，媒体和输入走 endpoint ↔ viewer 直连。

## 3. 关键决策（每条附否决方案）

### D1 Chromium 基线：pin 到 Windows Stable 157.0.8103.15
- 理由：Web 身份要像真 Chrome，Stable 版本号/GREASE 与官方 Chrome 一致；stable 分支补丁更少漂移；prior art 用的 M158 main 不可复现官方 UA。
- 否决：跟 main（prior art 的 `cr pin latest`）——每次同步都可能破坏补丁且版本号不对应任何发布的 Chrome。
- 升级策略：每个新 Stable 里程碑可选 rebase（见 02 §4），不追 patch 版本。

### D2 浏览器运行形态：Chrome `--headless=new` + Aura/Views + 保留 cloaked HWND
- 依据（已验证，pin）：`ui/views/win/hwnd_message_handler_headless.cc` `HWNDMessageHandlerHeadless::Init` 创建真实 HWND 并 `DWMWA_CLOAK`；
  `Activate/Deactivate/IsActive` 是逻辑状态，不调用 `SetForegroundWindow`；`SetCapture` 是 no-op；
  `ui/aura/window_tree_host.cc` 构造函数在 `--headless` 下关闭 native occlusion 跟踪（`native_window_occlusion_enabled_`），
  所以 cloaked 窗口不会因“被遮挡”停止合成；`DCompPresenter::Present` 无 cloak 检查（b1 §3）。
- 所有 PRTS 行为只在 `--prts-pipe` 存在时启用（运行期开关），无该开关时补丁后的 chrome.exe 行为等同上游 headless。
- 否决：Ozone headless（Windows 不支持）、headless_shell（没有浏览器 UI）、自建 Win32 隐藏桌面 + 桌面捕获（多租户互相干扰，违反“非侵入”）。

### D3 浏览器 ↔ endpoint IPC：三条继承的匿名管道 + 带 schema 的二进制协议
- `cmd`（endpoint→browser：RPC、输入、帧释放）、`evt`（browser→endpoint：应答、生命周期、窗口/弹层/IME 事件）、
  `frm`（browser→endpoint：帧描述符）。每条管道独立 writer 线程，避免 prior art 中帧描述符与事件共用 writer 队列造成的队头阻塞。
- 像素不过管道：GPU 帧 = D3D11 NT handle（endpoint 用浏览器进程句柄 `DuplicateHandle` 拉取），CPU 帧 = 只读 section。
- 消息定义集中在一个 schema 文件（`protocol/prts.proto` 风格的手写 IDL + 生成 C++/Rust 编解码；第一版可手写，但必须是
  “命名消息 + 版本号”，不再用 prior art 的魔数 + v1/v2 并存）。管道句柄通过 `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` 精确继承。
- 否决：Mojo（endpoint 不是 Chromium 进程）；命名管道（名字抢占/ACL 更复杂）；DevTools/CDP 作为主通道（不能覆盖浏览器 UI、延迟高、
  是另一种自动化指纹）。CDP 仍可在调试构建里用 `--remote-debugging-pipe` 辅助测试，但不进生产启动参数。

### D4 帧导出：每个 Aura 根做 Viz 视频捕获（`FrameSinkVideoCapturer`），不替换 OutputDevice
- 依据（已验证，b1 §4 / b2）：`HostFrameSinkManager::CreateVideoCapturer()` + `VideoCaptureTarget(compositor->frame_sink_id())` 捕获
  根 render pass（含 tab strip、omnibox、网页内容、OOPIF）。捕获请求在**发现该损伤的同一次 `Display::DrawAndSwap`** 中执行
  （`SurfaceAggregator::PrewalkSurface` 先 `NotifyAggregatedDamage` 再 `TakeCopyOutputRequestsFromClient`），没有固定的额外一帧延迟。
  `kPreferMappableSharedImage` 在 Windows 上走 `D3DImageBackingFactory::CreateGpuMemoryBufferHandle`：
  D3D11 DEFAULT 纹理，`SHARED_NTHANDLE | SHARED_KEYEDMUTEX`，key 固定为 0；支持 **ARGB(BGRA8) 和 NV12** 两种 GPU 输出
  （NV12 由 `SkiaOutputSurfaceImplOnGpu::CopyOutputNV12` 在 GPU 上做 Rec.709 limited 转换）。CEF 当前的 shared-texture OSR 用的是同一原语。
- 软件回退：同一 capturer 用 `kDefault` 得到只读共享内存 ARGB（prior art 已验证）。headless 下软件合成本来就是内存 `SoftwareOutputDevice`（b1 §2）。
- 弹出层（菜单、select 下拉、omnibox 下拉、气泡、tooltip）是**独立的 DesktopWindowTreeHostWin 根**，每个根一个 capturer，
  endpoint 按虚拟 z-order 合成（prior art 已验证四类弹层）。
- 否决：
  - 自定义 `SkiaOutputDevice`/`SoftwareOutputDevice` 直接渲染到共享纹理：估计 10-16 个 viz 文件 + Mojo 接口改动（b1 §5），
    GPU/软件两套实现、要处理 DComp overlay 能力、resize/context loss，维护成本高；只有当捕获拷贝被测量证明是瓶颈时才作为 spike。
  - 拦截 DComp / swap chain Present：DComp 路径下根本没有统一可读的根 swap chain（`SkiaOutputDeviceDComp` 由 renderer 分配
    DComp surface，write-only，b1 §1），不可行。
  - DWM/桌面复制捕获：cloaked 窗口不可见、多租户不可分。
- 已知上游 bug：`FrameSinkVideoCapturerImpl::MaybeDeliverFrame` 失败/版本变化路径不调用 `VideoCaptureOracle::CompleteCapture`，
  静态页面会永久卡住（b2 §3，pin 上仍存在）→ 保留 prior art 0013 的 viz 修复补丁（独立、可上游化）。

### D5 GPU 帧交接：首选“零额外拷贝”，保底沿用 prior art 双拷贝
- 目标路径：浏览器把 Viz 池纹理的 NT handle 直接发给 endpoint（不再在浏览器里拷到自有导出池）；endpoint 每个池缓冲区只
  `OpenSharedResource1` 一次并缓存，按 key 0 `AcquireSync` → `VideoProcessorBlt`（BGRA→NV12，正好写进编码器输入面）→ `ReleaseSync(0)`
  → 发 `FRAME_RELEASE`，浏览器调用 `FrameSinkVideoConsumerFrameCallbacks::Done()`。全程无 CPU 轮询 GPU 完成：跨设备顺序由 keyed mutex 保证。
- 保底路径：prior art 的“浏览器拷到 key0/1 导出池 + endpoint 私有拷贝”（已在 UHD 630 上跑通）。两条路径用能力位协商，spike S3 决定默认值。
- NV12 直出（capturer `PIXEL_FORMAT_NV12`）作为实验分支：省掉 endpoint 的 VPP 转换，但弹层合成要在 NV12 上做（需 shader），
  而且 `CopyOutputNV12` 在 GPU 线程上等待完成，需测量。

### D6 帧节奏：Viz 正常产帧 + 捕获节流为主；外部 BeginFrame 为第二阶段
- 见 03 §4 与 `research/b3-pacing.md`。第一阶段：compositor 照常跟 vsync（cloaked 窗口下 VSyncThreadWin 仍工作，失败时退化为定时 Sleep），
  capturer `SetMinCapturePeriod(1/fps)`、关闭 auto-throttling 与 animation lock-in；endpoint 用“最新帧覆盖”消化编码/网络抖动。
- 第二阶段（spike S5）：给浏览器根 compositor 打开 external BeginFrame（`ui::Compositor` 的 external begin frame 控制，同 CEF OSR 做法），
  由 endpoint 发 “BeginFrame now”，把产帧与编码器空闲对齐，消除 vsync 相位造成的平均半帧等待。

### D7 输入：在 `aura::WindowTreeHost::SendEventToSink` 层注入 `ui::Event`
- 依据（已验证，c1）：sink → `WindowEventDispatcher` → Views（`DesktopNativeWidgetAura`/`RootView`/`FocusManager`）与
  content（`RenderWidgetHostViewAura` → `input::RenderWidgetHostInputEventRouter`，Viz hit-test，OOPIF 正确）同时可达；
  键盘按 `FocusClient` 和 `GetFocusedRenderWidgetHost` 路由。touch 走 `GestureRecognizer`，在 content 里是真 `WebTouchEvent`（不是鼠标模拟）。
- 需要补的（c1 §2）：坐标用根 DIP 再乘一次宿主 root transform；`InputStateLookupWin` 读 `::GetKeyState` → 必须虚拟化；
  headless `HasCapture()==false` 使 Views 拖拽失效 → 逻辑 capture 查询；`ScreenWinHeadless` 的虚拟光标点要同步；
  离开根/切换弹层要手工发 exit；`::SetCursor/::SetCursorPos/::ClipCursor/::ShowCursor`、系统键盘钩子、`::DoDragDrop`、
  touch-drag 的 `ui::SendMouseEvent` 都要在 PRTS 模式下屏蔽或改为上报。
- 否决：DevTools Input 域与 `RenderWidgetHost::Forward*`（只能到网页，到不了 tab strip/omnibox/Views 菜单）；
  `SendInput`（全桌面共享前台/焦点，多租户互相抢，且 cloaked 窗口无法成为目标）。

### D8 焦点与激活：每个 instance 自己的逻辑激活，endpoint 的“控制权”与浏览器激活一一对应
- 激活/焦点状态都在各自进程内（`wm::FocusController` per root，`HWNDMessageHandlerHeadless::is_active_`），跨进程不竞争（c1 §3）。
- PRTS 控制器维护“当前活动根”：viewer 获得控制权 → `Widget::Activate()`（逻辑）；失去 → 释放所有按住的键/键钮、取消触点、`Deactivate`。
  弹层根的键盘目标：全局逻辑 capture 优先 → 活动的可接收输入弹层 → 浏览器根（prior art `KeyRoot()` 规则）。
- 进程内多个根共享 `DesktopCaptureClient` 静态状态、cursor manager、Env 输入状态；这对单租户是正确的。

### D9 可编辑焦点检测与远程软键盘
- 见 `research/c2-ime-osk.md` 与 02 §2.6。已验证：`--headless` 下 `ui::CreateInputMethod` 返回 `ui::MockInputMethod`
  （`ui/base/ime/init/input_method_factory.cc:52`），它的 VK 控制器是内存 stub，所以真实 TabTip/InputPane 本来就不会弹出；
  但它在焦点变为空时不通知观察者，也拿不到 enterkeyhint/vk_policy。
- 方案：remote 模式下工厂返回 `ui::RemoteInputMethod`（同 Mock 语义 + 空焦点/detach 通知 + 转发 show/hide 的 `RemoteVirtualKeyboardController`）；
  PRTS 对每个根注册 `ui::InputMethodObserver`，读取当前 `TextInputClient` 的 type/mode/flags/action/vk_policy/caret，以 `IME_STATE` 发给 endpoint → viewer。
  “何时弹键盘”直接复用 content 的 `VirtualKeyboardControllerWin` 策略（触摸/笔聚焦 + auto policy + show_ime_if_needed，或 manual policy）
  和 Views `Textfield::RequestFocusForGesture`，我们只换出口。
- 文本回填：viewer 软键盘/IME 的已提交文本 → `TextInputClient::InsertText`；组合文本（可选）→ `SetCompositionText`。

### D10 触摸能力暴露
- 注入的 touch 能被已注册监听器收到，但 `navigator.maxTouchPoints` 来自 `ui::MaxTouchPoints()`（无触摸屏时为 0，c1 §4）。
  instance 创建参数 `touch_points`（0 或 1-10），PRTS 模式下由小补丁让 `ui/base/pointer/pointer_device_win.cc` 的
  `MaxTouchPoints/GetTouchScreensAvailability/GetAvailablePointerAndHoverTypes` 返回虚拟能力。默认：viewer 声明触摸设备时开启。

### D11 品牌：产品 PRTS，Web 身份 Google Chrome
- 仅改 `components/embedder_support/user_agent_utils.cc` 两处（Windows 下 brand=“Google Chrome”，不加 `Headless` 前缀），
  其余 UA reduction/GREASE/client hints/JS `userAgentData` 全部沿用上游流水线；`chrome/app/theme/chromium/BRANDING` + 选定 GRIT 字符串改 PRTS。
  详见 02 §2.7 与 `d-branding.md`。否决：`--user-agent`（UA-CH 元数据会走 early-return，`d-branding.md` 已验证）、`is_chrome_branded=true`、
  在 JS/HTTP 两端分别伪造。

### D12 endpoint 媒体栈：Rust（沿用 prtsd）+ webrtc-rs 0.21 显式 GCC/TWCC/pacer + oneVPL
- 编码：oneVPL dispatcher 加载 legacy `libmfxhw64.dll`（UHD 630 = Gen9.5，新 GPU runtime 不支持），D3D11 视频内存输入，
  AsyncDepth=1、IPPP、无 B 帧、CBR/低延迟 VBR、按需 IDR；MF（Intel MFT）与 OpenH264 作为回退（均已在 prior art 跑通）。
- 传输：H.264 constrained baseline，offer `42e01f`；Chrome answer 恒为 3.1 且接收端不强制 level，SPS 写真实 level（1080p30 → 4.0，1080p60 → 4.2，见 `research/g-h264-level.md`），发送端 playout-delay 扩展 `(0,0)`，
  sender GCC/TWCC + 有界 pacer，NACK；不使用音频。
- 否决（暂缓）：原生 libwebrtc（构建/维护最重；作为传输行为参照，若 Rust 栈在 Wi-Fi 抖动下达不到 P99 再引入）；str0m（备选）。

### D13 control-plane 与 viewer
- 复用 prior art `server/`（Node ≥22，`ws`）与 viewer JS，改为新协议。viewer 代码必须在 Chrome 89 可运行（不用 `jitterBufferTarget`
  以外的新 API 时做特性检测）。

## 4. 端到端数据流（一帧）

1. 网页/浏览器 UI 变化 → renderer/browser 提交 CompositorFrame → Viz 在下一个 BeginFrame deadline 聚合。
2. 聚合发现根 sink 有损伤 → capturer `OnFrameDamaged` → oracle 采样通过 → `CopyOutputRequest`（blit 到 GMB 池纹理）随同一次 DrawAndSwap 执行。
3. 结果回到 capturer → `OnFrameCaptured(VideoBufferHandle{DXGI NT handle}, info{timestamp, coded size, visible rect}, content_rect, callbacks)`
   → PRTS 捕获器在 browser 进程收到，写 `FRAME` 描述符（source id、generation、buffer id、timestamp_us、rects）到 frame 管道。
4. endpoint：按 buffer id 取缓存的已打开纹理 → keyed mutex(0) → 合成/VPP 到编码器 NV12 面 → release → `FRAME_RELEASE`。
5. oneVPL `EncodeFrameAsync` + `SyncOperation` → Annex-B AU → RTP 打包（带 playout-delay、abs-capture-time 可选）→ pacer → UDP。
6. viewer 解码 → `<video>` 显示；`requestVideoFrameCallback` 回报显示时间用于测量。

输入方向：viewer PointerEvent/KeyboardEvent → 可靠有序 data channel（`prts.control.v1`）→ endpoint 校验/映射 → cmd 管道 `INPUT_*` →
browser UI 线程构造 `ui::Event` → 目标根的 `SendEventToSink`。

## 5. 延迟预算（目标：capture→display P80 < 100 ms，P99 < 200 ms）

brief 的“50 ms latency”按 **RTT 50 ms** 解读（单向约 25 ms）；实验同时覆盖“单向 50 ms”这一更严苛的解读（03 §6）。

| 段 | 典型 | P99 预算 | 说明 |
|---|---:|---:|---|
| 帧产生到捕获完成（vsync 相位 + 合成 + GPU blit） | 10 ms | 25 ms | external BeginFrame 后可降到 ~5 ms |
| 交接 + 合成 + BGRA→NV12 | 2 ms | 5 ms | 无 CPU 轮询 |
| H.264 编码（1080p, UHD 630） | 8 ms | 15 ms | prior art MF 720p 实测 9.3 ms 均值 |
| pacer + 串行化（12 Mbps, P 帧 ~20-30 KB） | 10 ms | 40 ms | IDR/场景切换是 P99 的主要来源 → VBV 与帧大小上限 |
| 网络单向 + Wi-Fi 抖动 | 25 ms | 60 ms | |
| 接收端组帧/抖动缓冲（playout-delay 0） | 3 ms | 20 ms | |
| 解码 + 显示（vsync） | 15 ms | 30 ms | |
| **合计** | **~73 ms** | **~195 ms** | 输入→画面（input-to-photon）另加上行 ~25 ms，单独报告 |

## 6. 代码仓布局（remotekit）

```text
remotekit/
  design/                 设计文档（本目录）
  research/               调查报告与 prompts
  chromium/               PIN.json, args/*.gn, patches/<series>, series
  scripts/                cr.ps1（补丁/构建工具）, codex-*.ps1/sh（编排工具）
  protocol/               浏览器↔endpoint 协议 schema 与生成器
  endpoint/               Rust（由 prior art prtsd 演进；cargo workspace）
  control-plane/          Node 示例 + viewer 静态页
  tools/                  测量工具（glass-to-glass、网络整形脚本）
```

Chromium checkout 不在仓库内：`C:\src\chromium\src`（见 02 §4、05 §3）。
