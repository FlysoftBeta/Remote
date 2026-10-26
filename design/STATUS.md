# STATUS（阶段交接）

更新：2026-10-25，Stage 1（调研与设计）orchestrator。下一阶段：Stage 2（实现，从 M0/M1 开始）。

## 1. 已完成
- 调研（`research/`，全部以 Chromium Stable **157.0.8103.15 / `1aa07b638799e59656559ae6c6649d4b197f555e`** 为基线，关键结论已在 `C:\src\cr-partial` 抽查核对）：
  | 文件 | 内容 | 状态 |
  |---|---|---|
  | `a-prior-art.md` | 用户早期项目 `C:\Users\Flysoft\Desktop\chromium`（36 补丁、prtsd Rust、server Node）全面评估 | 可靠 |
  | `b1-frame-path.md` | headless 根 compositor 输出路径（GPU DComp / headless 软件 `SoftwareOutputDevice`）、occlusion 在 `--headless` 下关闭、现有捕获钩子 | 可靠（已抽查） |
  | `b2-capture.md` | `FrameSinkVideoCapturer`：同一次 DrawAndSwap 执行、ARGB/NV12 GPU 输出、D3D11 keyed mutex key 0、CompleteCapture bug 仍在、CEF 同法 | 可靠 |
  | `b3-pacing.md` | BeginFrameSource 选择、external BeginFrame 只需 `DesktopWindowTreeHostWin::Init` 改一处 | 可靠（已抽查） |
  | `c1-input-layer.md` | `SendEventToSink` 注入路径、需虚拟化的项（`InputStateLookupWin`、逻辑 capture、光标 API…）、touch、替代方案否决 | 可靠（已抽查） |
  | `c2-ime-osk.md` | headless 用 `MockInputMethod`；可编辑状态观察点；VK 控制流；`RemoteInputMethod` 方案 | 可靠（已抽查） |
  | `d-branding.md` | UA/UA-CH 两处补丁、BRANDING/GRIT、期望值 | 可靠 |
  | `e-video.md` | 接收端延迟控制（Chrome 89）、发送栈比较、oneVPL/UHD 630 参数 | 前半可靠，后半未写完；Level 结论被 `g-h264-level.md` 纠正 |
  | `e2-vpl-zero-copy.md` | prior art oneVPL bridge 拆解、拷贝计数、零拷贝序列 | 可靠 |
  | `g-h264-level.md` | Chrome answer 恒为 Level 3.1 且不强制 → SPS 写真实 level | 源码证据 + 推断，S8 验证 |
  | `cr-partial.md` | 稀疏部分克隆说明 | 可靠 |
  | `b-headless-frames.md`、`c-input-focus.md` | 上一轮被中断的残稿 | 已标记 SUPERSEDED |
- 设计（`design/`）：`01-architecture.md`（决策 D1-D13）、`02-browser-patches.md`（13 个补丁 + `cr.ps1` 设计）、`03-video-pipeline.md`（管线、节奏、测量、E1-E14）、
  `04-isolation.md`（短版）、`05-plan.md`（M0-M6、spike S1-S9、检出/构建计划）、`ops.md`（Codex 编排工具）。
- 工具：`scripts/codex-run.ps1`（修复并发写日志崩溃）、`scripts/codex-task.sh`（有界任务）、`scripts/codex-limits.ps1`。

## 2. 核心决策速览
1. 基线 Stable 157.0.8103.15；补丁按维护单元组织，全部运行期由 `--prts-remote` 守卫。
2. Chrome `--headless=new` + Aura/Views + cloaked HWND；PRTS 逻辑在 `chrome/browser/prts/`。
3. 帧导出 = 每个 Aura 根一个 Viz `FrameSinkVideoCapturer`（GPU：D3D11 NT handle + keyed mutex；软件：只读共享内存）；不替换 OutputDevice。
4. 弹层 = 独立根，endpoint 合成。
5. 交接目标零额外拷贝（按 `DXGIHandleToken` 缓存打开的 Viz 池纹理，VPP 直接写编码器输入），保底 prior art 双拷贝。
6. 节奏：先 vsync + capture 节流；再 external BeginFrame（endpoint 驱动）。
7. 输入 = `ui::Event` → `WindowTreeHost::SendEventToSink`；否决 DevTools/RWH/SendInput。
8. 可编辑字段 = `RemoteInputMethod`（替换 headless 的 `MockInputMethod`）+ `InputMethodObserver` → `IME_STATE` → viewer 软键盘。
9. 品牌：UA/UA-CH 两处补丁 + BRANDING/GRIT。
10. endpoint：Rust（prior art prtsd 演进）、oneVPL（legacy MSDK runtime）主编码、MF/OpenH264 回退、webrtc-rs GCC/TWCC/pacer、playout-delay (0,0)。

## 3. 东西在哪
- 本仓：`C:\Users\Flysoft\Desktop\remotekit`（main 分支）。
- Chromium 只读稀疏克隆（grep 用）：`C:\src\cr-partial`（pin 同上；扩展用 `sparse-checkout add`，googlesource 卡住时用 `cr-partial.md` 的镜像方法）。
- 完整 checkout 计划位置：`C:\src\chromium\src`，depot_tools：`C:\src\depot_tools`（**都尚未创建**）。
- prior art（只读，复制使用，不要修改）：`C:\Users\Flysoft\Desktop\chromium`。可复用：`prtsd/`（endpoint 起点）、`server/`（control-plane 起点）、
  `patches/`（参考实现）、`scripts/cr.ps1`（工具起点）、分支 `wip/e1-onevpl`（oneVPL bridge，`git show wip/e1-onevpl:prtsd/native/vpl.cpp`）、`wip/i1-isolation`（隔离 WIP）。
- 工具链：VS 2026 Community + SDK 10.0.28000（pin 需要的版本）已装；cargo 1.99.1、Node v24 已装。
- Codex 余量（19:34 时）：5 小时窗口已用 91%，**23:39 重置**；周窗口 14%。

## 4. Stage 2 的下一步（按顺序）
1. **M0 基础设施**
   1. 按 02 §4 实现 `scripts/cr.ps1`（以 prior art 版本为起点：加 `series`/`Patch-Name` 导出、`check`、`stat`、`rebase`、构建锁、`.exit` 文件）与 `chromium/PIN.json`、`chromium/args/{dev,rel}.gn`。
   2. 克隆 depot_tools，`cr init`（后台运行，1-2 小时网络），然后 `cr gen dev` + `cr build dev chrome`（后台，3-5 小时）。用 completion 通知等待，不要轮询。
   3. 在构建期间并行：把 prior art `prtsd/` → `endpoint/`、`server/` → `control-plane/`（复制，不链接），去掉 v1 协议与 `prts-autopilot` 一类编排残留；
      跑 S7（oneVPL 参数 probe，独立小程序，基于 `wip/e1-onevpl` bridge）与 S8（Chrome 89 接收 1080p，用合成帧源）。
2. **M1 最小链路**：补丁 01→02→03→04→05→06→07→12→13（02 §1），每完成一个就增量构建 + 冒烟；endpoint 新协议 + MF 编码 + WebRTC。
   M0 构建一完成就先做 S1（prior art 补丁在 M157 上的冲突面）、S2（cloaked 根捕获稳定性）、S6（headless 几何）。
3. M1 出口达到后更新本文件，再进入 M2（弹层、触摸、远程软键盘）。

## 5. 未决问题 / 需要实测才能定的
- “50 ms latency” 是 RTT 还是单向：两种都测（01 §5）。
- S3 零拷贝交接是否安全；S4 capturer 的 GPU 等待成本；S5 external BeginFrame 的副作用。
- 外部协议处理能否不改上游文件（02 §1 末尾）。
- M157 上 prior art 的几何补丁（0021/0025/0028）哪些仍需要（S6）。

## 6. 本阶段遇到的问题
- 上一轮尝试被取消，b/c 两份报告不完整；本轮改为 6 个窄问题的 Codex 验证任务全部完成（每个 8-12 分钟）。
- 并发 Codex 启动时 `runs.log` 写冲突导致脚本崩溃，已修复。
- Codex 额度消耗快（6 个高强度任务约 50% 的 5 小时窗口）；最后一个问题（H.264 level）改用 Sonnet 子代理完成。

## M0（Stage 2 收尾，由 Stage 3 orchestrator 补记，2026-10-26）
- Checkout `C:\src\chromium\src` 固定在 157.0.8103.15 / `1aa07b63`（`chromium/PIN.json`），depot_tools 固定版本；`scripts/cr.ps1`（init/apply/export/check/stat/gen/build/run/status）已实际使用：
  `cr build` 写 `out\dev\prts-build-*.log/.exit` 与 `prts-build-last.exit`，`cr export` 生成 `chromium/patches/*.patch` + `series`。
- 首次全量 dev 构建：第一次 9462 s 后单步失败，增量重跑 11887 s 成功（`exit=0 target=chrome`，chrome.exe 02:11）。参数见 `chromium/args/dev.gn`
  （component、dcheck、symbol_level=1、proprietary_codecs、ffmpeg_branding=Chrome、rtc_use_h264；rel.gn 同样带编解码参数）。
- 冒烟（03:33，未打补丁）：`chrome.exe --headless=new --screenshot` 正常出图；UA = `HeadlessChrome/157.0.0.0`，brands = Chromium/Not-A.Brand（补丁 13 前的预期）。
  GPU 进程日志反复出现 `vsync_thread_win_dxgi.cc: Failed to find DXGI output with matching monitor`（cloaked/无匹配显示器 → VSync 退化路径，S2 关注点）。
- 坑：siso 不跟随目录 junction（`src/prts_endpoint` 必须用 `mklink /D` 符号链接）；Git Bash 的 `python` 是 Store 桩，用 depot_tools 的 python3。

## M1（Stage 3，进行中）
- 补丁系列（`chromium/series`）：01 switch、04 viz CompleteCapture（无守卫，可上游化）、11 弹层根内、12 品牌、13 Web 身份、06 aura/views 远程输入、
  08 RemoteInputMethod、02 `chrome/browser/prts/*`（控制器 + Mojo link + 窗口注册 + 捕获 + 输入 + IME 桥）、14 `gn_all` → `//prts_endpoint`。
- S10a 结论：endpoint 与浏览器都是 Mojo broker，endpoint `SendIsolated`（持有 chrome 进程句柄），浏览器 `AcceptIsolated`（`--prts-mojo=<handle>`）；
  NT 句柄经 ipcz driver 由 endpoint 从浏览器进程拉取（`research/` 待补；草稿说明在 `C:\src\tmp\m1\s10-link\NOTES.md`）。
- endpoint（`endpoint/`，树内 C++）：stdio 信令、libwebrtc PeerConnection、OpenH264（M1 编码器）、3 条 negotiated DC、D3D11 帧源（keyed mutex → 私有拷贝 → staging → I420）。
- Codex 草稿与提示词：`C:\src\tmp\m1\*`、`research/prompts5/`。
- 05:20 状态（pacing 暂停到 07:30）：真实链路已通（Hello、StartCapture、viz 出帧、D3D11 导入、OpenH264、WebRTC 到 viewer 解码 1898x1068），
  但只出 1 帧：Codex `m1-debug-frames` 定位为启动时 Network Service 崩溃导致首个导航失败（空文档，无损伤）→ 它在做启动导航恢复的 P0 修复
  （笔记 `C:\src\tmp\m1\debug-frames\NOTES.md`，改动未提交，在 src 工作区）。下一步：审查其改动 → 提交/导出补丁 → 测延迟（`/c/src/tmp/m1/test.sh`）。
- Widevine：Codex 已从 Google 组件更新服务取得 Win x64 CDM 4.10.3112.0（`C:\src\tmp\m1\widevine\`，`STEPS.md`：复制到 `third_party/widevine/cdm/win/x64/`，
  args 加 `enable_widevine/enable_library_cdms/bundle_widevine_cdm=true`、`enable_cdm_host_verification=false`）。尚未应用（需要较大重建）。
- 测试脚本：`/c/src/tmp/m1/{be.sh,test.sh,kill.sh,cp.sh}`；endpoint 参数 `--app`、`--cr-<switch>` 透传 chrome；chrome 日志用 `--cr-enable-logging` → `<profile>\chrome_debug.log`。

## M1 交接（Stage 3 orchestrator，2026-10-26 08:56）——下一位 orchestrator 从这里开始

### 已经能用（真实链路，已验证）
- PRTS chrome（`--headless=new --prts-remote`，由 endpoint 拉起）→ 根 `FrameSinkVideoCapturer`（D3D11 keyed-mutex 共享纹理）→ Mojo（isolated invitation，S10a 通过）
  → `prts_endpoint`（私有拷贝 → staging → I420 → **OpenH264**）→ libwebrtc → viewer。30 fps 连续出帧。
- 延迟（`research/m1/debug-frames.md`；回环、headless Edge 156 viewer、OpenH264、dev 构建）：tab 窗口 capture→display **P50 58.4 / P95 68.5 / P99 74.2 ms**；
  `--app` 窗口 P50 75.8 / P95 87.6 / P99 90.8 ms。注意：页面标记在浏览器 UI 下方，当时用的是任务内的对齐脚本（`C:\src\tmp\m1\debug-frames\run-aligned.mjs`）；
  正式修复见下方 marker-align 任务。input-to-photon 尚未测。
- 输入与弹层（`research/m1/input-validate.md`，截图在 `C:\src\tmp\m1\input-validate\final-*.png`，我抽查过 select 与应用菜单截图）：点击导航、omnibox 输入+回车、
  Ctrl+T/切换/关闭标签、应用菜单、右键菜单、`<select>`（S11：弹层留在根内，无新顶层 HWND）、滚轮、拖选、hover/tooltip、键盘（Shift/Backspace/方向键）**10/10 通过**。
- 品牌：remote 模式 UA = `Chrome/157.0.0.0`（无 Headless），brands = Not-A.Brand / Google Chrome / Chromium v157；菜单里产品名 PRTS。
- Widevine：CDM 4.10.3112.0 已放到 `src/third_party/widevine/cdm/win/x64/`（不入库，见 `design/07-widevine.md`），args 已加入 dev/rel 并重建 out\dev（exit=0）；
  **运行期 EME 验证未做**（`requestMediaKeySystemAccess('com.widevine.alpha')`）。

### 补丁与代码状态（全部已提交）
- src `prts` 分支 = upstream + 9 个提交，已 `cr export`：01 switch、04 viz CompleteCapture（无守卫）、11 弹层根内、12 品牌、13 Web 身份、06 aura/views 远程输入、
  08 RemoteInputMethod、02 `chrome/browser/prts/*`（含启动导航重试 P0）、14 gn_all。src 工作区干净（除 widevine 二进制与 `prts_endpoint` 符号链接）。
- `endpoint/`（`mklink /D src\prts_endpoint` → remotekit\endpoint，siso 不支持 junction）。endpoint 参数：`--chrome= --user-data-dir= --url= [--app] [--synthetic] [--cr-<switch>…]`。
- 工具：`/c/src/tmp/m1/{be.sh（先杀进程再增量构建）,test.sh,cp.sh,kill.sh}`；CP 读 `ENDPOINT_PATH/ENDPOINT_ARGS`；`tools/latency/run.mjs --no-mock`；
  `tools/latency/remote-page.html`；chrome 日志 `--cr-enable-logging` → `<profile>\chrome_debug.log`（endpoint 拉起的 chrome 没有 stderr）。

### 已知问题
1. 启动时 Network Service 必崩一次（无 `--prts-remote` 也崩；`NetworkServiceInProcess2` 不崩）→ 首个导航失败（空页）。目前 P0：`PrtsStartupNavigationRecovery` 重试一次。根因待查（任务 netsvc-crash）。
2. 窗口 1898x1069 而不是 1920x1080（有标题栏与最小化/最大化/关闭按钮，viewer 两侧有黑边）→ remote 模式应最大化/无边框（M2，S6）。
3. 本地化：zh-CN 下仍显示“您的 Chromium”（补丁 12 只改了英文 GRIT，未改 xtb 翻译）。
4. 帧路径每帧 `CreateTexture2D` + CPU staging/I420（M1 临时）；oneVPL + VPP 零拷贝在 M3。GPU 进程刷 `vsync_thread_win_dxgi: Failed to find DXGI output`（无匹配显示器，帧仍是 30 fps）。
5. 光标（remote_cursor_sink 未接到 endpoint/viewer）、IME/软键盘端到端、触摸未测（M2）。Chrome 89 + H.264 + playout-delay(0,0) 未实测。
6. endpoint 用 `#pragma allow_unsafe_buffers` 和去掉 style plugin 编译（M5 再清理）。

### Codex 任务（交接时）
- 已完成并已审：s10-link、m1-small-patches、endpoint-webrtc、endpoint-d3d、capture-registry、input-ime、m1-debug-frames、m1-input-validate、widevine（笔记在 `research/m1/`、草稿在 `C:\src\tmp\m1\*`）。
- **仍在运行**（08:33-08:46 启动，输出未审）：`vpl-encoder`（审查 t1 调参 + oneVPL `webrtc::VideoEncoder` 草稿 → `C:\src\tmp\m1\vpl\`）、
  `netsvc-crash`（崩溃根因，只读 → `C:\src\tmp\m1\netsvc\NOTES.md`）、`marker-align`（viewer 自动找标记行，会改 `viewer/`、`tools/latency/`，未提交 → 需审后提交）。
  提示词在 `research/prompts5/`，日志 `.codex-logs/`。
- `research/t1-encoder-tuning.md` + `tools/encoder-tuning/`（07575f6，自主运行）**我未审**；vpl-encoder 任务的 REVIEW.md 是第一份审查。

### 下一步（建议顺序）
1. 审 marker-align → 用 `tools/latency/run.mjs --no-mock` 正式复测 capture→display 与 input-to-photon（tab 模式），写入本文件；运行期验证 Widevine EME。
2. 审 netsvc-crash 结论，替换 P0 重试为真正修复。
3. M2：remote 窗口最大化/无边框 + `--screen-info` 与 viewer 尺寸（S6）、光标、IME/软键盘端到端、触摸 + `prts-web-prefs`（补丁 09）、zh-CN 品牌字符串。
4. M3：审 vpl-encoder 草稿 → oneVPL 编码器 + 零拷贝 VPP（取代 OpenH264 与 CPU I420），rel 构建测量，Chrome 89 实测。

## M2（Stage 4 orchestrator，2026-10-26 13:00）——进行中，功能项基本完成

### 已完成并验证（真实链路）
- 窗口（补丁 15 `15-remote-window`）：remote 模式浏览器窗口最大化、无标题按钮/无边框/无非客户区命中；viewer 发 `resize {w,h,dpr}`（200 ms 防抖）→ endpoint 策略
  （DIP = w×h，DSF = min(dpr, sqrt(1080p 面积/DIP 面积))，≥1）→ Mojo `SetViewport` → `ScreenWinHeadless::SetPrimaryDisplayGeometry` + 重新最大化；
  1280x720、1920x1080、1600x900、390x844@3 实时切换通过，无黑边，点击坐标正确（`research/m2/window.md`）。
- 光标、IME/OSK（含中文组合只提交一次、Backspace、失焦收起、触摸点输入框弹出 OSK）、触摸（maxTouchPoints=5、pointer:coarse、tap/单指滚动/双指缩放）、
  zh-CN/zh-TW 产品名 PRTS（chrome:// 页面已核对；仍有少量 “Chrome 应用商店” 等非产品名字符串）：全部通过（`research/m2/input-integrate.md`）。
  代码在补丁 `16-m2-input-wip`（**待拆分**到 02/06/08/09/12 维护单元）。
- Network Service 启动崩溃：`scripts/grant-lpac-acl.ps1`（给 out\dev 与 profile 父目录加 S-1-15-2-1/S-1-15-2-2 RX）后 3/3 干净；随后的判别任务（基线、
  禁用 NetworkServiceSandbox、计划任务启动（脱离 Codex 进程树）、Temp 下 profile、WER/CDB）**全部无法复现**。根因：未查明（历史证据指向 LPAC 网络沙箱
  utility 启动早期；冷启动/加载器资源访问为推断）。沙箱保留，启动导航重试保留为守卫（`research/m2/netsvc-root.md`）。复现时用其中的抓取工具取栈。
- Widevine：EME `requestMediaKeySystemAccess('com.widevine.alpha')` 成功，Shaka 演示 Widevine 资源许可证 HTTP 200、播放时间前进（`research/m2/runtime.md`）。
- 延迟工具：marker 自动找行；input→photon（viewer 发键 → 页面翻色 → 解码帧检测）；每级时间分解（endpoint 5 s 滚动统计 + viewer getStats/rVFC）；
  `tools/netem/`（自写 TURN 中继 + 每方向延迟/抖动/令牌桶限速 + 字节上限 drop-tail 队列（默认为限速下 50 ms）/Gilbert-Elliott 丢包/抖动尖峰，
  双方强制 relay，`run.mjs --netem <profile>`）；每份报告 JSON 自动记录 `pipeline` 配置块。

### 延迟（全部：Chromium 157 dev/component 构建；渲染 = GPU 进程，Intel UHD 630，ANGLE D3D11，Skia Graphite；捕获 = viz FrameSinkVideoCapturer →
D3D11 共享纹理 → endpoint 私有拷贝 → staging 回读 → libyuv I420；编码 = OpenH264 软件，1898x1068@30，start/min/max 4736/2000/12000 kbps；
页面 = tools/latency/remote-page.html（标记页仅约 0.17 Mbps，**限速/队列在这些运行里没有起作用**）；显示点 = rVFC expectedDisplayTime（headless viewer
无真实扫描输出/vsync，非 glass-to-glass）；60 s/次；P50/P80/P95/P99 ms）
| 配置 | capture→display | input→decoded flip |
|---|---|---|
| 回环，headless Edge 156，playout-delay 0 + fluid（`research/m2/latency.md` final30） | 50.8/60.8/67.2/75.3 | 96.3/105.2/111.4/118.4 |
| 回环，headless Edge（netem 任务基线，同设置） | 67.8/75.1/81.4/91.0 | 104.8/115.5/128.8/138.2 |
| relay 不整形 | 85.2/92.4/99.5/110.6；重复 69.6/79.3/87.4/95.3 | 133.1/140.0/151.8/155.0；重复 106.0/118.6/133.1/138.7 |
| d50j10-c12（单向 50±10 ms，12 Mbps） | 117.7/130.9/143.0/154.1；重复 103.5/118.1/129.3/140.4 | 227.0/242.2/255.4/264.1；重复 214.0/227.2/237.3/251.5 |
| 回环，**有头** Edge（硬件解码：GPU VideoDecode 引擎约 6%，decoderImplementation 读不到） | 85.7/93.8/100.0/108.0 | 136.1/145.3/155.5/158.5 |
- 同配置单次运行间差约 15-18 ms（需多次取中位）。旧的 85.7 ms 数字（m2-runtime）属于修复前设置。
- 分解（回环，P50）：viz capture_begin→end 12.7；回读 5.5；I420 1.8；OpenH264 9.7（P95 30）；viewer 提交→预计显示 18.6；抖动缓冲约 0（playout-delay 0 生效，
  关掉它会 +55 ms）。M3 可去掉约 17 ms（回读 + 转换 + 软件编码）。
- 结论：d50 单向配置下 **P80 < 100 ms 不达标**（capture→display 118-131，input 227-242）；P99 < 200 ms capture→display 达标、input 不达标。d25、限速矩阵、Wi-Fi
  变体尚未跑（需要高码率内容：滚动/视频页）。

### 已知问题（新增）
1. 一次 GPU 进程崩溃（Skia Graphite `ResourceCache.cpp:523 isInCache`）后捕获不恢复（0 fps）——需要在 GPU 重启后重建 capturer（M3 前修）。
2. Chrome 89：`C:\src\tmp\chrome89` 是不带 H.264 的 Chromium 89 → 需要带专有编解码器的 Chrome 89（官方离线包）才能验证。
3. 有头 viewer 比 headless 慢约 20 ms（真实合成/vsync）；有头才是真实基线。
4. 补丁 16 是 WIP 合集，需拆分；endpoint 仍是 OpenH264 + CPU 回读。

### 下一步（Codex 15:01 恢复；Claude 窗口 13:30 重置）
1. `m3-vpl-integrate`：合入 `C:\src\tmp\m2\vpl\remotekit.diff`（已与延迟改动冲突，需 3-way；按其 NOTES vendoring libvpl 2.17），构建、冒烟、`--encoder=vpl` 测分解。
2. `m2-netem-matrix`：加高码率场景（滚动长页、1080p 视频、canvas 游戏式动画）后跑 d25j5/d50j10 × 16/12/10 Mbps + 两个 Wi-Fi 变体，OpenH264 与 VPL 各一轮，
   每个配置 ≥2 次，用有头 viewer；结果表写入本文件。
3. GPU 崩溃后 capturer 恢复；Chrome 89 H.264；补丁 16 拆分；zh-CN 剩余字符串。
