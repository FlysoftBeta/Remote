# Multi-tenant isolation (low priority; design only, not implemented)

**Status: planned.** Nothing here is implemented beyond what the chain already does (one process tree and one profile per instance). Per
the [brief](brief.md) it is built only after the full chain runs and feels usable. Target API level: Win10 24H2 (no Win11-only APIs).
Only `tools/isolation/grant-lpac-acl.ps1` exists (grants read/execute on deployed binaries to sandboxed processes).

## 1. Trust boundary
- An instance = one rb process tree (browser + GPU + network + utility + renderers) + one user-data-dir. Tabs inside an instance are
  **not** different tenants.
- `re` and the control-plane are outside the tenant container and act as trusted brokers; a compromised browser must not obtain the
  endpoint's process handle, other tenants' texture handles, files or network rights.
- Chromium's renderer/GPU sandbox still protects page -> browser; the outer boundary protects compromised browser -> host/other tenants.

## 2. Stages
| Stage | Mechanism | Provides | Prior evidence (earlier project) |
|---|---|---|---|
| I0 (from MVP) | one Job Object per instance (`PROC_THREAD_ATTRIBUTE_JOB_LIST` at `CreateProcess`, `KILL_ON_JOB_CLOSE`), process/memory limits, explicit `--user-data-dir`, `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` for inheritance | lifecycle, limits, whole-tree reclaim on endpoint crash | implemented and passing there |
| I1 | per-tenant local Windows account (or low-privilege token) + profile/temp ACLs | minimum boundary between mutually distrusting tenants | designed, untested |
| I2 (experiment) | whole browser tree in a unique AppContainer SID (plain AC, then LPAC) | file/registry/named-object/network capability isolation | partial: Crashpad `CreateNamedPipe` ACCESS_DENIED (fixed), `SBOX12/OpenWindowStation 5` with the pre-created alternate desktop, LPAC not working |
| I3 (optional) | per-tenant desktop (`CreateDesktop` + AC SID ACL) | cross-tenant `FindWindow/PostMessage`, hooks, clipboard isolation | named window station needs admin; private desktop in the same station stalled |

## 3. Known pitfalls (from the earlier project, not yet re-verified here)
- Any `JOB_OBJECT_UILIMIT_*` bit on the outer job made Chromium's nested-job child launch fail (`SBOX18`, zero frames). Re-test bit by bit.
- AppContainer needs `USERPROFILE/APPDATA/LOCALAPPDATA` on AC-writable paths; Crashpad pipe names must live in the AC namespace.
- A D3D11 hardware device can be created inside AC (Intel LUID matched), but verify "not SwiftShader/WARP" at every start.
- Shared texture NT handles: created by the browser, duplicated by `re` (`DuplicateHandle` with the browser process handle); keep that
  direction; no named shared objects.
- A Job does not isolate GPU scheduling or VRAM; mutually hostile tenants eventually need a VM boundary.

## 4. Decision
**User target (2026-10-26):** Job Object + `JOB_OBJECT_UILIMIT_*` + LPAC, managed by `re` (creates the job, LPAC profile/capabilities and ACLs, and
launches the whole rb tree with the LPAC token). Small Chromium patches are allowed (nested job, alternate desktop/window station,
Crashpad pipe, GPU/D3D11 access). **Give up** if the Chromium change exceeds ~1k lines or is likely to conflict in future versions
(core sandbox/IPC hot files): fall back to Job (+UILIMIT if possible) + plain AppContainer (no per-tenant accounts: too heavy) and record why.

Plan: (1) MVP does I0 only; (2) then I1, which needs no Chromium change; (3) I2/I3 as research, first step a controlled experiment that
enables one UILIMIT bit at a time, then adds LPAC, recording failing child/call/error and whether frames still appear. Rejected: disabling
the Chromium sandbox for AC compatibility; Job UI limit bits as the UI-isolation mechanism; per-tenant named window stations.
