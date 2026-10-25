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
