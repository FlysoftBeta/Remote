# REVIEW：clean-room 架构复核（2026-10-25）

范围：01 的 D1-D13、02 的补丁清单、03 的 endpoint 栈。方法：不打开 prior art（`C:\Users\Flysoft\Desktop\chromium`、`research/a-prior-art.md`），
把 Stage 1 设计里所有以 prior art 为理由的论断视为未证明，只用 pin（157.0.8103.15 / `1aa07b63`）上的 Chromium 源码与 brief 重新推导。
源码地图与新证据：`research/r-chromium-map.md`（下称 MAP）。Stage 1 的 Chromium 侧报告（b1/b2/b3/c1/c2/d）关键行抽查后沿用。

## 1. 结论一览

| 决策 | 结果 | 一句话 |
|---|---|---|
| D1 Stable 157 pin | 保留 | 理由改为自身：真实发布版的 Web 身份、冻结分支、libwebrtc 同版本 |
| D2 new-headless + cloaked HWND | 保留 + 补充 | 新发现：headless 激活不互斥；自定义 PlatformWindow 方案否决 |
| D3 IPC | **改** | 三条管道 → Mojo invitation（S10）；管道协议降为退路 |
| D4 FrameSinkVideoCapturer | 保留，风险更正 | R1“GPU 线程等待”不成立：只是投递推迟 |
| D4b 弹层留在根内 | **新增** | 一个浏览器窗口 = 一个根/一个 capturer/一个 sink（S11） |
| D5 GPU 交接 | **改** | 零拷贝为默认设计；去掉“prior art 双拷贝保底”，保守变体自己定义 |
| D6 节奏 | 保留 | P0 vsync + 捕获节流，P1 外部 BeginFrame |
| D7 输入注入层 | 保留 + 细化 | 用现成 `aura::EventInjector`；多根路由在 D4b 后消失 |
| D8 焦点/激活 | 保留 + 补充 | 控制器必须维护单一活动根 |
| D9 IME/软键盘 | 保留，缩小 | 派生 MockInputMethod；`TextInputClient` 扩展移出必需集 |
| D10 触摸能力 | **改** | `pointer_device_win.cc` 补丁 → chrome 层 `OverrideWebPreferences` |
| D11 品牌 | 保留 | 两处钩子已核对；建议 `proprietary_codecs=true` |
| D12 endpoint 栈 | **改** | Rust/webrtc-rs → Chromium 树内 C++ + libwebrtc + oneVPL |
| D13 control-plane/viewer | 保留（措辞） | 不再“复用 prior art server/”，拷入代码只是候选 |
| 补丁 11 headless-geometry | **取消** | `--screen-info` 已覆盖；运行期变化看 S6 |

## 2. 逐条记录

### D1 基线
- 结论：保留。删除了“prior art 用 M158 main 不可复现官方 UA”的理由，改为：UA 主版本/UA-CH GREASE 由上游按主版本生成，只有对齐一个发布版才“像真 Chrome”；
  stable 分支冻结；D12 后 endpoint 的 libwebrtc 与 Chrome 157 发送端同源。

### D2 运行形态
- 候选：(a) new-headless + DesktopWindowTreeHostWin（现方案）；(b) Windows 上的自定义 `ui::PlatformWindow`/headless-ozone 式 shim；(c) Ozone headless；
  (d) headless_shell；(e) 隐藏桌面 + 桌面捕获/SendInput。
- 证据：`HWNDMessageHandlerHeadless` 逻辑激活/capture、cloak（c1 §3，抽查 `hwnd_message_handler_headless.cc:383-406`）；`DesktopWindowTreeHostPlatform` 仅
  linux/fuchsia 编译（`ui/views/BUILD.gn:985-995`），(b) 需要移植宿主并绕开 new-headless；(c)(d)(e) 分别因不支持 Windows/无浏览器 UI/多租户互扰被排除。
- 新发现：`Activate()` 不使其他根失活 → D8。

### D3 IPC — 改
- 候选：三条管道 + 自定义帧格式；命名管道；CDP；Mojo invitation；共享内存环。
- 为什么改：Stage 1 选管道的前提是“endpoint 不是 Chromium 进程”。D12 让 endpoint 成为同树 C++ 程序后，Mojo 给出类型化接口、原生句柄传递、同构建版本一致，
  上游 `remoting/` 用 Mojo invitation 连接独立启动的进程。进一步可把 capturer remote 交给 endpoint，帧回调不经浏览器 UI 线程。
- 风险：broker 向非 Chromium 启动的进程传句柄、GPU 进程 → endpoint 的 consumer 管道 → S10，退路明确。

### D4 帧导出
- 候选：root `FrameSinkVideoCapturer`；自定义 `SkiaOutputDevice`/`SoftwareOutputDevice`；`Layer::RequestCopyOfOutput`；`HostFrameSinkManager::RequestCopyOfOutput`；
  content per-WebContents 捕获 + Views；DComp/swap-chain 拦截；DWM 捕获。
- 证据：b2 §1-2（同一 DrawAndSwap、D3D11 keyed mutex、ARGB/NV12）；**新**：`skia_output_surface_impl_on_gpu.cc:1068-1095` 的 mappable 路径用 finished-proc +
  `ScheduleCheckReadbackCompletion` 异步投递，不阻塞 GPU 主线程（Stage 1 的 R1 表述过重）。自定义 OutputDevice 省一次 blit，但 b1 §5 估 10-16 文件 → S12 后备。
- 保留 viz CompleteCapture 修复：理由是 b2 §3 在 pin 上读到的 bug（`MaybeDeliverFrame` :1524），不再引用 prior art 补丁号。

### D4b 弹层留在根内 — 新增
- Stage 1 未考虑。证据：`chrome_views_delegate_win.cc:123-160`（Windows 弹层默认新顶层根，但 `shadow_elevation` 已强制根内）、
  `desktop_native_widget_aura.cc:252-276`（`<select>` 等 MENU 窗口建顶层父窗口；Fuchsia 关闭该分支）。ChromeOS 全部根内。
- 收益：删除弹层宿主跟踪、每弹层 capturer、endpoint z-order 合成、跨根输入/键盘根规则。代价：弹层不能越出窗口——remote 下窗口即视口，可接受。
- 风险：Windows 上从未以这种组合运行过的 Views 路径 → S11，退路 = Stage 1 的多根方案。

### D5 GPU 交接 — 改
- 去掉“prior art 双拷贝（浏览器导出池 key0/1 + endpoint 私有拷贝）作保底”。Chromium 的 GMB 池已是 keyed-mutex 共享纹理（key 0，b2 §2），且结果在 GPU 完成后才投递，
  因此直接使用池纹理是自然设计；保守变体是 endpoint 侧“Blt 后 EVENT 等待”或“立即私有拷贝后释放”，都不需要浏览器侧额外导出池。

### D6 节奏
- 候选：vsync + 捕获节流；外部 BeginFrame；`--fake-vsync-rate`；`--disable-frame-rate-limit`；`SetDisplayVSyncParameters`/`SetPreferredInterval`；viz synthetic 策略。
- 证据：b3 §1-3（抽查 `DesktopWindowTreeHostWin::Init` 未开 external control；CEF OSR 的外部 BeginFrame 用法作为上游侧参照）。保留。

### D7 输入
- 候选：WindowTreeHost sink（经 `aura::EventInjector`）；直接 `GetEventSink()->OnEventFromSource`（绕过 rewriter）；PostMessage 到 cloaked HWND；SendInput；
  RWH/DevTools；content 用 RWH + chrome 用 Views 的分裂路由。
- 证据：c1 §1-5；**新**：`ui/aura/event_injector.cc` 已做 `UpdateForRootTransform` 后送 sink。PostMessage 否决：WM_POINTER 需要真实指针设备信息，键盘状态仍读全局 `GetKeyState`。
- 06 补丁中的 Win32 调用点（`::SetCursor` 等）全部来自 c1 的 pin 源码审计，与 prior art 无关。

### D8 焦点
- 保留逻辑激活；新增“控制器维护单一活动根”（D2 新发现）。Stage 1 引用的 prior art `KeyRoot()` 规则删除，D4b 后不需要跨根键盘规则。

### D9 IME/软键盘
- 证据：`input_method_factory.cc:50-53`（headless → Mock）；`mock_input_method.cc`（通知集合、detach 不通知、VK controller stub）；
  `render_widget_host_view_aura.cc:3711-3735`（Windows 上 `VirtualKeyboardControllerWin` 经 `GetVirtualKeyboardController()` 输出）。
- 改动：`RemoteInputMethod` 明确为 `MockInputMethod` 子类；`TextInputClient` 新增 action/vk_policy 与 RWHVA 改动移出必需集（policy 已体现在 content 的显示决策里）。

### D10 触摸能力 — 改
- 候选：改 `pointer_device_win.cc`；`--touch-events`；`ChromeContentBrowserClientParts::OverrideWebPreferences`；CDP 触摸模拟。
- 证据：`web_contents_impl.cc:4001/4252`（缓存后再 embedder 覆写）、`chrome_content_browser_client.cc:5014`、`web_preferences.h:129-132`。
  新方案是 chrome 层、按 instance、不影响 Views 触摸 UI 判定，上游改动 1-3 行。

### D11 品牌
- 抽查 `user_agent_utils.cc:162-163`、`:449-450`。守卫改为运行期 `--prts-remote`（而非 `IS_WIN` 编译条件）。新增建议：`proprietary_codecs=true`、`ffmpeg_branding="Chrome"`。

### D12 endpoint 栈 — 改
- Stage 1 理由是“沿用 prtsd”，按本次规则不成立。重新评估见 03 §3.1。决定性因素：
  1. P99 主要由发送端拥塞控制/pacer/丢包恢复与接收端互操作决定；libwebrtc 是 Chrome 接收端的配对实现，webrtc-rs 在这点上没有源码级证据（Stage 1 自己列为 R5/S9）。
  2. oneVPL 与 D3D11 是 C/C++ API，Rust 方案也必须带一层 C++ 桥。
  3. 树内构建复用已有 checkout，libwebrtc 已是 chrome 的依赖；可共享 `.mojom` 与 GMB 句柄类型（D3）。
  4. 上游 `remoting/` 证明“树内原生主机 + libwebrtc + Windows 硬件 H.264 + `VideoPlayoutDelay::Minimal()`”可维护（MAP §7）。
- 代价：endpoint 跟 Chromium 构建绑定；C++ 内存安全靠纪律与 base/ 工具；首次需要摸清 `build_with_chromium` 下的 socket/线程配置（S9a）。
- 进程拓扑：改为每 instance 一个 endpoint worker + 主机 supervisor（故障隔离、Job 记账、句柄关系一对一）。

### D13 control-plane/viewer
- 保留 Node 样例与 Chrome 89 约束；“复用 prior art server/ 与 viewer JS”改为候选。

## 3. 补丁清单变化（02）
- 新增：11 `remote-popups-in-root`、09 `prts-web-prefs`、14 `prts-endpoint-target`。
- 取消：旧 11 `headless-geometry`（`--screen-info`，`headless_mode_browsertest_win.cc:237`）、旧 09 `virtual-touch-capability`。
- 缩小：03 registry（只跟踪浏览器窗口）、05 capture（不再有浏览器侧导出池与弹层源）、08 remote-ime（无 `TextInputClient`/RWHVA 改动）。
- 上游改动估计：~600 行 → ~250 行。
- 启动参数：删除仅以 prior art 事故为理由的 `--wm-window-animations-disabled`、`--disable-features=UiGpuRasterization`、`--disable-print-preview`，移入下方候选。

## 4. prior art 中可能仍有用的做法（候选，待评估，绝非默认）
| 候选 | 若采用需要先验证什么 |
|---|---|
| `--wm-window-animations-disabled`（据称规避 select 关闭时的 UAF） | 在我们的 non-component 构建上复现该崩溃；D4b 后弹层不再是独立根，问题可能不存在 |
| `--disable-features=UiGpuRasterization`（据称冷 shader 使弹层延迟） | 用 trace 测弹层首帧延迟 |
| endpoint 侧帧号色块像素校验、netem 单向整形器 | 作为测量工具可借鉴思路，按 03 §5 重新实现 |
| 已拷入 `endpoint/` 的 Rust 代码（Job Object 启动器、D3D11 帮助函数、MF 编码器、viewer） | D12 改为 C++ 后，只有 viewer JS 与 Job Object 启动逻辑可能值得参考 |
| 已拷入 `control-plane/` 的 Node 服务与 viewer | 按新的 endpoint 控制协议逐个文件评估 |
| 弹层宿主跟踪与 endpoint z-order 合成 | 仅当 S11 失败时才需要，届时按 MAP/c1 重新设计 |
| 构建/补丁工具中的经验（构建锁、`.exit` 文件、`cmd /d /c` 重定向） | 由 Stage 2 在 `cr.ps1` 中自行判断 |

## 5. 新增/变化的 spike
S10（Mojo 链接与 consumer 直达）、S11（弹层留在根内）、S12（自定义 OutputDevice，条件触发）、S13（WebPreferences 运行期切换）；
S9 改为 libwebrtc 树内集成 + 排队测量；S1（prior art 补丁移植量）删除；S6 缩小为运行期屏幕变化。

## 6. 对 M0/M1 的影响
- `endpoint/`：停止在 Rust prtsd 拷贝上投入；M0 改为树内 `prts_endpoint` 目标（合成帧 → libwebrtc → viewer）。
- 补丁：M1 需要 01/02/03/04/05/06/07/11/12/13/14；不再需要弹层跟踪与 endpoint 合成；Mojo（S10）和弹层根内化（S11）是 M1 最早要做的两个 spike。
- 构建参数（Stage 2 拥有）：建议 `proprietary_codecs=true`、`ffmpeg_branding="Chrome"`；需要 OpenH264 回退时 `rtc_use_h264=true`。
