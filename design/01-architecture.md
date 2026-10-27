# 01 总体架构

版本：clean-room 复核版，2026-10-25（取代 Stage 1 版本）。输入：`00-brief.md`、`research/r-chromium-map.md`（本次复核的源码地图）、
`research/b1-frame-path.md`、`b2-capture.md`、`b3-pacing.md`、`c1-input-layer.md`、`c2-ime-osk.md`、`d-branding.md`、`g-h264-level.md`。
所有 Chromium 路径相对 `src/`，基线 Stable **157.0.8103.15** / `1aa07b638799e59656559ae6c6649d4b197f555e`（“pin”）。
每条决策只以 Chromium 源码与 brief 为依据；逐条的 kept/changed 记录与证据见 `../archive/design/REVIEW.md`。早期项目（prior art）不作为任何决策的理由。

## 1. 一句话

**打小补丁的 Chrome `--headless=new`（产品名 PRTS，Web 身份 Google Chrome）在 remote 模式下把弹层留在浏览器窗口自己的 Aura 根里，
每个浏览器窗口用 Viz 自带的 `FrameSinkVideoCapturer` 导出一张 D3D11 共享纹理；Chromium 树内构建的 C++ browser-endpoint
（libwebrtc + oneVPL）直接用该纹理编 H.264 并经 WebRTC 发给用户；用户输入以 `ui::Event` 经 `aura::EventInjector` 注入对应
`WindowTreeHost`；浏览器 ↔ endpoint 用 Mojo（spike S10，失败退回带 schema 的管道协议）。**

## 2. 组件与进程

```text
 ┌──────────────────────────── PRTS browser process tree (one per instance) ─────────────────────────┐
 │ chrome.exe --headless=new --rb-remote --rb-mojo=<h> --screen-info={…} --user-data-dir=<inst>  │
 │   browser: chrome/browser/rb/*（控制器、窗口注册、捕获绑定、输入、IME 桥）                         │
 │            Views/Aura + DesktopWindowTreeHostWin + HWNDMessageHandlerHeadless（cloaked HWND 保留）   │
 │   GPU process: viz display compositor + FrameSinkVideoCapturerImpl (D3D11, Intel UHD 630)          │
 └──────▲ Mojo (control, input, IME, capture descriptors / or capturer remote) ────────────────────────┘
        │                                    pixels stay on GPU: D3D11 NT handle + keyed mutex
 ┌──────┴──────────── rb_legacy_endpoint (C++, built in the Chromium tree, one worker per instance) ─────────┐
 │ frame import (open-once cache) · optional scene pass · VideoProcessor BGRA→NV12 · oneVPL H.264       │
 │ libwebrtc PeerConnection (H.264 track + data channels) · pacing controller · metrics                 │
 └──────▲ control API (JSON over stdio/WebSocket)            │ WebRTC (UDP, direct)
 ┌──────┴──── control-plane (Node sample) ──┐   ┌──── viewer (Chrome 89+) ─────────────┐
 │ instance lifecycle, signalling relay     │◄──┤ <video> + input capture + soft keyboard│
 └──────────────────────────────────────────┘   └───────────────────────────────────────┘
```

- 一个 instance = 一个浏览器进程树 + 一个 profile + **一个 endpoint worker 进程**。每台主机一个轻量 supervisor（可先由 control-plane
  样例直接承担）负责拉起 worker。理由：故障隔离（一个编码器/驱动崩溃只影响一个租户）、Job Object 资源记账可以把该租户的编码/网络
  开销也算进去（为 Workstream C 铺路）、Mojo/句柄关系一对一更简单。代价：每个 worker 一份 D3D11 设备与 VPL 会话（UHD 630 上可接受，S7 一并测量）。
- control-plane 不在快路径上：WebRTC 建立后媒体与输入走 endpoint ↔ viewer 直连。

## 3. 关键决策

### D1 基线：pin 到 Windows Stable 157.0.8103.15
- 理由：Web 身份要与一个真实发布的 Chrome 一致（UA 主版本、UA-CH 品牌 GREASE 由上游算法按主版本生成）；stable 分支源码冻结，补丁不漂移；
  libwebrtc 同版本（D12）即与 Chrome 157 自己的发送端一致。
- 否决：跟 main（版本号不对应任何发布版，补丁随时破）。升级策略：每个新 Stable 里程碑可选 rebase（02 §4）。

### D2 运行形态：Chrome `--headless=new` + Aura/Views + 保留 cloaked HWND
- 依据（r-chromium-map §1-2）：`HWNDMessageHandlerHeadless` 保留真实 HWND 并 cloak，激活/capture 都是进程内逻辑状态；
  `--headless` 关闭 native occlusion；cloak 不影响 Present 与 BeginFrame 源。brief 明确要求走这条路径。
- 新发现：headless `Activate()` 不会使同进程其他窗口失活（`hwnd_message_handler_headless.cc:383-395`），D8 必须自己维护互斥。
- 所有 PRTS 行为由运行期开关 `--rb-remote` 守卫；无开关时补丁后的 chrome.exe 等同上游。
- 否决：
  - 自定义 `ui::PlatformWindow` / Windows 上的 headless-ozone 式 shim：`DesktopWindowTreeHostPlatform` 只为 linux/fuchsia 编译
    （`ui/views/BUILD.gn:985-995`），需要移植整条宿主并绕开 `DesktopWindowTreeHostWin` → 补丁大、离开 new-headless 路径（违反 brief）。
  - Ozone headless（Windows 不可用）、headless_shell（无浏览器 UI）、隐藏桌面 + 桌面复制/SendInput（多租户互扰）。

### D3 浏览器 ↔ endpoint IPC：Mojo invitation（spike S10），退路为三条管道 + schema 协议
- 因为 endpoint 改为 Chromium 树内 C++（D12），两端可以共享 `.mojom` 定义、句柄传递（D3D11 NT handle 作为 `PlatformHandle`）、
  版本天然一致（同一 pin 同一构建）。上游先例：`remoting/` 自己的多进程主机就用 Mojo invitation 连接独立启动的进程。
- 启动：endpoint 创建 `mojo::PlatformChannel`，把一端经 `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` 继承给 chrome.exe（`--rb-mojo=<h>`）；
  浏览器（broker）在 PRTS 控制器启动时发送 `OutgoingInvitation`，endpoint 以非 broker 身份接受。
- 更进一步（S10 的第二问）：浏览器可用 `HostFrameSinkManager::CreateVideoCapturer(PendingReceiver<mojom::FrameSinkVideoCapturer>)`
  创建 capturer 并把 **capturer remote 交给 endpoint**，endpoint 自己实现 `mojom::FrameSinkVideoConsumer`：帧回调从 GPU 进程直达 endpoint，
  浏览器 UI 线程不在帧路径上，浏览器侧捕获代码只剩“绑定 + 生命周期”。
- 退路（S10 失败时）：三条继承的匿名管道（cmd/evt/frm，各自 writer 线程，帧描述符不与事件抢队列），命名消息 + 版本号的 schema，
  像素不过管道（NT handle 由 endpoint `DuplicateHandle` 拉取）。
- 否决：DevTools/CDP 作主通道（到不了浏览器 UI、是自动化指纹）；命名管道（命名抢占/ACL）。

### D4 帧导出：每个浏览器窗口根一个 `FrameSinkVideoCapturer`
- 依据（b2 §1-2，r-chromium-map §2）：`VideoCaptureTarget(compositor->frame_sink_id())` 捕获根 render pass（tab strip、omnibox、网页、OOPIF、
  根内弹层），在发现损伤的同一次 `DrawAndSwap` 中执行；Windows 上输出 D3D11 keyed-mutex 共享纹理（ARGB 或 NV12）。
- **R1 已更正**：mappable blit 不阻塞 GPU 主线程，只把结果投递推迟到 GPU 完成（finished-proc + ≤2 ms 轮询，
  `skia_output_surface_impl_on_gpu.cc:1068-1095`）。剩余成本 = 一次全帧 GPU blit + 投递延迟，S4 测量。
- 软件回退：同一 capturer 用 `kDefault` 得到只读共享内存 ARGB；headless 软件合成本来就是内存 `SoftwareOutputDevice`（b1 §2）。
- 必带 viz 修复：`MaybeDeliverFrame` 失败/版本变化路径补 `CompleteCapture`（b2 §3 在 pin 上确认），独立可上游化。
- 否决/后备：
  - 自定义 `SkiaOutputDevice` 直接画进共享纹理：省一次 blit 与无用的 DComp present，但 b1 §5 估 10-16 个 viz 文件 + Mojo 改动，
    GPU/软件两套，要处理 overlay 能力/resize/context loss → 只有 S4 证明捕获成本不可接受时才做（spike S12）。
  - `ui::Layer::RequestCopyOfOutput` 每帧一次：一次性请求、无缓冲池与采样器，等于手写一个更差的 capturer。
  - content 层 per-WebContents 捕获 + Views 单独处理浏览器 UI：两路流、需要在 endpoint 重新拼装浏览器 UI，违背“UI chrome 必须可见”的简单性。
  - 拦截 DComp/swap chain Present：DComp 路径没有可读的统一根 swap chain（b1 §1）。DWM/桌面捕获：cloaked 不可见、租户不可分。

### D4b 弹层留在浏览器根内（remote 模式）——新增，spike S11
- 依据：Windows 上菜单/气泡/tooltip 与 content 的 `<select>` 弹层默认各自成为新的顶层 `DesktopWindowTreeHostWin`
  （`chrome_views_delegate_win.cc:123-160`、`desktop_native_widget_aura.cc:252-276`）。上游已有两种“根内弹层”变体：
  `shadow_elevation` 强制 `NativeWidgetAura`，以及 Fuchsia 关闭 `GetDefaultParent` 的顶层分支。ChromeOS 上所有 Chrome 弹层都是根内 `NativeWidgetAura`。
- 方案：remote 模式下 `ChromeViewsDelegate::CreateNativeWidget` 对“有 parent 的非顶层窗口”选 `kNativeWidgetAura`（chrome 层，~6 行），
  `GetDefaultParent` 像 Fuchsia 一样返回 `root_window_`（Views 层，~3 行，由 `ui` 级开关守卫）。
- 收益：一个浏览器窗口 = 一个根 = 一个 capturer = 一个 input sink；不需要弹层宿主跟踪、每弹层 capturer、endpoint 侧 z-order 合成与跨根输入路由。
- 代价：弹层不能超出浏览器窗口边界——remote 浏览器窗口就是整个视口，且 `--screen-info` 把虚拟屏幕设为窗口大小，菜单定位天然落在根内。
- 退路（S11 失败）：每个弹层根一个 capturer + endpoint 场景合成 + 跨根输入路由（Stage 1 方案）。

### D5 GPU 帧交接：直接使用 Viz 池纹理（零额外拷贝）
- endpoint 对每个池缓冲区 `OpenSharedResource1` 一次并缓存；每帧 `AcquireSync(0)` → VideoProcessorBlt（BGRA→NV12，直接写编码器输入面）→
  `ReleaseSync(0)` → 归还帧（`FrameSinkVideoConsumerFrameCallbacks::Done()`）。跨设备顺序由 keyed mutex 保证；结果投递前 GPU 已完成（D4）。
- 保守变体（S3 对照）：acquire 后先 `CopyResource` 到 endpoint 私有纹理再立即释放（一次 GPU 拷贝），用于 keyed mutex 持有时间影响 Viz 池时。
- NV12 直出（capturer `PIXEL_FORMAT_NV12`）为实验 E8：省掉 endpoint 的 VPP，D4b 成立后不再需要 RGBA 合成，因此更有吸引力。

### D6 帧节奏：P0 vsync + 捕获节流；P1 外部 BeginFrame
- P0：Viz 跟 `VSyncThreadWin`（cloak 不影响，失败退化 Sleep），capturer `SetMinCapturePeriod(1/fps)`、关闭 auto-throttling 与 animation lock-in，
  endpoint “最新帧覆盖”。代价：平均 ½ vsync 相位等待。
- P1（S5）：remote 模式下 `DesktopWindowTreeHostWin::Init` 以 `CreateCompositor(force_sw, /*external_begin_frame=*/true)` 创建（≤10 行），
  `set_wait_for_all_frame_sinks(false)`（同 `DesktopWindowTreeHostPlatform`），per-root pacer 一次只发一个 `IssueExternalBeginFrame`，
  由 endpoint 在编码器即将空闲时触发，并有最低频率兜底。子 sink 继承根的源，所以控制根即控制 rAF/合成（b3 §4）。
- 否决：`--fake-vsync-rate`（GPU 进程全局、启动期固定）、`--disable-frame-rate-limit`（空转）、`SetDisplayVSyncParameters`（对 Win DComp 根无效，b3 §3）。

### D7 输入：`aura::EventInjector::Inject` 注入 `ui::Event` 到目标 `WindowTreeHost`
- 依据（c1 §1，r-chromium-map §3）：sink → `WindowEventDispatcher` 同时到达 Views（tab strip/omnibox/菜单）与 content
  （`RenderWidgetHostInputEventRouter`，Viz hit-test，OOPIF）；touch 走 `GestureRecognizer`，content 里是真 `WebTouchEvent`。
  `EventInjector` 已实现 root transform（DIP→host 像素）后送 sink，不必自写。
- 必需的 remote 模式补丁（c1 §2-3）：`InputStateLookupWin` 不读 `::GetKeyState`；headless `HasCapture()` 返回逻辑 capture；
  `::SetCursor/SetCursorPos/ClipCursor/ShowCursor`、系统键盘钩子、`::DoDragDrop`、touch-drag 的 `ui::SendMouseEvent` 改为上报或 no-op；
  `ScreenWinHeadless` 虚拟光标点同步；离开根时补 exit。D4b 成立后只剩浏览器窗口之间的根选择。
- 否决：`SendInput`（桌面全局前台/焦点，多租户互抢，brief 明确排除）；向 cloaked HWND `PostMessage` 鼠标/键盘消息（touch 需要
  `GetPointerInfo` 的真实指针设备、键盘状态仍读全局 `GetKeyState`，本质仍是 OS 输入语义）；`RenderWidgetHost::Forward*` / DevTools Input
  （只到网页，到不了浏览器 UI）；“content 区用 RWH API + UI chrome 用 Views”的分裂路由（两套命中测试与焦点模型，Aura 已统一做好）。

### D8 焦点与激活：每 instance 逻辑激活，控制器维护单一活动根
- 状态在各自进程内（`wm::FocusController` per root，`HWNDMessageHandlerHeadless::is_active_`），跨进程不竞争；不调用 `SetForegroundWindow`。
- 因为 headless 不做激活互斥（D2），控制器在激活某浏览器窗口时显式 `Deactivate()` 其他根；viewer 失去控制权 → 释放按住的键/键钮、取消触点、`Deactivate`。
- 键盘目标 = 活动根的 `FocusClient`（D4b 后菜单在根内，由 Aura capture/focus 正常处理）。

### D9 可编辑焦点检测与远程软键盘
- 依据（r-chromium-map §4）：headless 已用 `MockInputMethod`（不会弹 TabTip/InputPane）；它通知 `OnTextInputStateChanged` 与
  `OnVirtualKeyboardVisibilityChangedIfEnabled`，但 detach/空焦点不通知；Windows 上 content 的“该弹/该收”策略在
  `VirtualKeyboardControllerWin` 里，经 `InputMethod::GetVirtualKeyboardController()` 输出。
- 方案：remote 模式下工厂返回 `ui::RemoteInputMethod`（派生自 `MockInputMethod`：补 detach/空焦点通知；`GetVirtualKeyboardController()`
  返回把 Display/Dismiss 转发给观察者的 controller）。PRTS 每根注册 `InputMethodObserver`，读 `TextInputClient` 的 type/mode/flags/caret/focus reason，
  发 `IME_STATE`。Views 文本框（omnibox）触摸聚焦走 `Textfield` → `ShowVirtualKeyboardIfEnabled` → 同一出口。
- 文本回填：viewer 提交文本 → `TextInputClient::InsertText`；组合（可选）→ `SetCompositionText`。
- enterkeyhint / vk_policy 原值：`TextInputClient` 没有访问器；policy 已体现在 content 的显示决策里，enterkeyhint 只影响软键盘回车键标签 →
  可选的后续补丁（给 `TextInputClient` 加两个带默认实现的虚函数），不进 M2 必需集。

### D10 触摸能力暴露：chrome 层覆写 WebPreferences（改）
- `navigator.maxTouchPoints`、`ontouchstart` 特性检测、`pointer/hover` 媒体查询都来自 `WebPreferences`；`WebContentsImpl::ComputeWebPreferences`
  在加载 `SlowWebPreferenceCache`（含 `ui::MaxTouchPoints()`）之后调用 embedder 的 `OverrideWebPreferences`（r-chromium-map §5）。
- 方案：PRTS 的 `ChromeContentBrowserClientParts::OverrideWebPreferences` 按 instance 参数 `touch_points`（0 或 1-10）设置
  `pointer_events_max_touch_points`、`touch_event_feature_detection_enabled`、`available_pointer_types/primary_pointer_type`、hover 类型；
  上游改动仅一行注册 extra part。viewer 声明触摸设备时默认开启；运行期变化经 `OnWebPreferencesChanged` 生效（S13 验证）。
- 否决：改 `ui/base/pointer/pointer_device_win.cc`（进程全局、影响 Views 的触摸 UI 判定，且是 ui 层补丁）；`--touch-events`（不改 maxTouchPoints）。

### D11 品牌：产品 PRTS，Web 身份 Google Chrome
- `components/embedder_support/user_agent_utils.cc` 两处（`:162-163` 品牌、`:449-450` `Headless` 前缀），由 `--rb-remote` 守卫；其余 UA reduction/GREASE/
  client hints/`userAgentData` 全部走上游流水线；`BRANDING` + 用户可见 GRIT 字符串改 PRTS（d-branding）。
- 否决：`--user-agent`（UA-CH 元数据 early-return，d-branding）、`is_chrome_branded=true`、在 JS/HTTP 两端分别伪造。
- 配套构建参数（建议给 Stage 2）：`proprietary_codecs=true`、`ffmpeg_branding="Chrome"`——真 Chrome 能播 H.264/AAC，Web 身份与“视频播放”测试场景都需要。

### D12 endpoint：Chromium 树内 C++ + libwebrtc（pin 版本）+ oneVPL（改）
- 理由（详见 03 §3 与 REVIEW）：libwebrtc 是 Chrome 89 接收端的配对实现（GCC/TWCC、pacer、NACK/RTX、FEC、playout-delay、frame dropping 都经过大规模验证），
  P99 风险最低；oneVPL 是 C API，D3D11 是 C++ 原生，Rust 方案本来也要一层 C++ 桥；树内构建复用同一 checkout/工具链，无需第二套 WebRTC 同步；
  上游 `remoting/` 正是“树内原生主机 + libwebrtc + 硬件 H.264 + `VideoPlayoutDelay::Minimal()`”（r-chromium-map §7）。
- 编码：自写 `webrtc::VideoEncoder`（oneVPL，D3D11 视频内存输入，native `VideoFrameBuffer` 携带 NV12 纹理）；回退：`media::VideoEncodeAccelerator`（MF）、
  OpenH264（`rtc_use_h264`，CPU）。
- 源码放 remotekit `endpoint/`，以目录联接（junction）映射到 `src/rb_legacy_endpoint/`；上游改动仅 `//BUILD.gn` 一行依赖（02 补丁 15）。
- 否决：Rust + webrtc-rs（GCC/pacer 成熟度未经验证、仍需 C++ 桥）；独立 libwebrtc 检出（第二套 gclient/工具链、版本漂移）；把 endpoint 放进浏览器进程（扩大补丁面、编码/网络进入将要被沙箱化的进程）。
  str0m 列为候选，见 REVIEW。

### D13 control-plane 与 viewer
- control-plane：Node 样例（实例生命周期、信令中继、调试页），按新 endpoint 控制协议编写；viewer 必须在 Chrome 89 可运行（新 API 做特性检测）。
  现有拷入的代码只是候选，见 REVIEW。

## 4. 端到端数据流（一帧）

1. 网页/浏览器 UI（含根内弹层）变化 → 提交 CompositorFrame → Viz 在 BeginFrame deadline 聚合。
2. 根 sink 有损伤 → capturer 采样 → `CopyOutputRequest` 随同一次 DrawAndSwap 执行 → GPU 完成后投递 `OnFrameCaptured(NT handle, info, rects)`。
3. endpoint（直接作为 consumer，或经浏览器转发描述符）→ 缓存的纹理 → keyed mutex(0) → VPP 到编码器 NV12 面 → release → `Done()`。
4. oneVPL `EncodeFrameAsync` + `SyncOperation` → libwebrtc（RTP 打包、playout-delay (0,0)、pacer）→ UDP。
5. viewer 解码显示；`requestVideoFrameCallback` 回报显示时间用于测量。

输入：viewer PointerEvent/KeyboardEvent → 有序可靠 data channel → endpoint 校验/映射 → Mojo `Input*` → 浏览器 UI 线程构造 `ui::Event` →
`aura::EventInjector::Inject(活动根 host)`。

## 5. 延迟预算（capture→display P80 < 100 ms，P99 < 200 ms）

brief 的“50 ms latency”按 RTT 50 ms 解读（单向 ~25 ms），实验同时覆盖单向 50 ms。下表全部是估计，M3 用测量替换。

| 段 | 典型 | P99 预算 | 说明 |
|---|---:|---:|---|
| 产生到捕获投递（vsync 相位 + 合成 + blit + GPU 完成） | 10 ms | 25 ms | P1 外部 BeginFrame 后约 5 ms |
| 交接 + BGRA→NV12 | 1 ms | 4 ms | 无 CPU 轮询；NV12 直出可省 |
| H.264 编码（1080p，UHD 630 VDEnc） | 6 ms | 15 ms | S7 实测 |
| pacer + 串行化（12 Mbps） | 10 ms | 40 ms | IDR/场景切换是 P99 主因 → VBV/MaxFrameSize/帧内刷新 |
| 网络单向 + Wi-Fi 抖动 | 25 ms | 60 ms | |
| 接收组帧/抖动缓冲（playout-delay 0） | 3 ms | 20 ms | |
| 解码 + 显示 | 15 ms | 30 ms | |
| **合计** | **~70 ms** | **~194 ms** | input-to-photon 另加上行 ~25 ms，单独报告 |

## 6. 仓库布局

```text
remotekit/
  design/  research/                  设计、调查结果
  archive/                            阶段计划、状态、提示词、旧工作流脚本
  browser/                            PIN.json, args/, series, patches/, branding/, cr.cmd
  endpoint/                           旧 C++ endpoint（symlink 到 src/rb_legacy_endpoint）
  re/                                 Rust endpoint
  control-plane/  viewer/              Node 控制平面与浏览器端 viewer
  tools/                              测量、网络整形、isolation/ ACL 工具
```
Chromium checkout：`C:\src\chromium\src`（不入库）。
