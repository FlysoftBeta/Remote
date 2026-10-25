# 02 浏览器补丁清单与补丁维护基础设施

基线：Chromium Stable **157.0.8103.15** / `1aa07b638799e59656559ae6c6649d4b197f555e`。版本：clean-room 复核版（2026-10-25）。
每个补丁的依据是 pin 上的源码（`research/r-chromium-map.md`、b1/b2/b3/c1/c2/d），逐条复核记录见 `REVIEW.md`。

原则：
1. **新代码放新文件**：PRTS 逻辑在 `chrome/browser/prts/`；ui 层不能依赖 chrome/ 的部分放 `ui/base/ime/remote_input_method.*`。
2. **上游文件只放钩子**：每处 ≤ 30 行，标注 `// PRTS:`，由运行期开关 `ui::IsPrtsRemoteMode()`（`ui/base/ui_base_switches`，`--prts-remote`）守卫；
   无开关时行为与上游一致。Views/Aura 不依赖 chrome/；content 不新增 public API。
3. **优先用上游已有扩展点**（embedder 接口、`ChromeContentBrowserClientParts`、`aura::EventInjector`、`HostFrameSinkManager`、`--screen-info`），
   其次才是钩子。
4. 可上游化的修复独立成补丁、带单测。

## 1. 补丁系列（按应用顺序）

| # | 名称 | 上游文件（钩子） | 新文件 | 估计规模 | 依据 |
|---|---|---|---|---|---|
| 01 | `prts-switch` | `ui/base/ui_base_switches.{h,cc}`（`kPrtsRemote` + `IsPrtsRemoteMode()`） | — | +15 | 所有钩子共用的守卫 |
| 02 | `prts-bootstrap` | `chrome/browser/BUILD.gn`（引入 `prts_sources.gni`）、`chrome/browser/headless/chrome_browser_main_extra_parts_headless.{h,cc}`（启动/关闭控制器） | `chrome/browser/prts/{prts_controller,prts_link,prts_switches}.*`、`chrome/browser/prts/mojom/*.mojom`（与 endpoint 共享） | 上游 +25；新 ~800 | D3：Mojo invitation（S10）；退路 `prts_pipe_win.*` |
| 03 | `prts-registry` | 无（`BrowserList`、`TabStripModelObserver`、`aura::WindowTreeHostObserver`） | `prts_window_registry.*` | 新 ~500 | 只跟踪浏览器窗口根（D4b 成立时无需弹层宿主跟踪） |
| 04 | `viz-capturer-complete-capture` | `components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.cc`（`MaybeDeliverFrame` 失败/版本变化路径调用 `oracle_->CompleteCapture(..., false, ...)`）+ unittest | — | +20/-5 + test | b2 §3：pin 上 bug 仍在（:1524） |
| 05 | `prts-capture` | 无（`HostFrameSinkManager::CreateVideoCapturer`、`WindowTreeHost::CreateVideoCaptureLock` 均为现有 API） | `prts_capture.*`（每浏览器根一个 capturer：配置 + 把 capturer remote 交给 endpoint，或在浏览器内做 consumer 并转发描述符） | 新 ~300-600 | D4/D5 |
| 06 | `aura-views-remote-input` | `ui/aura/input_state_lookup_win.cc`（remote 模式不读 `::GetKeyState`，由事件 flags 维护）；`ui/views/win/hwnd_message_handler_headless.{h,cc}`（逻辑 `HasCapture/SetCapture/ReleaseCapture`）；`ui/views/widget/desktop_aura/desktop_window_tree_host_win.cc`（`SetCursorNative`→上报；`MoveCursorToScreenLocationInPixels`/`UpdateMouseLockRegion`/`OnCursorVisibilityChangedNative`/`CaptureSystemKeyEventsImpl`/`StartTouchDrag` no-op）；`ui/views/widget/desktop_aura/desktop_drag_drop_client_win.cc`（不进 `::DoDragDrop`）；`ui/display/win/screen_win_headless.{h,cc}`（虚拟光标点的非测试 setter） | `ui/views/widget/desktop_aura/remote_cursor_sink.h`（Views 层观察者接口） | 上游 ~+110 | c1 §2-3（逐个 Win32 调用点已列出） |
| 07 | `prts-input` | 无 | `prts_input.*`（鼠标/滚轮/键盘/触摸 → `ui::Event` → `aura::EventInjector::Inject`）、`prts_touch.*`（0-9 触点池、源时钟映射、cancel）、活动根选择与按住状态 reset | 新 ~800 | c1 §1/§4，D7/D8 |
| 08 | `remote-ime` | `ui/base/ime/init/input_method_factory.cc`（headless 分支前：remote 模式 → `RemoteInputMethod`）；`ui/base/ime/BUILD.gn` | `ui/base/ime/remote_input_method.{h,cc}`（派生 `MockInputMethod`：detach/空焦点通知 + 转发型 VK controller）、`chrome/browser/prts/prts_ime_bridge.*` | 上游 ~+15；新 ~500 | D9；r-chromium-map §4 |
| 09 | `prts-web-prefs` | `chrome/browser/chrome_content_browser_client.cc`（注册 PRTS 的 `ChromeContentBrowserClientParts`，1-3 行） | `chrome/browser/prts/prts_content_browser_client_part.*`（`OverrideWebPreferences`：maxTouchPoints、pointer/hover 类型、touch 特性检测） | 上游 +3；新 ~120 | D10；替代 Stage 1 的 `pointer_device_win.cc` 补丁 |
| 10 | `external-begin-frame` | `ui/views/widget/desktop_aura/desktop_window_tree_host_win.cc`（`Init`：remote 模式 + `--prts-external-begin-frame` 时 `CreateCompositor(force_sw, true)`，`set_wait_for_all_frame_sinks(false)`） | `chrome/browser/prts/prts_frame_pacer.*` | 上游 ~+8；新 ~200 | b3 §2/§5；S5 |
| 11 | `remote-popups-in-root` | `chrome/browser/ui/views/chrome_views_delegate_win.cc`（remote 模式：有 parent 的非顶层 widget 用 `kNativeWidgetAura`，~6 行）；`ui/views/widget/desktop_aura/desktop_native_widget_aura.cc`（`GetDefaultParent`：remote 模式同 Fuchsia 返回 `root_window_`，~3 行） | — | +10 | D4b；S11（失败则删除本补丁，改走多根捕获） |
| 12 | `prts-branding` | `chrome/app/theme/chromium/BRANDING`（PRODUCT 值）、`chrome/app/chromium_strings.grd`、`chrome/app/settings_chromium_strings.grdp`（只改用户可见产品名） | — | 字符串 | d-branding |
| 13 | `chrome-web-identity` | `components/embedder_support/user_agent_utils.cc`（`GetUserAgentBrandList`：remote 模式 brand="Google Chrome"；`GetUserAgentInternal`：remote 模式不加 `Headless` 前缀）+ 调整相应单测期望 | — | +10 + tests | d-branding；pin 上 `:162-163`、`:449-450` 已核对 |
| 14 | `prts-endpoint-target` | `//BUILD.gn`（`gn_all` 增加 `//prts_endpoint`，1 行） | `src/prts_endpoint` 为指向 remotekit `endpoint/` 的目录联接，不进补丁 | +1 | D12 |

与 Stage 1 的差异：`headless-geometry`（旧 11）取消——虚拟屏幕尺寸/DSF/工作区由 `--screen-info` 提供（Windows new-headless 支持，
`headless_mode_browsertest_win.cc:237`）；运行期改尺寸/DSF 是否需要钩子由 S6 决定。`virtual-touch-capability`（旧 09，ui/base）被 09 `prts-web-prefs`（chrome 层）取代。
`TextInputClient` 新增 action/vk_policy 访问器移出必需集（D9）。

启动参数（无需补丁）：`--headless=new --prts-remote`、`--screen-info={WxH devicePixelRatio=…}`、`--user-data-dir=<per instance>`、`--no-first-run`、
`--no-default-browser-check`、`--disable-legacy-window`（不创建 `LegacyRenderWidgetHostHWND` 子窗口，c1 §3）。
其它参数（窗口动画、UI GPU 光栅等）只有在我们自己观察到问题时才加，见 REVIEW 的候选清单。

**规模估计**：上游约 15 个文件、~250 行（不含字符串与测试）；新文件约 3500 行，几乎全部在 `chrome/browser/prts/`。

## 2. 关键补丁细节

### 2.1 启动与链接（02）
- endpoint 启动 chrome.exe 时继承一个 `mojo::PlatformChannel` 端点（`--prts-mojo=<handle>`，经 `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` 精确继承），
  浏览器启动早期清除其可继承标志防止再传给子进程。控制器挂在 `ChromeBrowserMainExtraPartsHeadless`，所有 Chrome 对象操作在 UI 线程。
- 接口（`.mojom`，与 endpoint 共享，同 pin 同构建，无需版本协商以外的兼容层）：`PrtsBrowser`（endpoint→browser：窗口/标签操作、
  `Input*`、`Focus`、`Reset`、`PaceConfig/PaceTick`、`Refresh`）与 `PrtsBrowserClient`（browser→endpoint：`Hello{gpu_luid, vendor/device, software}`、
  窗口生命周期、`ImeState`、`Cursor`、捕获源出现/消失 + `pending_remote<viz.mojom.FrameSinkVideoCapturer>` 或帧描述符）。
- 退路协议（S10 失败）：三条管道 + 命名消息 schema，消息集合同上。

### 2.2 捕获（05）
- 每个浏览器窗口根：`host->compositor()->context_factory()->GetHostFrameSinkManager()->CreateVideoCapturer(...)`，
  `ChangeTarget(VideoCaptureTarget(compositor->frame_sink_id()))`，持有 `WindowTreeHost::CreateVideoCaptureLock()`。
- 配置：`SetFormat(ARGB)`（NV12 为 E8）、`SetMinCapturePeriod(1/fps)`、`SetMinSizeChangePeriod(0)`、`SetAutoThrottlingEnabled(false)`、
  `SetAnimationFpsLockIn(false, …)`、固定分辨率 = 根像素尺寸、`Start(consumer, kPreferMappableSharedImage)`；GPU 不可用时 `kDefault`。
- 启动校验：GPU LUID 与 endpoint 一致、非 SwiftShader/WARP；首个 GPU 帧超时则该源退到 CPU 共享内存。

### 2.3 输入（06/07）
- UI 线程构造事件：`ui::MouseEvent(type, loc_dip, root_loc_dip, ts, flags, changed_flags)`、`ui::TouchEvent(..., PointerDetails(kTouch, id, rx, ry, force))`
  （每个变化触点新建事件），交给 `aura::EventInjector().Inject(host, &event)`（它做 root transform）。
- 时间戳：viewer `event.timeStamp` 映射到 `base::TimeTicks`，保持间隔（fling 速度依赖它）。
- 键盘：`ui::KeyEvent` 带 DOM code/key/VKEY + `EF_IS_SYNTHESIZED`（`PreDispatchKeyEvent` 跳过 IME）；文本走 08 的 `InsertText`。
- 06 使 `Env::IsMouseButtonDown()` 回到事件 flags（`EnvInputStateController`），Views 拖拽/capture 正常。

### 2.4 节奏（10）
- `PrtsFramePacer` 每浏览器根一个，严格一个未 ack 的 `IssueExternalBeginFrame`；args：单调 frame_time、递增 sequence、`deadline = now + 0.8·interval`。

### 2.5 远程软键盘（08）
- `IME_STATE{window, focus_gen, editable, type, mode, flags, show_request(show|hide|none), caret_rect_root_dip, focus_reason}`。
- 触发：`RemoteInputMethod` 的 `SetFocusedTextInputClient/DetachTextInputClient/OnTextInputTypeChanged/OnCaretBoundsChanged` 与其 VK controller 的
  Display/Dismiss（content 的 `VirtualKeyboardControllerWin` 已实现“触摸/笔聚焦 + policy”决策）。viewer 关闭键盘 → controller 通知 `OnKeyboardHidden()`。
- caret：`client->GetCaretBounds()`（屏幕 DIP）→ `wm::ConvertRectFromScreen(host->window(), &r)`。

### 2.6 品牌（12/13）
- 期望（构建后实测）：`Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/157.0.0.0 Safari/537.36`；
  `Sec-CH-UA` 含 `"Google Chrome";v="157"` 与 `"Chromium";v="157"`，GREASE/顺序由上游算法生成。
- 建议构建参数 `proprietary_codecs=true`、`ffmpeg_branding="Chrome"`（`chromium/args/*.gn` 归 Stage 2 维护）。

## 3. 已评估但不采用的补丁点
- `content/public` 新增合成输入 API / RWH 注入：到不了浏览器 UI（c1 §5）。
- 为测试串行化输入的屏障：增加队头延迟；测试改为 viewer 侧 rVFC 观察结果。
- `ui/base/pointer/pointer_device_win.cc` 虚拟触摸屏：被 09 取代。
- 自定义 viz OutputDevice：S12 后备（D4）。
- 移植 `DesktopWindowTreeHostPlatform` / 自定义 PlatformWindow 到 Windows：D2 否决。
- `--user-agent` / `is_chrome_branded=true`：d-branding。

## 4. 补丁维护基础设施（`scripts/cr.ps1`，由 Stage 2 实现）

- 布局：`remotekit/chromium/{PIN.json, series, patches/*.patch, args/{dev,rel}.gn}`；checkout `C:\src\chromium\src`；src 内分支 `upstream`（= PIN）、
  `prts`（= upstream + series），操作前自动建备份分支。
- 命令：`init`（unmanaged `.gclient`，按 tag 浅同步）、`apply`（拒绝脏工作区；`git am --3way` 按 series）、`export`（按 `Patch-Name:` trailer 生成稳定文件名，
  只写真正变化的文件）、`check`（临时 worktree 试应用）、`stat`（每补丁上游文件/行数，超阈值警告：单钩子 > 30 行、上游总量 > 400 行）、
  `rebase <tag>`（只在 Stable 里程碑升级时用）、`gen/build`（构建锁、日志与 `.exit` 文件）。
- 规则：每个维护单元一个提交（`--fixup` + autosquash 折回）；remotekit 只提交 series/patches/PIN/args；补丁头写明目的、上游文件、守卫开关、设计章节、上游化可能性；
  里程碑升级只跑 `cr check` + 构建 + 冒烟。
- `endpoint/` 通过 junction 进入 src，不进补丁；`cr apply` 需确保 junction 存在。
