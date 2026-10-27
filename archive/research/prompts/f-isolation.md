# Task f: Windows isolation primitives (short, low priority)
Write: C:\Users\Flysoft\Desktop\remotekit\research\f-isolation.md   (target 150-300 lines)

Context: multi-tenant cloud browser on Windows Server / Win10 24H2-level APIs (avoid Win11-only APIs). Each tenant gets a
PRTS Chromium (which already runs its own sandbox: renderers in AppContainer/LPAC? or restricted tokens + alternate desktop,
GPU process with its own policy) plus a browser-endpoint process. Survey:
1. Job Objects (limits, UI restrictions JOB_OBJECT_UILIMIT_*, CPU rate control, memory limits, network rate control,
   silos? server silos are container-only), nested jobs, how Chromium already uses jobs (sandbox/win/src/job.cc, policy).
2. AppContainer and LPAC (capabilities, CreateAppContainerProfile, named object isolation, network capabilities,
   loopback exemption), and whether the Chromium *browser* process can run inside an AppContainer (it normally cannot; why).
3. Restricted tokens, integrity levels, separate local user accounts per tenant (most robust?), logon sessions.
4. Window stations and desktops (CreateDesktop per tenant, the Chromium alternate desktop for renderers, effect on the
   cloaked HWND, DWM, GPU access on a non-interactive window station / session 0 limitations — important: does D3D11 GPU
   work in a service session / non-interactive desktop? DWM cloaking needs DWM).
5. How the Chromium sandbox uses these today (sandbox/win/src: TargetPolicy, TokenLevel, JobLevel, IntegrityLevel,
   AppContainer for GPU/network service, alternate desktop/winstation), referencing files at current stable.
6. Recommendation: a staged plan (per-tenant user account + job object + per-tenant desktop?), what is feasible on Win10
   24H2-level, and the key risks (GPU in session 0, clipboard, focus isolation across tenants on one desktop).
