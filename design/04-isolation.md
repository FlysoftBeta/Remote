# 04 多租户隔离（低优先级，短版）

状态：仅设计方向。按 brief，**完整链路跑通、体验可用之后**才实施。目标平台 Win10 24H2 级 API（不使用 Win11-only API）。

## 1. 信任边界

- 一个 instance = 一个 PRTS 浏览器进程树（browser + GPU + network + utility + renderers）+ 一个 user-data-dir。
  同一 instance 内的多个标签页**不是**不同租户。
- endpoint（C++）和 control-plane（Node）在租户容器之外，是可信 broker；浏览器被攻破不能拿到 endpoint 的进程句柄、
  其他租户的纹理句柄、文件或网络权限。
- Chromium 自带的 renderer/GPU 子进程沙箱继续生效，保护的是“网页 → 浏览器”；外层边界防的是“被攻破的浏览器 → 主机/其他租户”。

## 2. 分阶段方案

| 阶段 | 机制 | 能提供什么 | 先验证据（prior art `docs/rb/isolation.md`, `journal-i1.md`） |
|---|---|---|---|
| I0（MVP 起就有） | 每 instance 一个 Job Object，`PROC_THREAD_ATTRIBUTE_JOB_LIST` 在 `CreateProcess` 时原子加入，`KILL_ON_JOB_CLOSE`；进程/内存上限；显式 `--user-data-dir`；`PROC_THREAD_ATTRIBUTE_HANDLE_LIST` 限定继承句柄 | 生命周期、资源上限、endpoint 崩溃时整树回收 | 已在 prior art 实现并通过（job smoke / M4 / M5 / E2E PASS） |
| I1 | 每租户独立的 Windows 本地账户（或 per-tenant 低权限 token）+ profile/临时目录 ACL | 同机互不信任租户的最低可用边界 | 设计中，未实测 |
| I2（实验） | 整棵浏览器树跑在唯一 AppContainer SID 中（先普通 AC，再 LPAC） | 文件/注册表/命名对象/网络能力隔离 | 部分失败：Crashpad `CreateNamedPipe` ACCESS_DENIED（已修）；Chromium broker 预建 alternate desktop 时 `SBOX12/OpenWindowStation 5`；LPAC 未跑通 |
| I3（可选） | 每租户 desktop（同一 window station 下 `CreateDesktop` + AC SID ACL） | 跨租户 `FindWindow/PostMessage`、钩子、剪贴板隔离 | 命名 window station 需要管理员；同 station 下私有 desktop 方案停在 WIP |

## 3. 已知的坑（直接沿用 prior art 的结论）

- **外层 Job 不能开任何 `JOB_OBJECT_UILIMIT_*` 位**：每一位都会让 Chromium 嵌套 job 的子进程启动失败（`SBOX18`，0 帧）。
  UI 隔离只能靠 desktop/AC，而不是 Job UI 限制。
- AppContainer 需要 `USERPROFILE/APPDATA/LOCALAPPDATA` 指向 AC 可写目录；Crashpad 管道名要放进 AC 命名空间。
- GPU：AC 内 D3D11 硬件设备可以创建（native probe PASS，Intel LUID 匹配）；但必须在每次启动时校验“非 SwiftShader/WARP”，
  不能把软件渲染当作兼容方案。
- 共享纹理 NT handle：由浏览器创建、endpoint 用 `DuplicateHandle` 拿过来（endpoint 持有浏览器进程句柄），
  方向不变即可在 AC 下工作；不要用命名共享对象。
- Job 不隔离 GPU 调度/显存；互相敌对的租户最终需要 VM 级边界。

## 4. 决策

**用户定的目标方案（2026-10-26，优先于下文旧决策）**：Job Object + `JOB_OBJECT_UILIMIT_*` + LPAC，由 endpoint 管理（endpoint 负责创建 job、LPAC profile/capability、ACL，以 LPAC token 启动整棵 PRTS 浏览器树）。
- 允许对 Chromium 做小补丁让它在 外层 Job(UILIMIT) + LPAC 下工作（例如沙箱嵌套 job / alternate desktop / window station 访问、Crashpad 管道、GPU/D3D11 访问）。
- 放弃条件：所需 Chromium 改动超过约 1k 行，或很可能在未来版本中造成合并冲突（侵入核心沙箱/IPC 热点文件）。放弃时退回到 Job(+UILIMIT 若可行) + 普通 AppContainer（不用 per-tenant 账户，太重），并记录原因。
- M6 第一步：逐位受控实验（每次只开一个 UILIMIT 位，再叠加 LPAC），记录哪个子进程、哪个调用、什么错误码失败，以及能否出帧；上一节“任何 UILIMIT 位都会失败”的结论来自 prior art、未经本项目验证。

1. MVP 只做 I0（已有代码可复用到 endpoint 的 launcher 中）。
2. 链路可用后先做 I1（独立账户），它不需要改 Chromium。
3. I2/I3 作为研究项，复用 prior art 分支 `wip/i1-isolation`（infra `d045f0a`、Chromium `wip-i1=9d3afd6eae`）里的 provisioner 与探针，
   不重新从零调查。
4. 被否决：关闭 Chromium 沙箱换取 AC 兼容；用 Job UI 限制位做 UI 隔离；每租户命名 window station（需要管理员且影响共享 station ACL）。
