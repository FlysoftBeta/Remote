//! Instance manager (01 section 2): one instance = one PRTS browser process tree + one profile dir.
//! M1: launch chrome.exe --headless=new --prts-pipe=<cmd>,<evt>,<frm> with PROC_THREAD_ATTRIBUTE_HANDLE_LIST
//! (exact handle inheritance, D3) and atomic Job Object assignment (kill-on-close).
#![allow(dead_code)]
pub struct InstanceId(pub u32);
