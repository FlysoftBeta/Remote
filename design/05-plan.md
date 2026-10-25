# 05 实施计划（阶段、里程碑、spike、Chromium 检出与构建）

版本：clean-room 复核版（2026-10-25）。排序原则（brief）：**先跑通可用的端到端链路**，再提体验，再测量调优，最后隔离。
测试最少，只为调试；不跑完整测试套件。每个里程碑结束 = 可演示 + `design/STATUS.md` 更新。

## 1. 里程碑

### M0 基础设施
- `scripts/cr.ps1` + `chromium/{PIN.json, series, args}`（02 §4）；depot_tools、`cr init`、先构建**未打补丁**的 `chrome`。
- endpoint 骨架改为**Chromium 树内 C++ 目标**（D12）：`endpoint/` 以 junction 映射到 `src/prts_endpoint`，补丁 14 把它挂进 `gn_all`；
  第一个可执行程序：合成帧源（D3D11 纹理）→ OpenH264 或 MF VEA → libwebrtc PeerConnection → viewer（data channel 回显）。
- control-plane：Node 样例，只做实例启动与信令中继（新协议）；已拷入的代码按 REVIEW 的候选清单逐项评估后再决定保留。
- 并行 spike：S7（VPL 参数矩阵，可作为树内小工具）、S8（Chrome 89 接收）、S10（Mojo invitation 握手，见下）。
- 出口：未打补丁 chrome 在 `--headless=new --screen-info=…` 下启动；`prts_endpoint` 用合成帧推 1080p H.264 到 viewer。

### M1 最小端到端链路（看到并操作真实浏览器窗口，含 tab strip/omnibox/菜单）
- 补丁 01、02（Mojo；S10 失败则管道）、03、04、05、06、07（鼠标/滚轮/键盘）、11（S11 通过时）、12、13、14。
- endpoint：帧导入（零拷贝 + 保守变体开关）、BGRA→NV12、编码器（先 MF VEA 或 OpenH264，VPL 在 M3）、libwebrtc H.264、data channel 输入。
- viewer：显示 + 鼠标/键盘 + 本地 textarea 提交文本。
- 出口：1080p30 浏览网页、点 tab、打开菜单/右键菜单/`<select>`、omnibox 输入网址回车；UA/UA-CH 显示 Google Chrome（echo 页手测）。

### M2 可用体验
- 触摸：07 touch + 09 `prts-web-prefs`；viewer PointerEvent 多指、fling。
- 远程软键盘：08 + viewer 策略（`IME_STATE` 驱动显示/隐藏、inputmode 映射、caret 附近定位）。
- 光标（`Cursor` 事件 → viewer CSS cursor）、窗口/标签管理、resize/DSF（`--screen-info` + S6 结论）、多浏览器窗口切换（D8 单一活动根）。
- 出口：手机/平板（Chrome 89+）上触摸浏览、输入、使用菜单无明显故障。

### M3 编码与传输主路径
- oneVPL `webrtc::VideoEncoder`（03 §3.4），零拷贝默认（S3 通过后），libwebrtc 码率/IDR 策略、playout-delay (0,0)。
- 测量工具（03 §5），B0 基线（三场景）。
- 出口：B0 在 RTT 50 ms ±10 ms、12 Mbps 下给出 capture→display 与 input-to-photon 的 P50/P80/P99。

### M4 延迟调优与对照实验
- 外部 BeginFrame（补丁 10，S5）、03 §6 的 E1-E14。出口：三场景 P80 < 100 ms、P99 < 200 ms，或明确瓶颈与下一步。

### M5 稳健性
- 设备丢失/驱动重置、浏览器/worker 崩溃重启、孤儿进程清理（supervisor + Job Object）、长时间运行、多 instance（2-4）并发；非 component Release 便携包。

### M6 隔离（低优先级，04）

## 2. Spike（括号内为最晚需要结论的里程碑）

| # | 问题 | 方法 | 通过标准 | 失败时 |
|---|---|---|---|---|
| S2 | cloaked HWND 下根 capturer 是否稳定出帧（交互会话 / RDP 断开 / 锁屏 / 无显示器） | 补丁 02+05 最小集，各跑 10 分钟，记录帧率与 `VSyncThreadWin` 回退 | ≥ 95% 目标帧率，无冻结 | external BeginFrame（S5）作时钟源（M1） |
| S3 | 零拷贝交接正确性与收益 | 帧号色块校验压力测试，对照保守变体 | 0 撕裂/错帧 | 保守变体为默认（M3） |
| S4 | capturer 成本（blit + GPU 完成后投递）对合成与延迟的影响 | ETW/`chrome://tracing`，三场景 | 投递延迟 P99 < 5 ms，合成帧率不降 | S12（M4） |
| S5 | external BeginFrame 对浏览器根是否安全（OOPIF、视频、动画） | 补丁 10，fixed/demand 三场景 | 无冻结、rAF 正常、P80 改善 | 保持 P0（M4） |
| S6 | 运行期改虚拟屏幕尺寸/DSF（viewer 旋转/换设备）是否需要钩子 | 无补丁 headless + `--screen-info`，测窗口 bounds、`window.outer*`、菜单位置；再测运行期改尺寸 | 列出仍需要的修正（若有） | 补 `ScreenWinHeadless` 运行期更新钩子（M2） |
| S7 | legacy MSDK 在 UHD 630 上接受的参数组合（LowPower、CBR/LowDelayBRC、MaxFrameSize、IntRef、Level 4.2、RGB4） | 独立 probe：Query/Init/GetVideoParam 矩阵 + 300 帧解码校验 | 可用组合表 | MF VEA（M3） |
| S8 | Chrome 89 与当前 Chrome 接收 1080p（SPS 4.0/4.2，协商 3.1）、playout-delay (0,0) | 便携 Chromium 89 + 当前 Chrome，硬解/软解，`webrtc-internals` | 正常解码，jitterBufferDelay ≈ 0 | 降 720p 或改 level（M1） |
| S9 | libwebrtc 作为树内独立可执行程序：(a) socket/线程/field trials 在 `build_with_chromium` 下怎么配；(b) `VideoStreamEncoder` 排队与 frame dropper；(c) GCC 在 Wi-Fi + 50 ms RTT 下的排队 P99 | (a) 读 `remoting/protocol` 与 `third_party/webrtc_overrides`；(b)(c) netem 实测 | (a) 合成帧链路跑通；(c) 排队 P99 < 1 帧 | (a) 按 `remoting/` 用 `net::UDPSocket`；(c) 调 pacer/FEC |
| S10 | Mojo 链接：(a) 浏览器（broker）向 endpoint 发 invitation 并传 D3D11 NT handle；(b) capturer remote 交给 endpoint，GPU 进程直接回调 endpoint 的 consumer | 最小原型：两端各 ~200 行 | (a) 握手 + 句柄可用；(b) 帧直达 | (a) 失败 → 管道协议；(b) 失败 → 浏览器做 consumer 转发描述符（M1） |
| S11 | 弹层留在根内（补丁 11）：菜单、右键菜单、omnibox 下拉、`<select>`、气泡、tooltip、浏览器模态对话框在 Windows headless 下是否正常（定位、焦点、关闭、动画） | 补丁 11 + 逐类手测 | 全部可用，无崩溃 | 删除补丁 11，回到多根捕获 + endpoint 合成（M1） |
| S12 | 自定义 `SkiaOutputDevice` 写共享纹理（仅当 S4 不通过） | b1 §5 的 5-8 文件原型 | 省掉 blit 且稳定 | 接受 capturer 成本 |
| S13 | `prts-web-prefs` 运行期切换 touch 能力是否即时生效 | 改参数后检查 `navigator.maxTouchPoints`/媒体查询 | 新导航或即时生效 | 仅在启动时设置 |

S2、S6、S11 在未打补丁构建完成后立即做；S7、S8、S10(a) 不依赖补丁，M0 并行。

## 3. Chromium 检出与构建

### 3.1 前置条件
- VS 2026 (18) + Windows SDK 10.0.28000（pin 的 `build/vs_toolchain.py` 期望值）、Debugging Tools；`DEPOT_TOOLS_WIN_TOOLCHAIN=0`；`core.longpaths true`；`C:\src` 加 Defender 排除（按需）。

### 3.2 检出
- `cr init` = unmanaged `.gclient` + `gclient sync --revision src@refs/tags/157.0.8103.15 --no-history --shallow -D`，`custom_vars: checkout_pgo_profiles=False`。
  `C:\src\cr-partial` 保留作快速 grep。

### 3.3 构建配置（建议；`chromium/args` 归 Stage 2）
- `out\dev`：component、`is_debug=false`、`dcheck_always_on=true`、`symbol_level=1`、blink/v8 symbol 0、`enable_nacl=false`。
- `out\rel`：non-component、`is_official_build=false`、dcheck off、`symbol_level=0`、`enable_nacl=false`；**延迟测量只用 rel**（component + dcheck 扭曲时序）。
- 两者都建议 `proprietary_codecs=true`、`ffmpeg_branding="Chrome"`（D11、S-video 场景）；endpoint 需要 OpenH264 回退时 `rtc_use_h264=true`。
- endpoint 目标增量构建只编译 `//prts_endpoint` 及其依赖；首次会编译 libwebrtc（已是 chrome 的依赖，构建过 chrome 后基本命中）。
- 运行期校验浏览器 GPU 进程与 endpoint 使用同一 LUID，拒绝 SwiftShader/WARP 冒充硬件。

### 3.4 构建纪律
- 一个 out 目录同时只跑一个 siso/ninja（锁文件）；运行中的 chrome.exe/endpoint 会锁 DLL → 构建前结束测试进程。
- 以 `.exit` 文件和日志尾部为准；原生测试串行执行。

## 4. 风险登记（摘要）
| 风险 | 影响 | 缓解 |
|---|---|---|
| R1 capturer 成本 | 尾延迟 | S4 → S12 |
| R2 UHD 630 渲染与编码争用 | 游戏场景 P99 | VDEnc；监控引擎占用 |
| R3 弹层根内化在 Windows 上的未知问题 | M1 进度 | S11，退路已定义 |
| R4 Chrome 89 接收端行为 | 兼容 | S8 |
| R5 libwebrtc 树内集成工作量 | M0/M1 进度 | S9(a)，参照 `remoting/` |
| R6 headless 下 Windows 原生副作用（光标、拖放、系统键盘） | 多租户互扰 | 补丁 06；M5 在交互会话中验证真实光标不动 |
| R7 “50 ms latency” 解读 | 验收口径 | RTT 与单向两种都测 |
