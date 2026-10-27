# 06 协议：信令、会话、DataChannel（输入 / IME / 统计 / 延迟探针）

版本：**v1**（2026-10-25，parallel-track）。与 `01-architecture.md`（D7 输入、D8 焦点、D9 IME、D13）和 `03-video-pipeline.md`（§3.2 SDP、§5 测量）一致。
参考实现：`control-plane/`（信令/会话服务器）、`viewer/`（Chrome 89+ 客户端）、`tools/mock-endpoint/`（浏览器页面扮演 endpoint）、`tools/latency/`（测量）。
共享编解码代码：`viewer/protocol.js`（浏览器与 Node 通用，二进制输入编码/解码、常量）。

## 0. 总览

```text
viewer ──WS /signal (JSON)──► control-plane ◄──WS /signal?role=endpoint (JSON)── mock endpoint / 远程 endpoint
                                         ◄──stdio，4 字节 LE 长度前缀 + JSON（同一消息集）── re.exe（被 CP 拉起）
viewer ◄═══════════ WebRTC：H.264 视频（endpoint→viewer，recvonly）+ 3 条 negotiated DataChannel ═══════════► endpoint
```

- control-plane（CP）只做会话与信令中继，不在媒体/输入快路径上。
- **endpoint 是 offerer**：它决定 H.264 fmtp（`profile-level-id=42e01f;packetization-mode=1;level-asymmetry-allowed=1`，03 §3.2）与
  `playout-delay` RTP 扩展（`a=extmap … http://www.webrtc.org/experiments/rtp-hdrext/playout-delay`）。viewer 只 answer，并在 answer 前用
  `setCodecPreferences` 把 H.264 排在前面（若可用）。
- DataChannel 全部 `negotiated: true` + 固定 id：两端各自创建，不走 DCEP 往返，Chrome 89 支持。

## Browser <-> endpoint IPC

浏览器与 endpoint 仍使用 **Mojo**。唯一协议定义是 Chromium 的
`chrome/browser/rb/mojom/rb.mojom`（`browser/patches/02-rb-browser.patch`，后续维护补丁扩展它）；
`viz.mojom.RbD3D11FrameFence` 来自 `services/viz/privileged/mojom/compositing/frame_sink_video_capture.mojom`。
`re` 直接使用生成的 C++ bindings，没有另一套 pipe 协议。

| 接口 / 方向 | 方法与内容 |
|---|---|
| `RbBrowser`，endpoint → browser | `SetClient` 传 `pending_remote<RbBrowserClient>`，随后接收 Hello / 窗口状态 |
| 同上 | `StartCapture(window_id, CaptureParams{max_fps,nv12}, pending_remote<RbFrameSink>)`；`StopCapture(window_id)`；`RequestRefreshFrame(window_id)` |
| 同上 | `ActivateWindow(window_id)`；`Focus(focused)`；`NewWindow(url)`；`SetViewport(size_dip, dsf)` |
| 同上 | `Pointer(window_id, action/type/pid/x/y/buttons/button/mods/pressure/radius/timestamp)`；`Wheel(window_id,x/y/dx/dy/mods/timestamp)`；`Key(window_id,down/mods/repeat/key_code/code/key/timestamp)`；`ResetInput()` |
| 同上 | `ImeCommit(text,context)`；`ImeCompose(text,cursor,context)`；`ImeDelete(before,after,context)`；`ImeHidden(context)`；`SetTouchCapabilities(points,touch_only)` |
| `RbBrowserClient`，browser → endpoint | `Hello(version,gpu_luid,software_compositing)` |
| 同上 | `WindowAdded` / `WindowChanged`：id、DIP / px 尺寸、DSF、title；`WindowRemoved(id)`；`WindowActivated(id)` |
| 同上 | `CursorChanged(css)`；`ImeStateChanged(seq,focused,type,mode,flags,caret_norm,reason,composing,context)`；`VirtualKeyboard(show,seq)` |
| `RbFrameSink`，browser → endpoint | `NewBuffer(buffer_id, handle<platform> texture, coded_size)`：首次出现的 D3D11 shared **NT handle** |
| 同上 | `OnFrame(FrameInfo) => (bool consumed)`：buffer id、frame seq、coded / visible / content 几何、capture begin / end / reference TimeTicks，以及可选 ready fence |
| 同上 | `Reset()`：有序的 pool 边界；也用于 Viz / GPU 重建，与初始绑定、resize 不作原因区分 |

启动沿用旧 endpoint 的 `PlatformChannel` + `OutgoingInvitation::SendIsolated`，通过继承的
`--rb-mojo` 平台端点启动 `--rb-remote --headless=new` Chrome。发送 invitation 时提供真实 Chrome
process handle，供 Windows Mojo 复制句柄。已运行且没有预置 invitation 的浏览器无法直接 attach。
texture 经 Viz → browser 的 GPU buffer handle 到达 browser，由 browser `DuplicateHandle` 本地复制后，
经 `NewBuffer` 的 Mojo platform-handle 传输复制到 endpoint；数值 HANDLE 不作为 IPC 数据传送。
fence 从 Viz 每帧 lease 的 `GetRbReadyFence()` 返回，包含 token、value、adapter LUID、可选 shared fence
NT handle；browser 对每个 writer token 只发送一次 OS handle，之后只传 token / value / LUID。
没有 fence 时使用 keyed mutex，key 0；有 fence 时须在相同 adapter 上 `OpenSharedFence` 并提交 `Wait(value)`。

## 1. 信令（WebSocket，JSON）

### 1.1 连接与认证占位
- viewer：`ws(s)://<cp>/signal?role=viewer&instance=<id>&token=<t>`；endpoint：`…?role=endpoint&instance=<id>&token=<t>`。
- 认证占位：CP 若设置了环境变量 `CP_TOKEN`，则 token 必须相等，否则关闭码 `4401`。正式方案（签名的短期 token，绑定 instance 与角色）留给后续；
  **token 只在 WS URL 中出现一次，不进入任何日志**。
- 所有消息是 JSON 对象，字段 `t` 为类型；**每条连接的第一条消息是 `hello`，带 `v`（协议主版本，当前 1）**。主版本不一致 → 对端回 `{t:"error",code:"version"}` 并关闭（`4400`）。
  未知 `t` 忽略（向前兼容），未知字段忽略。

### 1.2 消息

| 方向 | `t` | 字段 | 说明 |
|---|---|---|---|
| viewer→CP | `hello` | `v, role:"viewer", caps:{touch:int, dpr:num, osk:bool, codecs:[...]}, ua` | `touch` = `navigator.maxTouchPoints`；`osk` = 是否像触摸设备（需要远程软键盘） |
| CP→viewer | `welcome` | `v, sid, instance, iceServers:[…]` | `sid` 会话 id（随机 128 bit hex） |
| endpoint→CP | `hello` | `v, role:"endpoint", instance, name, caps:{codecs:["H264"], maxFps}` | 注册 instance；同一 instance 新 endpoint 顶替旧的 |
| CP→endpoint | `session.open` | `sid, viewer:{caps, ua}, iceServers` | endpoint 为该会话建 PeerConnection，然后发 `offer` |
| endpoint→CP→viewer | `offer` | `sid, sdp` | |
| viewer→CP→endpoint | `answer` | `sid, sdp` | |
| 双向 | `ice` | `sid, candidate:{candidate, sdpMid, sdpMLineIndex}` 或 `candidate:null` | trickle；`null` = end-of-candidates |
| viewer→CP→endpoint | `restart` | `sid` | 请求 ICE restart：endpoint 以 `iceRestart:true` 重新 offer |
| 双向 | `bye` | `sid, reason` | 正常结束。CP 在任一侧 WS 断开时向另一侧合成 `bye` |
| CP→任一 | `error` | `code, message` | `version`、`auth`、`no-endpoint`、`busy` |

### 1.3 会话生命周期
1. viewer 连接 → `hello` → CP 校验 → 若 instance 没有在线 endpoint：回 `error:no-endpoint`，保持连接，endpoint 上线后自动继续（便于调试先开 viewer）。
2. CP 分配 `sid`，发 `welcome` 给 viewer，`session.open` 给 endpoint。
3. endpoint：建 PC（视频 sendonly + 3 条 DC）→ `offer`。viewer：`setRemoteDescription` → 设置接收延迟提示（§5）→ `answer`。双方 trickle ICE。
4. DTLS/SCTP 通后：viewer 在 `ctl` 发 `hello`（§3.1），endpoint 回 `hello`；之后输入开始流动。WebRTC 建立后 CP 只保留 WS 用于 `bye/restart`。
5. **单控制者**：每个 instance 同时只有一个 viewer 会话（D8 输入焦点是 instance 级）。新 viewer 进入 → 旧会话收到 `bye reason:"replaced"`，endpoint 关闭旧 PC
   并按 D8 释放按住的键/键钮、取消触点。
6. 结束：任一侧 `bye`、WS 断开（CP 15 s ping/30 s 超时）、PC `failed` 超过 10 s（viewer 先尝试一次 `restart`）。

### 1.4 endpoint 的 stdio 传输
CP 拉起 `re.exe`（`ENDPOINT_PATH`）时，用 stdin/stdout 承载同一消息集，帧格式 = 4 字节小端长度 + UTF-8 JSON（≤ 4 MiB，`control-plane/framing.js`）。
`instance` 由命令行给出（`--instance=<id>`）。endpoint 的 stderr 是日志。实例控制（启动浏览器、关闭）放在同一通道的 `instance.*` 消息中，后续定义。

## 2. DataChannel

| id | label | 选项 | 编码 | 用途 |
|---|---|---|---|---|
| 0 | `input` | ordered, reliable | **二进制** | 鼠标/指针/触摸/滚轮/键盘（高频，热路径） |
| 1 | `ctl` | ordered, reliable | JSON 文本 | hello、resize、焦点、IME/OSK、光标、统计、关键帧请求 |
| 2 | `probe` | **unordered, maxRetransmits=0** | JSON 文本 | 时钟同步 ping/pong（丢了就丢，不能被重传排队污染 RTT） |

### 2.1 为什么输入用二进制、其余用 JSON（延迟论证）
- 延迟上两者的差别不在编解码 CPU（都是微秒级），而在**字节数与排队**：一次 120 Hz 的多指移动每秒数百条消息；二进制每条 12-20 字节，JSON 60-120 字节。
  SCTP/DTLS 每条消息有 ~28+ 字节固定开销，二进制让每条输入稳定落在一个小 DATA chunk 内、在上行 Wi-Fi 上占用更少空口时间，拥塞时 `bufferedAmount` 涨得更慢。
- endpoint 是 C++：固定布局小端结构可以零分配解析，直接映射到 `ui::MouseEvent/TouchEvent/KeyEvent`；不需要 JSON 解析器进入每个指针事件的路径。
- 输入走 **ordered reliable**：键盘/按钮的 down/up 丢失或乱序会造成“卡键”，代价远大于一次重传（局域网 RTT 50 ms 下重传 ≈ 1 RTT，且只在丢包时发生）。
  指针 move 的“过期”问题用发送端合并解决（§2.4），而不是用不可靠通道。
- 低频消息（IME 文本、状态、统计）用 JSON：可读、易扩展、可调试，频率低到字节数无关紧要。

### 2.2 二进制输入格式（`input`，小端）
每条消息一个 DataChannel message；公共 8 字节头：

| 偏移 | 类型 | 字段 |
|---|---|---|
| 0 | u8 | `kind`：1 POINTER，2 WHEEL，3 KEY，4 RESET |
| 1 | u8 | `action` / 子类型（见下） |
| 2 | u16 | `seq`：每条输入消息 +1（回绕），用于 input-to-photon 与丢失检测 |
| 4 | u32 | `ts`：viewer 时钟（`performance.timeOrigin+now()`）毫秒 ×10 取低 32 位（0.1 ms 分辨率，约 5 天回绕） |

`mods`（u8 位掩码）：1 Shift、2 Ctrl、4 Alt、8 Meta、16 CapsLock、32 NumLock、64 AltGraph。

**坐标**：`x, y` 为 u16，**相对视频帧可见区域归一化**：`round(px / frameW * 65535)`，与 viewer 显示尺寸、DPR、letterbox 无关；
endpoint 乘以当前根的 DIP 尺寸得到 `gfx::PointF`（D7：`EventInjector` 再做 DIP→host 像素）。落在 letterbox 黑边的事件被夹到边界（down 不发送，见 viewer）。

**POINTER（kind=1，20 字节）**：

| 偏移 | 类型 | 字段 |
|---|---|---|
| 1 | u8 | action：0 down，1 move，2 up，3 cancel，4 leave（鼠标离开视频区） |
| 8 | u16 | x |
| 10 | u16 | y |
| 12 | u8 | `ptype`：0 mouse，1 touch，2 pen |
| 13 | u8 | `pid`：指针槽位 0-15（鼠标恒为 0；触摸/笔由 viewer 把 `pointerId` 映射到 1-15 中最小的空闲槽位，最多 15 个同时触点） |
| 14 | u8 | `buttons`：DOM `buttons` 位掩码（1 左，2 右，4 中，8 后退，16 前进） |
| 15 | i8 | `button`：本次变化的键钮（DOM `button`，move 时为 -1） |
| 16 | u8 | mods |
| 17 | u8 | `pressure` ×255 |
| 18 | u8 | `rx`：触点半径，视频帧像素（夹到 255） |
| 19 | u8 | `ry` |

- 键钮变化（含按住一个键时再按另一个的 chord）一律作为 down/up 发送（每个变化的键钮一条，带新的 `buttons`），不作为 move。
- `seq` 在**实际发送时**分配（合并后的 move 取发送时的 seq），保证线上 seq 单调。
- 多点触摸 = 每个指针一条消息，用 `pid` 区分；这正好对应 Aura 的 `ui::TouchEvent`（每个事件一个 pointer id），endpoint 不需要聚合。
- endpoint 侧按 `pid` 跟踪活动触点；收到 RESET 或会话结束时为所有活动触点合成 cancel、为按住的键钮合成 up（D8）。

**WHEEL（kind=2，16 字节）**：

| 偏移 | 类型 | 字段 |
|---|---|---|
| 1 | u8 | mode：0 像素，1 行，2 页（DOM `deltaMode`） |
| 8 | u16 | x |
| 10 | u16 | y |
| 12 | i16 | dx：mode 0 时为**视频帧像素 ×4**（viewer 已按显示缩放换算）；mode 1/2 时为行/页 ×4 |
| 14 | i16 | dy：同上 |

（mods 不单列：endpoint 用最近一次 KEY/POINTER 的 mods 状态；Ctrl+滚轮缩放依赖它。）

**KEY（kind=3，变长）**：

| 偏移 | 类型 | 字段 |
|---|---|---|
| 1 | u8 | action：0 down，1 up |
| 8 | u8 | mods（事件发生后的状态） |
| 9 | u8 | flags：1 repeat，2 location-right，4 numpad |
| 10 | u16 | `keyCode`（DOM 旧式 keyCode，≈ Windows VK；仅作回退） |
| 12 | u8 | `codeLen` |
| 13 | u8 | `keyLen` |
| 14 | … | `code`（UTF-8，如 `KeyA`）后接 `key`（UTF-8，如 `a`、`Enter`、`Dead`） |

endpoint：`ui::KeycodeConverter::CodeStringToDomCode(code)`、`KeyStringToDomKey(key)`（单字符 key → `DomKey::FromCharacter`），
`KeyboardCode` 由 DomCode/DomKey 推导，`keyCode` 只在 code 为空时回退。可打印字符由 Aura 正常生成 char 事件，**不再单独发 char**。
`isComposing` 或 `keyCode==229` 的键盘事件 viewer 不发送（交给 IME 路径，§3.3）。

**RESET（kind=4，8 字节）**：释放所有按住的键/键钮、取消所有触点。viewer 在 blur、visibilitychange(hidden)、指针 capture 丢失时发送。

### 2.3 版本与扩展
- `ctl` 的 `hello` 带 `input:1`（二进制输入格式版本）；新增字段只能追加在消息末尾，接收方按长度容忍更长的消息；新 `kind` 对旧端被忽略。
- 消息长度不足其 kind 的最小长度 → 丢弃并计数。

### 2.4 发送端合并（viewer）
- 指针 move：每个 `pid` 只保留最新坐标；`input.bufferedAmount < 4 KiB` 时立即发送，否则缓存到下一次 `bufferedamountlow`/rAF 再发（只发最新）。
  down/up/cancel、KEY、RESET 永远立即发送，且发送前先冲掉该 `pid` 的待发 move（保证顺序）。
- 使用 `getCoalescedEvents()` 不必要：我们要的是最新位置，而非轨迹（绘图类应用以后可加 `kind=1, action=1` 的批量变体）。

## 3. `ctl` 通道（JSON）

### 3.1 握手与视口
| 方向 | `t` | 字段 |
|---|---|---|
| viewer→ep | `hello` | `v:1, input:1, caps:{touch, osk, dpr}, view:{w, h, dpr}` |
| ep→viewer | `hello` | `v:1, input:1, frame:{w, h}, dsf, name` |
| viewer→ep | `resize` | `w, h, dpr`：viewer 可用的显示区域（CSS px）与 DPR。endpoint **可以**据此调整远端窗口（DIP = w×h，DSF = dpr 或策略值），也可以忽略（固定 1080p） |
| ep→viewer | `frame` | `w, h, dsf`：远端根尺寸变化（视频分辨率随之变化；坐标归一化不受影响） |
| viewer→ep | `focus` | `focused:bool`：viewer 页面获得/失去焦点或可见性。false → endpoint 释放输入并 `Deactivate` 活动根（D8）；true → `Activate` |
| viewer→ep | `keyframe` | 请求 IDR（调试；正常由 RTCP PLI/FIR 触发） |
| ep→viewer | `cursor` | `css`：远端光标对应的 CSS cursor 名（`default`、`pointer`、`text`、`wait`、`none`…）。来自 remote 模式下拦截的 `::SetCursor`（D7） |

### 3.2 IME / 远程软键盘（endpoint → viewer）
来源：D9 的 `RemoteInputMethod` + `InputMethodObserver`（`OnTextInputStateChanged`、`OnCaretBoundsChanged`、VK controller 的 Display/Dismiss）。

| `t` | 字段 | 语义 |
|---|---|---|
| `ime.state` | `seq, focused:bool, type, mode, flags, rect:{x,y,w,h}, reason, composing:bool` | 可编辑焦点变化或光标框变化。`focused:false` = blur/detach（`type:"none"`）。 |
| `ime.vk` | `show:bool, seq` | 远端“该弹/该收软键盘”的决策（`VirtualKeyboardControllerWin` 策略经 `GetVirtualKeyboardController()` 输出） |

- `type`：`text, password, search, email, number, tel, url, textarea, contenteditable, date, time, datetime, month, week, null, none`（`ui::TextInputType` 小写去前缀）。
- `mode`：`default, none, text, tel, url, email, numeric, decimal, search`（`ui::TextInputMode`）。`inputmode=none` 时 `ime.state focused:true` 但 `ime.vk` 不会 show。
- `flags`：位掩码透传 `ui::TextInputFlags` 的子集：1 autocomplete-off，2 autocorrect-off，4 spellcheck-off，8 autocapitalize-none，16 characters，32 words，64 sentences。
- `rect`：光标/选区框，**归一化到视频帧（0-65535，同输入坐标）**；viewer 用它放置隐藏输入框（让候选窗/系统放大镜贴近）并在软键盘遮挡时平移视图。
- `reason`：`mouse, touch, pen, other, none`（`ui::TextInputClient::FocusReason`）。
- `seq` 单调递增；viewer 丢弃比已处理更旧的状态。

### 3.3 文本回填（viewer → endpoint）
| `t` | 字段 | endpoint 动作 |
|---|---|---|
| `ime.commit` | `text` | `TextInputClient::InsertText(text)`（有组合则先确认/替换组合） |
| `ime.compose` | `text, cursor` | `SetCompositionText`（`cursor` = 组合内光标位置，UTF-16 下标）；空文本 = 清除组合 |
| `ime.delete` | `before, after` | `ExtendSelectionAndDelete(before, after)`（UTF-16 单位）。Android 软键盘的退格多以此方式到达 |
| `ime.key` | `key` | 软键盘上的功能键（`Enter`、`Backspace`、`Tab`…）当 viewer 只拿到 `keyCode 229` 时：endpoint 合成一对 KEY down/up |
| `ime.hidden` | — | 用户在本地收起了软键盘（viewer 的隐藏输入失焦）；endpoint 仅记录，不改变远端焦点 |

viewer 行为（Chrome 89 约束）：
- 隐藏输入为一个 1×1、`opacity:0`、`position:fixed` 的 `<textarea>`（`autocomplete/autocorrect/autocapitalize/spellcheck` 按 `flags` 设置，`inputmode`/`type` 按 `mode`/`type` 设置；
  password 用 `<input type=password>`）。
- 收到 `ime.vk show:true`：`textarea.focus()`。**移动端浏览器只在“用户激活”期间允许程序化 focus 弹出软键盘**；远端判定通常在触摸后一个 RTT（< 150 ms）内到达，
  落在 transient activation 窗口内。若超时（> 1 s，或 focus 后 `visualViewport` 未缩小），viewer 显示一个浮动“键盘”按钮，用户点按即在手势内 focus。
- 文本采集：`compositionstart/update/end` → `ime.compose`/`ime.commit`；无组合的 `input` 事件（`insertText`）→ `ime.commit`；`deleteContentBackward/Forward` → `ime.delete`；
  每次处理后清空 textarea（保持“无上下文”的输入缓冲，远端是唯一真相）。
- 物理键盘在 textarea 聚焦时仍按 §2.2 发 KEY（`keydown` 中 `isComposing==false && keyCode!=229` 的键），并对这些键 `preventDefault`，避免重复文本。
- `ime.vk show:false` 或 `ime.state focused:false` → `textarea.blur()`。

## 4. 统计与延迟探针

### 4.1 `probe` 通道（时钟同步）
- viewer→ep：`{t:"ping", id, t0}`（viewer 墙钟毫秒，`performance.timeOrigin + performance.now()`，浮点）。
- ep→viewer：`{t:"pong", id, t0, t1, t2}`：`t1` = endpoint 收到时刻，`t2` = 发出时刻（endpoint 墙钟毫秒，浮点；C++ 用 QPC 校准到系统时间）。
- viewer：`t3` = 收到时刻；`rtt = (t3-t0) - (t2-t1)`，`offset = ((t1-t0)+(t2-t3))/2`（endpoint 时钟 − viewer 时钟）。
  保留最近 32 个样本，取 **RTT 最小** 的样本的 offset（NTP 式最小延迟滤波）；4 Hz 发送，前 2 s 20 Hz 预热。
- 为什么用墙钟：真实部署中帧标记由远端浏览器里的测试页绘制（同一台主机），页面的 `timeOrigin+now()` 与 endpoint 的系统时间同源，
  因此只需要一次 viewer↔endpoint 的时钟映射。

### 4.2 帧标记（像素编码，`tools/latency/marker.js`）
- 位置：视频帧左上角，一行 **2 个同步格 + 72 个数据格**，每格 `cell = max(4, round(frameW/160))` 像素（1080p 下 12，888×12 px；接收端按 `cell × videoWidth / frame.w` 缩放，`frame.w` 来自 ctl hello/frame，未知时用同一公式），黑=0 白=1（亮度阈值 50%）。
- 数据（大端比特序）：`frameId:u16 | t:u32（发送端墙钟毫秒低 32 位） | inputSeq:u16（最近一次已应用的输入 seq） | crc8`（多项式 0x07，覆盖前 8 字节）……共 9 字节 = 72 比特。
  同步格：白、黑，用于定位与阈值自检；CRC 不符的帧丢弃（被编码器糊掉或过渡帧）。
- 发送端（mock endpoint 的 canvas、真实链路中远端浏览器里的测试页）在**每帧绘制时**写入当前时间；接收端在 `requestVideoFrameCallback` 中把视频帧左上区域
  `drawImage` 到小 canvas 读像素。
- capture→display 延迟 = `displayTime − (tUnwrapped − offset)`；`displayTime` 取 rVFC 的 `metadata.expectedDisplayTime`（Chrome 89 可用；否则回调参数 `now`）。
- input-to-photon：viewer 记录每个输入 `seq` 的发送时刻；第一帧 `inputSeq ≥ s` 的显示时刻 − 发送时刻。

### 4.3 统计消息（`ctl`）
| 方向 | `t` | 字段（1 Hz） |
|---|---|---|
| ep→viewer | `stats` | `fps, kbps, encMs:{p50,p99}, qp, idr, backend, queue, bweKbps` |
| viewer→ep | `vstats` | `fps, kbps, jbMs, decMs, rtt, lost, nack, pli, freeze, lat:{p50,p80,p99}` |

viewer 的 `jbMs` = Δ`jitterBufferDelay`/Δ`jitterBufferEmittedCount`，`decMs` = Δ`totalDecodeTime`/Δ`framesDecoded`（Chrome 89 的 `inbound-rtp` 均有）；
`rtt` 取 `candidate-pair.currentRoundTripTime`。

## 5. 接收端延迟提示（Chrome 89 实际支持）
| 机制 | Chrome 89 | 当前 Chrome | viewer 做法 |
|---|---|---|---|
| `RTCRtpReceiver.playoutDelayHint`（秒，非标准） | **有**（M79 起无需 trial） | 有 | `'playoutDelayHint' in r` 时设 0 |
| `RTCRtpReceiver.jitterBufferTarget`（毫秒，标准） | 无（Chrome 124 起） | 有 | 优先使用，设 0 |
| RTP `playout-delay` 扩展 (min=0,max=0) | **生效**：M89 `VCMTiming` 在 min=max=0 时 render time = 0（立即渲染） | 生效 | 由 endpoint 发送（offer 中 extmap）；viewer 不需要做什么 |
| `setCodecPreferences` | 有（M76+） | 有 | 把 `RTCRtpReceiver.getCapabilities('video')` 中 H.264（packetization-mode=1 优先）排前 |
| `requestVideoFrameCallback` | 有（M83+） | 有 | 测量用；无则退回 `timeupdate` 粗估并标记“不精确” |

注意（e-video §M89）：接收端的 `UpdatePlayoutDelays` 取 frame/base/sync 最小值中的最大者——**JS 侧设非零 hint 会抵消 RTP (0,0) 的立即渲染**，所以只设 0 或不设。
实测（`research/s8-chrome89.md`，Chromium 89.0.4389.0）：`playoutDelayHint` 在 89 上作为**下限**生效（0.1 s → 抖动缓冲恰为 100 ms），
但设 0 **不会**把缓冲压到 89 自己的估计以下（本机回环 35-42 ms，而 156 为 7-9 ms）→ Chrome 89 的低延迟必须依赖 endpoint 发送 RTP playout-delay (0,0)。
89 的 `jitterBufferDelay/EmittedCount` 在旧式 `track` 统计对象上，不在 `inbound-rtp` 上（viewer 已回退）。

## 6. 安全与健壮性
- endpoint 对所有 DataChannel 输入做长度/范围校验；坐标夹到 [0,65535]；`pid` ≥ 16 丢弃；KEY 字符串 ≤ 32 字节；IME 文本 ≤ 4096 UTF-16 单位。
- CP 对每条 WS 消息 ≤ 64 KiB，非 JSON 关闭连接；只转发白名单类型。
- 输入只在双方 `ctl hello` 交换完成后发送/接受（viewer 收到 endpoint 的 hello 才启用输入；版本不符则关闭会话）；会话结束、`focus:false`、`bye` → endpoint 执行 RESET 语义。
