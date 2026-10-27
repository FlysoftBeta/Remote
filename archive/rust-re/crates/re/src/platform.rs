use crate::Result;
use std::{
    ffi::{c_char, c_void},
    path::Path,
    sync::atomic::{AtomicBool, Ordering},
};
use windows::Win32::System::{
    Console::SetConsoleCtrlHandler,
    Performance::{QueryPerformanceCounter, QueryPerformanceFrequency},
};
static STOP: AtomicBool = AtomicBool::new(false);
struct Logger;
impl log::Log for Logger {
    fn enabled(&self, _: &log::Metadata<'_>) -> bool {
        true
    }
    fn log(&self, r: &log::Record<'_>) {
        eprintln!("[re {}] {}", r.level(), r.args());
    }
    fn flush(&self) {}
}
pub fn init() {
    let _ = log::set_logger(&Logger);
    log::set_max_level(log::LevelFilter::Info);
    unsafe {
        let _ = SetConsoleCtrlHandler(Some(stop), true);
    }
}
unsafe extern "system" fn stop(_: u32) -> windows::core::BOOL {
    STOP.store(true, Ordering::Release);
    true.into()
}
pub fn stopping() -> bool {
    STOP.load(Ordering::Acquire)
}
pub fn now_us() -> i64 {
    static FREQUENCY: std::sync::OnceLock<i64> = std::sync::OnceLock::new();
    let f = *FREQUENCY.get_or_init(|| {
        let mut n = 0;
        unsafe {
            QueryPerformanceFrequency(&mut n).expect("QPC frequency");
        }
        n
    });
    let mut n = 0;
    unsafe {
        QueryPerformanceCounter(&mut n).expect("QPC");
    }
    ((n as i128 * 1_000_000) / f as i128) as i64
}
extern "C" fn native_log(_: *mut c_void, severity: i32, p: *const c_char, n: usize) {
    // Native logging has process-lifetime callback storage and borrows UTF-8.
    let text = unsafe { std::slice::from_raw_parts(p.cast::<u8>(), n) };
    let text = String::from_utf8_lossy(text);
    if severity >= 2 {
        log::error!("{}", text.trim_end());
    } else if severity == 1 {
        log::warn!("{}", text.trim_end());
    } else {
        log::info!("{}", text.trim_end());
    }
}
pub fn native_logging(path: &Path) -> Result<()> {
    // The session wrappers pin this same module until process exit.
    let lib = unsafe { libloading::Library::new(path)? };
    type Set = unsafe extern "C" fn(
        Option<extern "C" fn(*mut c_void, i32, *const c_char, usize)>,
        *mut c_void,
    );
    unsafe {
        let set: libloading::Symbol<'_, Set> = lib.get(b"re_native_set_log_callback\0")?;
        set(Some(native_log), std::ptr::null_mut());
    }
    std::mem::forget(lib);
    Ok(())
}
