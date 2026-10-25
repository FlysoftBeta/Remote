# r-chromium-map: Windows new-headless 架构地图（clean-room 复核）

Pin：157.0.8103.15 / `1aa07b63`。来源：`C:\src\chromium\src`（完整检出，只读）与 `C:\src\cr-partial`。
标记：[V] 本次在 pin 上亲自读过；[R] 引用 Stage-1 Chromium 侧报告（b1/b2/b3/c1/c2/d），本次抽查过关键行；[I] 推断。
本文件**不**参考 prior art（`C:\Users\Flysoft\Desktop\chromium`、`research/a-prior-art.md`）。

## 1. 浏览器进程窗口层

```text
BrowserView ─ views::Widget ─ DesktopNativeWidgetAura ─ DesktopWindowTreeHostWin(aura::WindowTreeHost)
                                   │                            └─ HWNDMessageHandlerHeadless（--headless 时）
                                   ├─ content_window_ (aura::Window, delegate=DNWA)
                                   ├─ wm::FocusController(DesktopFocusRules) = FocusClient + wm::ActivationClient
                                   ├─ DesktopCaptureClient（文件静态 active_client_）
                                   └─ WindowParentingClient::GetDefaultParent
```

- [V] `ui/views/win/hwnd_message_handler_headless.cc:383-406`：`Activate()` 只在 `!is_active_` 时置位并回调
  `HandleActivationChanged(true)`；**不会让同进程的其他 headless 窗口失活**（真实 Windows 由 OS 发 WM_ACTIVATE 做互斥）。
  → 同一 instance 内多个根可能同时“active”，激活互斥必须由我们的控制器维护。
- [R] 同文件 `Init` 创建真实 HWND 并 `DWMWA_CLOAK`；`SetCapture/ReleaseCapture` no-op、`HasCapture()==false`（c1 §3）。
- [V] `ui/views/BUILD.gn:985-995`：`DesktopWindowTreeHostPlatform`（Ozone 用的 PlatformWindow 宿主）只在 `is_linux || is_fuchsia` 编译。
  → “在 Windows 上做一个自定义 `ui::PlatformWindow`/headless-ozone 式 shim”意味着把这条宿主移植到 Windows 并绕开
  `DesktopWindowTreeHostWin`/`HWNDMessageHandlerHeadless`，即离开 brief 要求的 new-headless 路径。
- [V] `chrome/browser/ui/views/chrome_views_delegate_win.cc:123-160` `ChromeViewsDelegate::CreateNativeWidget`：
  有 parent 的非 CONTROL/WINDOW 类型 widget（菜单、气泡、tooltip 等）在 Windows 上一律 `kDesktopNativeWidgetAura`
  （新的顶层根）；`shadow_elevation` 情况强制 `kNativeWidgetAura`（说明 Views 在 Windows 上支持根内弹层）。
- [V] `ui/views/widget/desktop_aura/desktop_native_widget_aura.cc:252-276` `GetDefaultParent`：
  `WINDOW_TYPE_MENU`（content 的 `<select>` 弹层等）或全屏窗口 → `DesktopNativeWidgetTopLevelHandler::CreateParentWindow`
  （新顶层根）；**Fuchsia 下整段关闭、直接返回 `root_window_`**——即“弹层留在宿主根内”是上游已有的平台变体。
- [V] `chrome/browser/headless/headless_mode_init.cc:60-115`：Windows 上 headless 只补 incognito/no-error-dialogs/no-first-run/
  临时 user-data-dir；不禁 GPU（Linux 分支才强制 ozone headless 与 SwiftShader WebGL）。
- [V] `components/headless/screen_info/README.md` + `chrome/browser/headless/test/headless_mode_browsertest_win.cc:237`：
  `--screen-info={WxH devicePixelRatio=… workArea…}` 在 Windows new-headless 上可用（虚拟屏幕尺寸/DSF/工作区）。

## 2. 合成与输出（ui::Compositor → viz）

- [R] 根 compositor frame sink：`RootCompositorFrameSinkImpl::Create` → `OutputSurfaceProviderImpl` → GPU：`SkiaOutputSurfaceImpl`，
  HWND 非空 → onscreen；有 DComp → `SkiaOutputDeviceDComp` + `DCompPresenter`（子 HWND），否则 `SkiaOutputDeviceGL`/Dawn；
  软件：headless 优先返回内存 `SoftwareOutputDevice`（b1 §1-2）。cloak 不影响 Present；`--headless` 关闭 native occlusion（b1 §3）。
- [R] BeginFrameSource：external controller → `ExternalBeginFrameSourceMojo`；`--disable-frame-rate-limit` → BackToBack；
  DComp → `ExternalBeginFrameSourceWin`（`VSyncThreadWin`，失败退化 Sleep）；否则 `DelayBasedBeginFrameSource`（b3 §1）。
  子 sink（renderer/OOPIF）继承根的源（b3 §4）。`DesktopWindowTreeHostWin::Init` 以 `CreateCompositor(force_sw)` 创建，未开 external control（b3 §2）。
- [R] `FrameSinkVideoCapturerImpl`：损伤在 `SurfaceAggregator::PrewalkSurface` 中通知，捕获请求在同一次 `DrawAndSwap` 执行（b2 §1）；
  `kPreferMappableSharedImage` 在 Windows 产出 D3D11 `SHARED_NTHANDLE|SHARED_KEYEDMUTEX`（key 0）纹理，ARGB 与 NV12（b2 §2）。
- [V] **更正 Stage-1 风险 R1**：`components/viz/service/display_embedder/skia_output_surface_impl_on_gpu.cc:1068-1095`
  `CopyOutputRGBAInTexture`：mappable blit 目标时 `should_wait_for_gpu_work` 只是把结果投递推迟到 GPU 完成——注册 Skia finished-proc
  （`ReadbackContextTexture::OnMailboxReady`）并计入 `num_readbacks_pending_`，由 `ScheduleCheckReadbackCompletion`（2 ms 轮询）推动；
  **GPU 主线程不阻塞等待**。代价是投递延迟 ≈ 该帧 GPU 完成时间 + ≤2 ms 轮询粒度，而不是拖慢浏览器合成。
- [R] 已知 bug：`FrameSinkVideoCapturerImpl::MaybeDeliverFrame`（:1524）失败/版本变化路径不调用 `oracle_->CompleteCapture` → 静态页 passive refresh 卡死（b2 §3）。

## 3. 事件路由

- [R] `EventSource::SendEventToSink`（经 rewriter）→ `WindowEventDispatcher`（`EventProcessor`）→ `WindowTargeter`
  （键盘按 FocusClient；located 按 capture/pressed/touch-lock/hit-test）→ `DesktopNativeWidgetAura`→Views，或
  `RenderWidgetHostViewAura` → `input::RenderWidgetHostInputEventRouter`（Viz hit-test，OOPIF）（c1 §1）。
- [V] `ui/aura/event_injector.h:20-31` / `.cc`：`aura::EventInjector::Inject(WindowTreeHost*, ui::Event*)` 已有，located 事件先
  `UpdateForRootTransform(GetRootTransform(), GetRootTransformForLocalEventCoordinates())` 再送 sink——正是我们需要的 DIP→host 像素变换，
  不必自写变换代码。
- [R] Windows 陷阱：`InputStateLookupWin` 读 `::GetKeyState`；headless `HasCapture()==false` 使 Views 拖拽失效；
  `SetCursorNative/MoveCursorToScreenLocationInPixels/UpdateMouseLockRegion/CaptureSystemKeyEventsImpl/StartTouchDrag/DoDragDrop`
  仍调用全局 Win32 API（c1 §2-3）。

## 4. 输入法与软键盘

- [V] `ui/base/ime/init/input_method_factory.cc:50-53`：`--headless` → `MockInputMethod`（无 TSF，不会弹 OS 触摸键盘）。
- [V] `ui/base/ime/mock_input_method.cc`：`SetFocusedTextInputClient(client)` 仅在 client 非空时 `OnTextInputTypeChanged`
  （→ 观察者 `OnTextInputStateChanged`）；`DetachTextInputClient` **不通知**；`SetVirtualKeyboardVisibilityIfEnabled`
  通知 `OnVirtualKeyboardVisibilityChangedIfEnabled`；`GetVirtualKeyboardController()` 返回内置 stub。
- [V] `content/browser/renderer_host/render_widget_host_view_aura.cc:3711-3735`：Windows 上 content 不走
  `show_ime_if_needed`→`SetVirtualKeyboardVisibilityIfEnabled`，而是创建 `VirtualKeyboardControllerWin(this, GetInputMethod())`
  并 `UpdateTextInputState(state)`（它读 vk_policy / show_ime_if_needed / last_vk_visibility_request，经
  `InputMethod::GetVirtualKeyboardController()` 显示/隐藏）。→ 换掉 InputMethod 的 VK controller 即可拿到 content 的“该弹/该收”决策。
- [V] `ui/base/ime/text_input_client.h`：没有 vk_policy / enterkeyhint 访问器（只有 `GetFocusReason()` 等）。

## 5. 触摸能力与 Web 偏好

- [V] `content/browser/web_contents/web_contents_impl.cc:4001` 先 `SlowWebPreferenceCache::Load(&prefs)`（含 `ui::MaxTouchPoints()`），
  `:4252` 再调用 `ContentBrowserClient::OverrideWebPreferences`；`chrome/browser/chrome_content_browser_client.cc:5014` 依次调用每个
  `ChromeContentBrowserClientParts::OverrideWebPreferences`（`chrome_content_browser_client_parts.h:61`）。
- [V] `third_party/blink/public/common/web_preferences/web_preferences.h:129-132`：`touch_event_feature_detection_enabled`、
  `pointer_events_max_touch_points`、`available_pointer_types`、`primary_pointer_type`（及 hover 类型）都是可覆写的 prefs。
  → 虚拟触摸能力可以在 chrome 层按 instance 注入，无需改 `ui/base/pointer/pointer_device_win.cc`。

## 6. 品牌

- [V] `components/embedder_support/user_agent_utils.cc:162-163`（`#if !BUILDFLAG(CHROMIUM_BRANDING) brand = GetProductName()`）与
  `:449-450`（`--headless` → `product.insert(0, "Headless")`）是 UA-CH 品牌与 UA 字符串的唯一两处决定点（d-branding 的结论成立）。

## 7. 同树参照：Chrome Remote Desktop host（`remoting/`）

- [V] `remoting/protocol/chromium_socket_factory.*`、`chromium_port_allocator_factory.*`：在 Chromium 树内用 libwebrtc（PeerConnection）
  + Chromium 网络栈的原生主机。
- [V] `remoting/codec/webrtc_video_encoder_gpu.cc`：Windows 上 H.264 经 `media::VideoEncodeAccelerator`（MF），NV12 输入。
- [V] `remoting/protocol/webrtc_video_encoder_wrapper.cc:412-413`：`SetPlayoutDelay(VideoPlayoutDelay::Minimal())`、`VideoContentType::SCREENSHARE`。
  → “Chromium 树内 C++ 可执行程序 + libwebrtc + 硬件 H.264 + 最小播放延迟”是上游自己在 Windows 上维护的组合。
