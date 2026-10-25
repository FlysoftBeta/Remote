# 02 浏览器补丁清单与补丁维护基础设施

基线：Chromium Stable **157.0.8103.15** / `1aa07b638799e59656559ae6c6649d4b197f555e`。
原则：
1. **新代码放新文件**：PRTS 逻辑全部在 `chrome/browser/prts/`（浏览器层）和少量 `ui/base/ime/remote_input_method.*`（因 ui 层不能依赖 chrome/）。
2. **上游文件只放钩子**：每处钩子 ≤ 30 行，标注 `// PRTS:`，并由一个运行期开关守卫：`ui::IsPrtsRemoteMode()`
   （新增于 `ui/base/ui_base_switches.{h,cc}`：`--prts-remote`，Views/Aura/content 都能依赖 ui/base）。没有该开关时行为与上游一致。
3. Views/Aura 不依赖 chrome/；content 不新增 public API（prior art 的 `content/public/browser/synthetic_input.h`、`aura_input_barrier` 不再引入）。
4. 可上游化的修复（viz capturer）独立成补丁、带单测。
5. 每个补丁 = 一个连贯的维护单元（prior art 的 36 个补丁合并为下表约 13 个）。

## 1. 补丁系列（按应用顺序）

| # | 名称 | 上游文件（钩子） | 新文件 | 估计规模 | 来源 / 证据 |
|---|---|---|---|---|---|
| 01 | `prts-switch` | `ui/base/ui_base_switches.{h,cc}`（`kPrtsRemote` + `IsPrtsRemoteMode()`） | — | +15 | 新 |
| 02 | `prts-bootstrap` | `chrome/browser/BUILD.gn`（引入 `prts_sources.gni`）、`chrome/browser/headless/chrome_browser_main_extra_parts_headless.{h,cc}`（启动/关闭控制器）、`chrome/app/chrome_main.cc`（若需早期继承句柄清理） | `chrome/browser/prts/{prts_controller,prts_pipe_win,prts_protocol,prts_switches}.*`, `prts_sources.gni` | 上游 +30；新 ~1200 | prior art 0006/0007/0016/0033 合并（有界读写线程、`CancelSynchronousIo`、`BLOCK_SHUTDOWN`） |
| 03 | `prts-registry` | 无（使用 `BrowserList`/`TabStripModelObserver`/`aura::EnvObserver::OnHostInitialized` + `aura::WindowTreeHostObserver`） | `prts_window_registry.*`, `prts_host_tracker.*`, `prts_tab_registry.*` | 新 ~1500 | prior art 0001-0005/0020/0024；拆分其 1337 行注册表 |
| 04 | `viz-capturer-complete-capture` | `components/viz/service/frame_sinks/video_capture/frame_sink_video_capturer_impl.cc`（`MaybeDeliverFrame` 失败/版本变化路径调用 `oracle_->CompleteCapture(..., false, ...)`）+ unittest | — | +20/-5 + test | prior art 0013；b2 §3 证实 pin 上仍有此 bug |
| 05 | `prts-capture` | 无（`HostFrameSinkManager::CreateVideoCapturer`、`WindowTreeHost::CreateVideoCaptureLock` 都是现有 API） | `prts_capture_source.*`, `prts_buffer_export_win.*`（零拷贝：转发 GMB NT handle + `DXGIHandleToken`；保底：key0/1 导出池拷贝） | 新 ~900 | prior art 0010-0012/0014/0015/0017-0019/0021/0026/0027 合并 |
| 06 | `aura-views-remote-input` | `ui/aura/input_state_lookup_win.cc`（remote 模式下 `InputStateLookup::Create()` 返回 null，按钮状态改由事件 flags 维护）；`ui/views/win/hwnd_message_handler_headless.{h,cc}`（remote 模式下 `HasCapture()` 返回逻辑 capture 状态，`SetCapture/ReleaseCapture` 记录逻辑状态）；`ui/views/widget/desktop_aura/desktop_window_tree_host_win.cc`（remote 模式下 `SetCursorNative`→上报而非 `::SetCursor`；`MoveCursorToScreenLocationInPixels`/`UpdateMouseLockRegion`/`OnCursorVisibilityChangedNative`/`CaptureSystemKeyEventsImpl`/`StartTouchDrag` 变 no-op）；`ui/views/widget/desktop_aura/desktop_drag_drop_client_win.cc`（不进 `::DoDragDrop`，取消）；`ui/display/win/screen_win_headless.{h,cc}`（`SetCursorScreenPointForTesting` 增加非测试别名 `SetVirtualCursorScreenPoint`） | `ui/views/widget/desktop_aura/remote_cursor_sink.h`（Views 层观察者接口，chrome 实现） | 上游 ~+120 | c1 §2/§3；prior art 0022/0023 中被证明必要的部分 |
| 07 | `prts-input` | 无 | `prts_input.*`（鼠标/滚轮/键盘/触摸 → `ui::Event`，按根 DIP→像素 root transform 一次，`SendEventToSink`）、`prts_input_routing.*`（逻辑 capture 优先 → 弹层 z-order → 浏览器根；键盘根规则；按住状态与 reset）、`prts_touch.*`（10 触点池、源时钟映射、cancel） | 新 ~1100 | prior art 0023/0035/0036（去掉 content 屏障）；c1 §1/§4 |
| 08 | `remote-ime` | `ui/base/ime/init/input_method_factory.cc`（headless 分支前：remote 模式→`RemoteInputMethod`）；`ui/base/ime/BUILD.gn`；`ui/base/ime/text_input_client.h`（新增两个带默认实现的虚函数 `GetTextInputAction()`、`GetVirtualKeyboardPolicy()`）；`content/browser/renderer_host/render_widget_host_view_aura.{h,cc}`（覆写上述两函数读 `TextInputState.action/vk_policy`；`OnUpdateTextInputStateCalled` 在 action/vk_policy/show_ime_if_needed 变化时也通知 input method） | `ui/base/ime/remote_input_method.{h,cc}`（派生自 `MockInputMethod` 语义：无 OS IME；补 null/detach 通知；自带 `RemoteVirtualKeyboardController` 转发 show/hide，无 OS 调用）、`chrome/browser/prts/prts_ime_bridge.*`（每根注册 `InputMethodObserver`，生成 `IME_STATE`，执行 `InsertText`/`SetCompositionText`） | 上游 ~+80；新 ~600 | c2 全文；headless 本已用 `MockInputMethod`（不会弹 TabTip/InputPane） |
| 09 | `virtual-touch-capability` | `ui/base/pointer/pointer_device_win.cc`（remote 模式且 `--prts-touch-points=N>0` 时 `MaxTouchPoints()`、`GetTouchScreensAvailability()`、`GetAvailablePointerAndHoverTypesImpl()` 返回虚拟触摸屏） | — | +30 | c1 §4（无触摸屏时 maxTouchPoints=0） |
| 10 | `external-begin-frame` | `ui/views/widget/desktop_aura/desktop_window_tree_host_win.cc`（`Init`：remote 模式且 `--prts-external-begin-frame` 时 `CreateCompositor(force_sw, true)`，并 `set_wait_for_all_frame_sinks(false)`） | `chrome/browser/prts/prts_frame_pacer.*` | 上游 ~+8；新 ~200 | b3 §2/§5；CEF OSR 同样用 `IssueExternalBeginFrame` |
| 11 | `headless-geometry` | 视 spike S6 结果：`ui/display/win/screen_win_headless.cc`（虚拟显示器/DSF）、`ui/aura/window_tree_host.cc`（root 尺寸）、`content/browser/renderer_host/render_widget_host_view_aura.cc`（DOM outer size 与逻辑根一致）、`ui/views/controls/menu/menu_controller.cc`（弹层定位） | — | 未知（prior art 0021/0025/0028 合计 ~+300/-200，需先在 M157 复验） | prior art；风险项 |
| 12 | `prts-branding` | `chrome/app/theme/chromium/BRANDING`（4 个 PRODUCT 值）、`chrome/app/chromium_strings.grd`、`chrome/app/settings_chromium_strings.grdp`（只改用户可见产品名，保留 Chromium 版权/作者） | — | ~+250/-250（字符串） | prior art 0029/0030/0032；`d-branding.md` |
| 13 | `chrome-web-identity` | `components/embedder_support/user_agent_utils.cc`（`GetUserAgentBrandList`：`#if BUILDFLAG(IS_WIN) brand = "Google Chrome"`；`GetUserAgentInternal`：`#if !BUILDFLAG(IS_WIN)` 包住 `Headless` 前缀）+ 调整 `user_agent_utils_unittest.cc:302`、`headless_mode_browsertest.cc:328` 的 Windows 期望 | — | +8/-2 + tests | prior art 0029/0031；`d-branding.md`（pin 上 `user_agent_utils.cc:162`、`:449` 已核对） |

不做补丁、用启动参数解决的：`--headless=new`、`--wm-window-animations-disabled`（prior art 0034：非 component 构建 select 关闭时 UAF）、
`--disable-features=UiGpuRasterization`（冷 shader 导致弹层延迟 100 ms+）、`--disable-legacy-window`（不创建 `LegacyRenderWidgetHostHWND` 子窗口）、
`--user-data-dir=<per instance>`、`--no-first-run --no-default-browser-check`、`--disable-print-preview`（打印默认关）。
外部协议处理（prior art 0022 改了 `external_protocol_handler.cc`）改为 PRTS 控制器在 `ChromeContentBrowserClient` 现有策略/policy 层拒绝，避免上游改动；若无可用钩子再加 ≤10 行补丁。

**上游文件总改动估计**：约 20 个上游文件、~600 行（不含字符串与测试）；新文件 ~5500 行，几乎全部在 `chrome/browser/prts/`。
对比 prior art：36 补丁、content public API 新增、1071 行的输入补丁。

## 2. 关键补丁细节

### 2.1 启动与 IPC（02）
- `--prts-pipe=<cmd>,<evt>,<frm>`（十进制句柄值）。浏览器启动早期 `SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0)`，防止子进程继承。
- 控制器生命周期挂在 `ChromeBrowserMainExtraPartsHeadless`（unified headless 已有的 extra parts），所有对 Chrome 对象的操作 post 到 UI 线程。
- 协议：`HELLO{version, capabilities, gpu_luid, vendor/device, swiftshader}` → `HELLO_ACK{options}`；窗口/标签生命周期；`FRAME`/`FRAME_RELEASE`/`BUFFER_GONE`；
  `INPUT_MOUSE/WHEEL/KEY/TEXT/COMPOSITION/TOUCH`、`FOCUS`、`RESET`；`IME_STATE`、`CURSOR`；`PACE_CONFIG/PACE_TICK`；`REFRESH`。
  帧头：LE u32 长度 + magic + 版本 + 类型 + 请求 id（沿用 prior art 的有界 envelope：最大 1 MiB，队列 1024 条/8 MiB）。

### 2.2 窗口与弹层跟踪（03）
- 浏览器窗口：`BrowserList` 观察；`BrowserView::GetWidget()->GetNativeWindow()->GetHost()`。
- 弹层：`aura::Env::AddObserver` → `OnHostInitialized(host)`，再等待 `WindowTreeHostObserver` 的可见/首帧条件才视为 ready（prior art：创建回调太早）；
  通过 transient parent / `views::Widget::parent()` / owner 关系归属到浏览器窗口；虚拟 z-order 按显示顺序维护，不依赖创建顺序。

### 2.3 捕获（05）
- 见 03 §2。GPU：`kPreferMappableSharedImage`；描述符带 `DXGIHandleToken`（`ui/gfx/gpu_memory_buffer_handle.h` 中 `DXGIHandle::token()`：同一底层资源的克隆句柄 token 相同），
  首次出现时才附带句柄；池重建时 `BUFFER_GONE`。软件：只读 section。
- 启动校验：GPU LUID 与 endpoint 一致、非 SwiftShader/WARP、共享 keyed 纹理探测成功；首个 GPU 帧 5 s 超时则该源退到 CPU（prior art）。

### 2.4 输入钩子（06/07）
- 事件构造统一在 UI 线程：`ui::MouseEvent(type, loc, root_loc, ts, flags, changed_flags)`，loc 为根 DIP 经 `host->GetRootTransform()` 正变换后的像素；
  `ui::TouchEvent(type, loc, root_loc, ts, ui::PointerDetails(kTouch, id, rx, ry, force))`，每个变化触点新建事件（自动分配 unique event id），
  触点 id 映射到 0-9；时间戳由 viewer `event.timeStamp` 映射到 `base::TimeTicks` 保持间隔（prior art 0036：否则 fling 速度丢失）。
- 键盘：`ui::KeyEvent` 带 DOM code/key/VKEY，`EF_IS_SYNTHESIZED` 使 `WindowEventDispatcher::PreDispatchKeyEvent` 跳过 IME；文本走 08 的 `InsertText`。
- 由于 06 把 `InputStateLookup` 置空，`aura::Env::IsMouseButtonDown()` 改用事件 flags（`EnvInputStateController` 已维护），Views 拖拽/capture 正常。
- `--disable-legacy-window` 避免向真实子 HWND `ShowWindow`。

### 2.5 节奏（10）
- 只在 remote 模式 + 开关下改变 compositor 创建参数；`PrtsFramePacer` 每根一个，严格一个未 ack 的 `IssueExternalBeginFrame`。

### 2.6 可编辑字段 → 远程软键盘（08）
- `IME_STATE{window_id, focus_gen, editable, type, mode, action, flags, vk_policy, show_request(show|hide|none), caret_rect_root_dip, focus_reason}`。
- 触发：`RemoteInputMethod` 的 `SetFocusedTextInputClient/DetachTextInputClient/OnTextInputTypeChanged/OnCaretBoundsChanged/SetVirtualKeyboardVisibilityIfEnabled`
  与其 `RemoteVirtualKeyboardController::Display/Dismiss`。content 的 `VirtualKeyboardControllerWin`（`content/browser/renderer_host/virtual_keyboard_controller_win.cc`）
  已实现“触摸/笔聚焦 + auto policy + show_ime_if_needed → show；manual policy → show/hide”的策略，我们直接复用它的决策，只换掉出口。
- Views 文本框（omnibox）：`Textfield::RequestFocusForGesture` 在触摸时调用 `ShowVirtualKeyboardIfEnabled` → 同一出口。
- caret：`client->GetCaretBounds()`（屏幕 DIP）→ `wm::ConvertRectFromScreen(host->window(), &r)`。
- viewer 关闭键盘 → `IME_VK_HIDDEN` → `RemoteVirtualKeyboardController` 通知观察者 `OnKeyboardHidden()`，保持 content 的 shown 标志一致。

### 2.7 品牌（12/13）
- 期望（源推导，待构建后实测）：`Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/157.0.0.0 Safari/537.36`；
  `Sec-CH-UA: "Not-A.Brand";v="99", "Google Chrome";v="157", "Chromium";v="157"`（GREASE/顺序由上游算法按主版本生成，不硬编码）。
- `chrome://version` 应用名 PRTS、UA 为 Chrome；exe 仍为 `chrome.exe`，默认 profile 路径仍为 Chromium（我们总是显式 `--user-data-dir`）。
- 不改：`is_chrome_branded`、`version_info::GetProductName()` 消费者、installer/注册表身份（便携部署，无安装器）。

## 3. 已评估但不采用的补丁点
- `content/public/browser/synthetic_input.h` + RWH 注入（prior art 0008/0009）：到不了浏览器 UI。
- `content::DispatchWithAuraInputBarrier`（prior art 0023）：为验收测试串行化输入，增加队头延迟；改为 endpoint 侧测试用 rVFC 观察结果。
- omnibox 几何诊断 RPC（prior art 0028 的诊断部分）：测试专用，不进生产。
- 自定义 viz OutputDevice（b1 §5）：仅作为 R1 风险的后备 spike。
- `--user-agent` / `is_chrome_branded=true`：见 `d-branding.md`。

## 4. 补丁维护基础设施（`scripts/cr.ps1`，基于 prior art 改进）

### 4.1 布局
```text
remotekit/chromium/
  PIN.json        {"version":"157.0.8103.15","commit":"1aa07b63…","ref":"refs/tags/157.0.8103.15","depot_tools":"<rev>","updated":"…"}
  series          有序补丁文件名列表（唯一的顺序来源）
  patches/*.patch 文件名 = 稳定 slug（如 08-remote-ime.patch），来自提交信息里的 `Patch-Name:` trailer，不随顺序变化
  args/dev.gn     component, is_debug=false, dcheck_always_on=true, symbol_level=1, blink/v8_symbol_level=0
  args/rel.gn     non-component, is_official_build=false, dcheck off, symbol_level=0, enable_nacl=false, proprietary_codecs=false
checkout: C:\src\chromium\src  (CR_ROOT 可改)；depot_tools: C:\src\depot_tools
```
src 内分支：`upstream` = PIN；`prts` = upstream + series；备份分支 `prts-backup/<timestamp>`。

### 4.2 命令
| 命令 | 行为 |
|---|---|
| `cr init` | 写 `.gclient`（unmanaged src），`gclient sync --revision src@<ref> --no-history --shallow -D`，建 `upstream`，`cr apply` |
| `cr apply` | **拒绝**在有未提交修改时执行；先建备份分支；`git checkout -B prts upstream`；按 `series` 逐个 `git am --3way --keep-cr`；失败时打印补丁名并停在 am 状态 |
| `cr export` | 从 `upstream..prts` 逐提交生成补丁，文件名取 `Patch-Name:` trailer；重写 `series`；只改变内容真正变化的文件（diff 比较后写入），避免无意义 churn |
| `cr check` | 在临时 `git worktree`（基于 upstream）里无副作用地应用 series，报告哪些补丁冲突；不动工作分支 |
| `cr stat` | 统计每个补丁触及的上游文件与行数（区分新文件/上游文件），超过阈值（单钩子 > 30 行、上游总量 > 800 行）时警告 |
| `cr rebase <tag>` | `git fetch --depth=1 origin tag <tag>`；`git rebase --onto <newpin> upstream prts`（逐补丁解决冲突）；`gclient sync` 到新 pin；更新 PIN.json；`cr export`；只在 Stable 里程碑升级时用 |
| `cr gen <dev|rel>` / `cr build <dev|rel> [target]` | `gn gen out\<cfg>`（args 从 `args/<cfg>.gn` 复制，若已存在则比较并提示差异）；`autoninja -C out\<cfg> chrome`；构建锁文件防止并发 ninja；日志与 `.exit` 文件写入 `out\<cfg>\prts-build-<ts>.log`（prior art 教训：stderr 状态行在 PowerShell Stop 模式下被误判，用 `cmd /d /c` 重定向） |
| `cr run` | 以开发参数启动 `out\<cfg>\chrome.exe --headless=new --prts-remote …`（无 endpoint 时仅调试用） |

### 4.3 规则
- 提交即补丁：在 `prts` 分支上每个维护单元一个提交（用 `git commit --fixup` + `rebase --autosquash` 把修改折叠回所属单元）。
- remotekit 仓库只提交 `series`/`patches`/`PIN.json`/`args`，从不提交 Chromium 源。
- 每个补丁头部写明：目的、触及的上游文件、守卫开关、对应设计文档章节、上游化可能性。
- 里程碑升级前后运行最小的针对性测试（`cr check` + 构建 + 03 §5 的冒烟），不跑完整测试套件。
