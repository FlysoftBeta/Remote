# 05 实施计划（阶段、里程碑、spike、Chromium 检出与构建）

排序原则（brief）：**先跑通一条可用的端到端链路**，再提体验，再测量调优，最后隔离。测试最少，只为调试；不跑完整测试套件。
每个里程碑结束 = 可演示 + `design/STATUS.md` 更新。时间是开发机（32 逻辑核、UHD 630）上的粗估。

## 1. 里程碑

### M0 基础设施（并行，0.5-1 天人力 + 首次构建 4-8 小时机器时间）
- `scripts/cr.ps1`（02 §4）+ `chromium/PIN.json`、`series`、`args/{dev,rel}.gn`。
- depot_tools、`cr init`（检出 + 依赖）、`cr gen dev`、**先构建未打补丁的 `chrome`**（验证工具链与缓存，后续增量构建快）。
- endpoint 仓骨架：把 prior art `prtsd/` 拷到 `endpoint/` 作为起点（保留 `transport.rs`、`browser.rs` 启动器/Job、`d3d.rs`、`mf_encoder.rs`、
  `encoder.rs` 回退链、`viewer.rs`、`shaping.rs`），control-plane 拷 `server/`。prior art 只读，拷贝后在新仓里改。
- 出口：未打补丁 chrome 在 `--headless=new` 下启动；endpoint 能编译并用合成帧源推 WebRTC 到 viewer（prior art 已有 Synthetic 源）。

### M1 最小端到端链路（目标：在 viewer 里看到并操作真实浏览器窗口，含 tab strip/omnibox）
- 补丁 01、02、03（仅浏览器窗口，无弹层）、04、05（**沿用 prior art 双拷贝交接**，先求稳）、06、07（鼠标/滚轮/键盘）、12、13。
- endpoint：新协议（HELLO/窗口/FRAME/INPUT）、帧导入、BGRA→NV12、**MF 编码器先上**（已验证可用），WebRTC H.264、data channel 输入。
- viewer：显示 + 鼠标/键盘 + 本地 textarea 提交文本。
- 出口：1080p30 能浏览网页、点 tab、在 omnibox 输入网址回车；UA/UA-CH 显示 Google Chrome（用 `d-branding.md` 的 echo 页手测一次）。

### M2 可用体验
- 弹层：03 的 host tracker + 每弹层 capturer + endpoint 场景合成（菜单、select、omnibox 下拉、右键菜单、气泡、tooltip）。
- 触摸：补丁 07 touch + 09 虚拟触摸能力；viewer PointerEvent 触摸、多指、fling。
- 远程软键盘：补丁 08 + viewer 软键盘策略（`IME_STATE` 驱动显示/隐藏、inputmode 映射、enterkeyhint、caret 附近定位）。
- 光标：`CURSOR` 事件 → viewer CSS cursor。
- 窗口/标签管理 RPC、resize、DSF（1.0/1.25/1.5/2.0），补丁 11（按 S6 结果）。
- 出口：手机/平板（Chrome 89+）上触摸浏览、输入、使用菜单无明显故障。

### M3 编码与传输主路径
- oneVPL 编码器（复活 prior art `wip/e1-onevpl` 的 bridge：`vpl.cpp`/`vpl_encoder.rs`/`build.rs`，按 03 §3.2 修正：显式实现选择/LUID、Level、LowPower、CBR 组）。
- 零额外拷贝交接（S3 通过后设为默认）、VP 对齐/缓存。
- webrtc-rs GCC/TWCC + 有界 pacer、playout-delay (0,0)、码率控制环、IDR 策略。
- 测量工具（03 §5）：标记块测试页、viewer 测量模式、netem 脚本；建立 B0 基线（三场景）。
- 出口：B0 在 RTT 50 ms ±10 ms、12 Mbps 下给出 capture→display 与 input-to-photon 的 P50/P80/P99。

### M4 延迟调优与对照实验
- external BeginFrame（补丁 10）与 demand pacing（S5 结论落地）。
- 03 §6 的 E1-E14；把结果写入 `design/experiments/`。
- 出口：P80 < 100 ms、P99 < 200 ms（capture→display）在三场景达标，或给出明确的瓶颈与下一步。

### M5 稳健性
- 设备丢失/驱动重置、浏览器崩溃重启、endpoint 重启后的孤儿清理（prior art 有实现）、长时间运行句柄/内存平稳、多 instance（2-4 个）并发。
- 打包：非 component Release 构建 + 便携包（prior art `package-preview.ps1` 思路）。

### M6 隔离（低优先级，04）
- I1 独立账户 → I2/I3 研究。

## 2. Spike（最大风险先验证；括号内为最晚需要结论的里程碑）

| # | 问题 | 方法 | 通过标准 | 失败时 |
|---|---|---|---|---|
| S1 | 补丁 02/03/05/06/07 在 M157 上的移植量（prior art 基于 M158 main） | 在 M157 checkout 上逐个 `git apply --3way` prior art 相关补丁看冲突面；读 M157 对应函数 | 冲突集中在少数文件、API 名变化可机械修正 | 按 02 的新结构重写，不硬搬（M1） |
| S2 | cloaked HWND 下根 capturer 是否稳定出帧（GPU/DComp 路径、无物理显示器/RDP 断开/锁屏） | 最小补丁：控制器 + 05，开发机上分别在交互会话、RDP 断开、锁屏下跑 10 分钟，记录帧率与 `VSyncThreadWin` 回退 | ≥ 目标帧率的 95%，无冻结 | 改用 external BeginFrame（S5）作为时钟源（M1） |
| S3 | 零额外拷贝交接：直接用 Viz 池纹理（key 0、按 `DXGIHandleToken` 缓存），Blt 后立即 `ReleaseSync`，正确性与收益 | endpoint 压力测试：快速变化画面 + 校验像素（帧号色块）对比保守变体（Blt 后等 EVENT）；统计 capture→encode-start | 0 撕裂/错帧，延迟降低 ≥ 1 ms 或 GPU 时间降低 | 保留 prior art 双拷贝（M3） |
| S4 | `CopyOutputRequest` mappable blit 在 GPU 线程上的等待对浏览器合成的影响（R1） | trace（`chrome://tracing` 或 ETW/PIX）测 GPU 主线程阻塞时间，三场景 | 每帧阻塞 < 2 ms | 评估非 mappable SharedImage 路径或自定义 OutputDevice（b1 §5）（M4） |
| S5 | external BeginFrame 对浏览器根是否安全（OOPIF 新鲜度、视频、动画、弹层根） | 补丁 10 + pacer，fixed 30/60 与 demand 模式跑三场景 | 无冻结/卡死、rAF 正常、input-to-photon P80 改善 | 保持 vsync + capture 节流（M4） |
| S6 | headless 几何：窗口外框/客户区、DSF、弹层定位、`window.outer*`（prior art 0021/0025/0028 是否在 M157 仍必要） | 在 M157 无补丁 headless 下测 960x640@DSF 1/1.25/2 的 bounds 与菜单位置 | 列出仍需要的修正 | 移植 prior art 修正（M2） |
| S7 | legacy MSDK runtime 在 UHD 630 上接受的参数组合（LowPower、CBR/LowDelayBRC、MaxFrameSize、IntRef、Level 4.2、RGB4） | 独立 probe 程序：Query/Init/GetVideoParam 回读矩阵 + 编码 300 帧解码校验 | 得到可用组合表 | 退 MF（M3） |
| S8 | Chrome 89 与当前 Chrome 接收 1080p（SPS level 4.0/4.2，协商 3.1）、playout-delay (0,0) 生效 | 便携 Chromium 89（`chromium-browser-snapshots` 对应 build）与当前 Chrome，硬解/软解，`webrtc-internals` 看 jitterBufferDelay | 解码正常，jitterBufferDelay ≈ 0 | 降 720p 或改 level（M1 末/M3） |
| S9 | webrtc-rs 0.21 GCC/pacer 在 Wi-Fi + 50 ms RTT 下的稳定性 | netem 对称延迟+抖动+丢包，测码率跟踪与排队时延 | 排队 P99 < 1 帧时长，无长时间低码率锁死 | str0m 或原生 libwebrtc 传输（M3/M4） |

S1、S2、S6 在 M0 构建完成后立即做；S7、S8 不依赖 Chromium 构建，可在 M0 期间并行。

## 3. Chromium 检出与构建计划（32 逻辑核、768 GB 空闲、Intel UHD 630）

### 3.1 前置条件（已核对）
- Visual Studio **2026 (18) Community** 已安装（`C:\Program Files\Microsoft Visual Studio\18\Community`），Windows SDK **10.0.28000.0** 与 10.0.26100.0、Debuggers 已装；
  pin 的 `build/vs_toolchain.py` 期望 VS 2026 + SDK 10.0.28000（已核对，`SDK_VERSION = '10.0.28000.0'`）。Stage 2 需确认 VS 已含
  “Desktop development with C++”、MFC/ATL 组件，SDK Debugging Tools ≥ 10.0.26100.3323。
- Rust（cargo 1.99.1）、Node v24 已装。Python 由 depot_tools 自带。
- 环境：`DEPOT_TOOLS_WIN_TOOLCHAIN=0`、`vs2026_install` 指向 VS 目录；`git config --global core.longpaths true`；建议把 `C:\src` 加入 Defender 排除（Stage 2 orchestrator 用 sudo 执行，属于本机开发配置，按需）。

### 3.2 检出
```powershell
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git C:\src\depot_tools
# PATH 前置 C:\src\depot_tools；gclient 首次运行自举
cr init        # = 写 .gclient(unmanaged) + gclient sync --revision src@refs/tags/157.0.8103.15 --no-history --shallow -D
```
- `.gclient` 加 `"custom_vars": {"checkout_pgo_profiles": False}`，不需要 NaCl/测试数据。预计 30-40 GB、网络 1-2 小时；googlesource 不稳定时
  可用 `research/cr-partial.md` 中验证过的 HTTP/1.1 + openssl 设置。`C:\src\cr-partial` 保留作只读快速 grep。

### 3.3 构建配置
- `out\dev`（日常）：`is_component_build=true`、`is_debug=false`、`dcheck_always_on=true`、`symbol_level=1`、`blink_symbol_level=0`、`v8_symbol_level=0`、
  `enable_nacl=false`、`use_siso=true`（pin 默认 siso，本地执行，无远程构建）。增量改 `chrome/browser/prts` 预计 1-3 分钟链接。
- `out\rel`（测量/打包）：`is_component_build=false`、`is_official_build=false`、`dcheck_always_on=false`、`symbol_level=0`、`enable_nacl=false`；
  **延迟测量只用 rel**（component + dcheck 会扭曲时序；prior art 还发现 component 掩盖了 Release 才出现的 UAF）。
- 首次全量：32 线程本地构建 `chrome` 估计 3-5 小时（prior art：16 线程 component ~6.5 小时）；两个 out 目录不要同时构建（磁盘与内存争用）。
  先建 `out\dev`，M3 前再建 `out\rel`。
- UHD 630 只与运行期相关：运行时校验 Chromium GPU 进程与 endpoint 使用同一 LUID（`vendor 0x8086, device 0x9bc5`），拒绝 SwiftShader/WARP 冒充硬件。

### 3.4 构建/运行纪律（prior art 教训）
- 一个 out 目录同时只允许一个 ninja/siso（锁文件）；运行中的 chrome.exe 会锁 DLL 导致链接失败 → 构建前结束测试进程。
- 构建结果以 `.exit` 文件和日志尾部为准，不信任陈旧日志。
- 原生测试串行执行（并发 UI 测试会互相污染）。

## 4. 风险登记（摘要）
| 风险 | 影响 | 缓解 |
|---|---|---|
| R1 capturer GPU 等待拖慢合成 | 尾延迟 | S4；后备自定义 OutputDevice |
| R2 UHD 630 渲染与编码争用 | 游戏场景 P99 | VDEnc（LowPower）用固定功能单元；限制 Chromium 光栅负载（UI 光栅走 CPU） |
| R3 M157 移植量大于预期 | 进度 | S1；按新结构重写，prior art 仅作参考 |
| R4 Chrome 89 接收端行为 | 兼容 | S8 |
| R5 Rust WebRTC 栈成熟度 | P99 | S9；str0m/libwebrtc 备选 |
| R6 headless 下 Windows 原生副作用（光标、拖放、系统键盘） | 多租户互扰 | 补丁 06 全部 remote 模式 no-op；M5 在交互会话中验证真实光标不动 |
| R7 “50 ms latency” 解读（RTT vs 单向） | 验收口径 | 两种都测，报告分开（01 §5） |
