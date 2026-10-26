You are working in C:\Users\Flysoft\Desktop\remotekit (Windows 11, PowerShell and Git Bash; sudo is available, UAC is off). Project: a cloud browser that streams a custom Chromium ("PRTS") over WebRTC + H.264 to a Chrome 89+ viewer. Read design/00-brief.md (requirements) and design/03-video-pipeline.md (video design) first.

Hard rules:
- Do NOT modify C:\src\chromium\src, endpoint code inside the Chromium tree, viewer/, control-plane/ or the design/ docs: another agent owns them and is mid-work. Do not start Chromium builds.
- Put all temporary files, binaries and test media under C:\src\tmp\<your task name>\. Never use C:\src.old.
- Only add files under research/ (your report) and tools/<your task dir>/ (your scripts and programs).
- Do NOT git commit or push. The orchestrator will review and commit.
- Keep the machine usable: at most 8 cores for heavy CPU work (the iGPU is shared).
