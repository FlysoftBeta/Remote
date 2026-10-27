//! Checked-in bindings for re_native_mojo.h API v1; all unsafe is confined here.
use super::{Buffer, Error, Event, Frame, FrameAction, FrameId, Hello, Ime, Key, Result, Window};
use std::{
    any::Any,
    cell::Cell,
    ffi::{CString, c_char, c_void},
    marker::PhantomData,
    os::windows::{
        ffi::OsStrExt,
        io::{AsRawHandle, BorrowedHandle},
    },
    panic::{AssertUnwindSafe, catch_unwind, resume_unwind},
    path::Path,
    ptr::{self, NonNull},
    rc::Rc,
    slice, str,
    sync::atomic::{AtomicU64, Ordering},
    time::Duration,
};

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Size {
    pub width: i32,
    pub height: i32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Rect {
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct RectF {
    pub x: f32,
    pub y: f32,
    pub width: f32,
    pub height: f32,
}
#[repr(C)]
#[derive(Clone, Copy)]
struct StringView {
    data: *const c_char,
    size: u32,
}
impl StringView {
    fn new(s: &str) -> Result<Self> {
        Ok(Self {
            data: s.as_ptr().cast(),
            size: s
                .len()
                .try_into()
                .map_err(|_| Error("string too long".into()))?,
        })
    }
    unsafe fn as_str<'a>(&self) -> &'a str {
        if self.size == 0 {
            return "";
        }
        // Mojo validates UTF-8 and the native callback owns these bytes.
        str::from_utf8(unsafe { slice::from_raw_parts(self.data.cast(), self.size as usize) })
            .unwrap_or("")
    }
}
#[repr(C)]
struct HelloInfo {
    version: u32,
    gpu_luid: u64,
    software: u8,
}
#[repr(C)]
struct WindowInfo {
    id: u32,
    dip: Size,
    dsf: f32,
    px: Size,
    title: StringView,
}
#[repr(C)]
struct ImeInfo {
    seq: u32,
    focused: u8,
    kind: StringView,
    mode: StringView,
    flags: u32,
    caret: RectF,
    reason: StringView,
    composing: u8,
    context: u32,
}
#[repr(C)]
struct BufferInfo {
    window: u32,
    buffer: u32,
    epoch: u64,
    texture: *mut c_void,
    size: Size,
}
#[repr(C)]
struct FrameInfo {
    window: u32,
    buffer: u32,
    epoch: u64,
    lease: u64,
    seq: u64,
    texture: *mut c_void,
    fence: *mut c_void,
    token_high: u64,
    token_low: u64,
    fence_value: u64,
    luid: u64,
    size: Size,
    visible: Rect,
    content: Rect,
    begin_us: i64,
    end_us: i64,
    reference_us: i64,
    received_us: i64,
    callback_us: i64,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Pointer {
    pub action: i32,
    pub kind: i32,
    pub pid: u8,
    pub x: f32,
    pub y: f32,
    pub buttons: u8,
    pub button: i8,
    pub mods: u8,
    pub pressure: f32,
    pub radius_x: f32,
    pub radius_y: f32,
    pub timestamp_us: i64,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Wheel {
    pub x: f32,
    pub y: f32,
    pub dx: f32,
    pub dy: f32,
    pub mods: u8,
    pub timestamp_us: i64,
}
#[repr(C)]
struct KeyInfo {
    down: u8,
    mods: u8,
    repeat: u8,
    code_num: u16,
    code: StringView,
    key: StringView,
    timestamp_us: i64,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Stats {
    pub frames: u64,
    pub released: u64,
    pub abandoned: u64,
    pub rejected: u64,
    pub resets: u64,
    pub outstanding: u64,
    pub now_us: i64,
}
#[repr(C)]
struct Callbacks {
    version: u32,
    size: u32,
    hello: extern "C" fn(*mut c_void, *const HelloInfo),
    window_added: extern "C" fn(*mut c_void, *const WindowInfo),
    window_changed: extern "C" fn(*mut c_void, *const WindowInfo),
    window_removed: extern "C" fn(*mut c_void, u32),
    window_activated: extern "C" fn(*mut c_void, u32),
    cursor: extern "C" fn(*mut c_void, StringView),
    ime: extern "C" fn(*mut c_void, *const ImeInfo),
    vk: extern "C" fn(*mut c_void, u8, u32),
    buffer: extern "C" fn(*mut c_void, *const BufferInfo),
    frame: extern "C" fn(*mut c_void, *const FrameInfo),
    reset: extern "C" fn(*mut c_void, u32, u64),
    recovered: extern "C" fn(*mut c_void, u32, u64),
    disconnect: extern "C" fn(*mut c_void),
}
// API v1 Windows x64 layout, checked independently by C++ static_asserts.
const _: () = {
    assert!(size_of::<HelloInfo>() == 24);
    assert!(size_of::<WindowInfo>() == 40);
    assert!(size_of::<ImeInfo>() == 88);
    assert!(size_of::<BufferInfo>() == 32);
    assert!(size_of::<FrameInfo>() == 160);
    assert!(size_of::<Pointer>() == 48);
    assert!(size_of::<Wheel>() == 32);
    assert!(size_of::<KeyInfo>() == 48);
    assert!(size_of::<Stats>() == 56);
    assert!(size_of::<Callbacks>() == 112);
};
type Native = c_void;
#[link(name = "kernel32")]
unsafe extern "system" {
    fn LoadLibraryExW(path: *const u16, file: *mut c_void, flags: u32) -> *mut c_void;
    fn AddDllDirectory(path: *const u16) -> *mut c_void;
    fn GetProcAddress(module: *mut c_void, name: *const c_char) -> *mut c_void;
    fn GetLastError() -> u32;
    fn GetHandleInformation(handle: *mut c_void, flags: *mut u32) -> i32;
}
pub(super) fn valid_handle(handle: BorrowedHandle<'_>) -> bool {
    let mut flags = 0;
    unsafe { GetHandleInformation(handle.as_raw_handle(), &mut flags) != 0 }
}

macro_rules! api {
    ($($name:ident : $ty:ty),* $(,)?) => {
        struct Api { $($name: $ty),* }
        impl Api {
            unsafe fn load(module: *mut c_void) -> Result<Self> {
                Ok(Self { $($name: {
                    let symbol = unsafe { GetProcAddress(module, concat!(stringify!($name), "\0").as_ptr().cast()) };
                    if symbol.is_null() { return Err(Error(concat!("missing symbol: ", stringify!($name)).into())); }
                    unsafe { std::mem::transmute::<*mut c_void, $ty>(symbol) }
                }),* })
            }
        }
    }
}
api! {
    re_native_abi_version: unsafe extern "C" fn() -> u32,
    re_mojo_api_version: unsafe extern "C" fn() -> u32,
    re_mojo_create: unsafe extern "C" fn(*const Callbacks, *mut c_void, *mut *mut Native) -> i32,
    re_mojo_launch: unsafe extern "C" fn(*mut Native, u32, *const *const c_char) -> i32,
    re_mojo_poll: unsafe extern "C" fn(*mut Native, u32) -> i32,
    re_mojo_start_capture: unsafe extern "C" fn(*mut Native, u32, u32, u8) -> i32,
    re_mojo_stop_capture: unsafe extern "C" fn(*mut Native, u32) -> i32,
    re_mojo_release_frame: unsafe extern "C" fn(*mut Native, u64, u8) -> i32,
    re_mojo_refresh: unsafe extern "C" fn(*mut Native, u32) -> i32,
    re_mojo_activate: unsafe extern "C" fn(*mut Native, u32) -> i32,
    re_mojo_pointer_input: unsafe extern "C" fn(*mut Native, u32, *const Pointer) -> i32,
    re_mojo_wheel_input: unsafe extern "C" fn(*mut Native, u32, *const Wheel) -> i32,
    re_mojo_key_input: unsafe extern "C" fn(*mut Native, u32, *const KeyInfo) -> i32,
    re_mojo_reset_input: unsafe extern "C" fn(*mut Native) -> i32,
    re_mojo_ime_commit: unsafe extern "C" fn(*mut Native, StringView, u32) -> i32,
    re_mojo_ime_compose: unsafe extern "C" fn(*mut Native, StringView, i32, u32) -> i32,
    re_mojo_ime_delete: unsafe extern "C" fn(*mut Native, i32, i32, u32) -> i32,
    re_mojo_focus: unsafe extern "C" fn(*mut Native, u8) -> i32,
    re_mojo_new_window: unsafe extern "C" fn(*mut Native, StringView) -> i32,
    re_mojo_set_viewport: unsafe extern "C" fn(*mut Native, Size, f32) -> i32,
    re_mojo_ime_hidden: unsafe extern "C" fn(*mut Native, u32) -> i32,
    re_mojo_touch_capabilities: unsafe extern "C" fn(*mut Native, u8, u8) -> i32,
    re_mojo_get_stats: unsafe extern "C" fn(*mut Native, *mut Stats) -> i32,
    re_mojo_now_us: unsafe extern "C" fn() -> i64,
    re_mojo_shutdown: unsafe extern "C" fn(*mut Native) -> i32,
    re_mojo_destroy: unsafe extern "C" fn(*mut Native),
}
fn check(status: i32) -> Result<()> {
    if status == 0 {
        Ok(())
    } else {
        Err(Error(format!("re_native Mojo status {status}")))
    }
}
struct Dispatch<F> {
    handler: F,
    panic: Option<Box<dyn Any + Send>>,
}
unsafe fn dispatch<F: for<'a> FnMut(Event<'a>) -> FrameAction>(
    ctx: *mut c_void,
    event: Event<'_>,
) -> FrameAction {
    let ctx = unsafe { &mut *ctx.cast::<Dispatch<F>>() };
    if ctx.panic.is_some() {
        return FrameAction::Abandon;
    }
    match catch_unwind(AssertUnwindSafe(|| (ctx.handler)(event))) {
        Ok(action) => action,
        Err(panic) => {
            ctx.panic = Some(panic);
            FrameAction::Abandon
        }
    }
}
struct State {
    dispatch: Option<for<'a> unsafe fn(*mut c_void, Event<'a>) -> FrameAction>,
    context: *mut c_void,
    owner: u64,
    native: *mut Native,
    release: unsafe extern "C" fn(*mut Native, u64, u8) -> i32,
    now: unsafe extern "C" fn() -> i64,
}
impl State {
    fn emit(&self, event: Event<'_>) -> FrameAction {
        self.dispatch
            .map_or(FrameAction::Abandon, |dispatch| unsafe {
                dispatch(self.context, event)
            })
    }
}
unsafe fn state<'a>(user: *mut c_void) -> &'a State {
    unsafe { &*user.cast::<State>() }
}
unsafe fn window<'a>(ptr: *const WindowInfo) -> Window<'a> {
    let w = unsafe { &*ptr };
    Window {
        window_id: w.id,
        size_dip: w.dip,
        device_scale_factor: w.dsf,
        size_px: w.px,
        title: unsafe { w.title.as_str() },
    }
}
extern "C" fn hello(user: *mut c_void, ptr: *const HelloInfo) {
    let h = unsafe { &*ptr };
    unsafe { state(user) }.emit(Event::Hello(Hello {
        version: h.version,
        gpu_luid: h.gpu_luid,
        software_compositing: h.software != 0,
    }));
}
extern "C" fn window_added(user: *mut c_void, ptr: *const WindowInfo) {
    unsafe { state(user) }.emit(Event::WindowAdded(unsafe { window(ptr) }));
}
extern "C" fn window_changed(user: *mut c_void, ptr: *const WindowInfo) {
    unsafe { state(user) }.emit(Event::WindowChanged(unsafe { window(ptr) }));
}
extern "C" fn window_removed(user: *mut c_void, id: u32) {
    unsafe { state(user) }.emit(Event::WindowRemoved(id));
}
extern "C" fn window_activated(user: *mut c_void, id: u32) {
    unsafe { state(user) }.emit(Event::WindowActivated(id));
}
extern "C" fn cursor(user: *mut c_void, css: StringView) {
    unsafe { state(user) }.emit(Event::Cursor(unsafe { css.as_str() }));
}
extern "C" fn ime(user: *mut c_void, ptr: *const ImeInfo) {
    let s = unsafe { &*ptr };
    unsafe { state(user) }.emit(Event::Ime(Ime {
        seq: s.seq,
        focused: s.focused != 0,
        input_type: unsafe { s.kind.as_str() },
        mode: unsafe { s.mode.as_str() },
        flags: s.flags,
        caret_norm: s.caret,
        reason: unsafe { s.reason.as_str() },
        composing: s.composing != 0,
        context: s.context,
    }));
}
extern "C" fn vk(user: *mut c_void, show: u8, seq: u32) {
    unsafe { state(user) }.emit(Event::VirtualKeyboard {
        show: show != 0,
        seq,
    });
}
extern "C" fn buffer(user: *mut c_void, ptr: *const BufferInfo) {
    let b = unsafe { &*ptr };
    unsafe { state(user) }.emit(Event::NewBuffer(Buffer {
        window_id: b.window,
        buffer_id: b.buffer,
        epoch: b.epoch,
        texture: unsafe { BorrowedHandle::borrow_raw(b.texture) },
        coded_size: b.size,
    }));
}
extern "C" fn frame(user: *mut c_void, ptr: *const FrameInfo) {
    let f = unsafe { &*ptr };
    let state = unsafe { state(user) };
    let rust_callback_us = unsafe { (state.now)() };
    let action = state.emit(Event::Frame(Frame {
        id: FrameId {
            value: f.lease,
            owner: state.owner,
        },
        window_id: f.window,
        buffer_id: f.buffer,
        epoch: f.epoch,
        frame_seq: f.seq,
        texture: unsafe { BorrowedHandle::borrow_raw(f.texture) },
        fence: (!f.fence.is_null()).then(|| unsafe { BorrowedHandle::borrow_raw(f.fence) }),
        fence_token: (f.token_high, f.token_low),
        fence_value: f.fence_value,
        adapter_luid: f.luid,
        coded_size: f.size,
        visible_rect: f.visible,
        content_rect: f.content,
        capture_begin_us: f.begin_us,
        capture_end_us: f.end_us,
        reference_time_us: f.reference_us,
        received_us: f.received_us,
        callback_us: f.callback_us,
        rust_callback_us,
    }));
    if action != FrameAction::Defer {
        unsafe {
            (state.release)(
                state.native,
                f.lease,
                u8::from(action == FrameAction::Consumed),
            );
        }
    }
}
extern "C" fn reset(user: *mut c_void, window_id: u32, epoch: u64) {
    unsafe { state(user) }.emit(Event::Reset { window_id, epoch });
}
extern "C" fn recovered(user: *mut c_void, window_id: u32, epoch: u64) {
    unsafe { state(user) }.emit(Event::Recovered { window_id, epoch });
}
extern "C" fn disconnect(user: *mut c_void) {
    unsafe { state(user) }.emit(Event::Disconnected);
}

pub(super) struct Link {
    api: Api,
    native: NonNull<Native>,
    state: Box<State>,
    _thread: PhantomData<Rc<Cell<()>>>,
}
impl Link {
    pub fn load(path: &Path) -> Result<Self> {
        let path = path
            .canonicalize()
            .map_err(|e| Error(format!("DLL path: {e}")))?;
        let wide: Vec<_> = path.as_os_str().encode_wide().chain(Some(0)).collect();
        let parent: Vec<_> = path
            .parent()
            .ok_or_else(|| Error("DLL path has no parent".into()))?
            .as_os_str()
            .encode_wide()
            .chain(Some(0))
            .collect();
        // Chromium delay-loaded component DLLs also need this directory. Keep
        // the cookie process-lifetime alongside the intentionally pinned DLL.
        if unsafe { AddDllDirectory(parent.as_ptr()) }.is_null() {
            return Err(Error(format!("AddDllDirectory: {}", unsafe {
                GetLastError()
            })));
        }
        // Process-lifetime module: never FreeLibrary while Chromium threads live.
        let module = unsafe { LoadLibraryExW(wide.as_ptr(), ptr::null_mut(), 0x1100) };
        if module.is_null() {
            return Err(Error(format!("LoadLibraryExW: {}", unsafe {
                GetLastError()
            })));
        }
        let api = unsafe { Api::load(module) }?;
        if unsafe { (api.re_native_abi_version)() } != 1
            || unsafe { (api.re_mojo_api_version)() } != 1
        {
            return Err(Error(
                "re_native ABI / Mojo API version mismatch (expected 1/1)".into(),
            ));
        }
        static NEXT_OWNER: AtomicU64 = AtomicU64::new(1);
        let mut state = Box::new(State {
            dispatch: None,
            context: ptr::null_mut(),
            owner: NEXT_OWNER.fetch_add(1, Ordering::Relaxed),
            native: ptr::null_mut(),
            release: api.re_mojo_release_frame,
            now: api.re_mojo_now_us,
        });
        let callbacks = Callbacks {
            version: 1,
            size: size_of::<Callbacks>() as u32,
            hello,
            window_added,
            window_changed,
            window_removed,
            window_activated,
            cursor,
            ime,
            vk,
            buffer,
            frame,
            reset,
            recovered,
            disconnect,
        };
        let mut native = ptr::null_mut();
        check(unsafe {
            (api.re_mojo_create)(&callbacks, (&mut *state as *mut State).cast(), &mut native)
        })?;
        state.native = native;
        Ok(Self {
            api,
            native: NonNull::new(native).ok_or_else(|| Error("null native client".into()))?,
            state,
            _thread: PhantomData,
        })
    }
    pub fn launch(&mut self, argv: &[String]) -> Result<()> {
        let args: Vec<_> = argv
            .iter()
            .map(|s| CString::new(s.as_str()).map_err(|_| Error("NUL in argv".into())))
            .collect::<Result<_>>()?;
        let ptrs: Vec<_> = args.iter().map(|s| s.as_ptr()).collect();
        let len = ptrs
            .len()
            .try_into()
            .map_err(|_| Error("too many arguments".into()))?;
        check(unsafe { (self.api.re_mojo_launch)(self.native.as_ptr(), len, ptrs.as_ptr()) })
    }
    pub fn poll<F: for<'a> FnMut(Event<'a>) -> FrameAction>(
        &mut self,
        timeout: Duration,
        handler: F,
    ) -> Result<()> {
        if timeout > Duration::from_secs(1) {
            return Err(Error("poll timeout exceeds 1 second".into()));
        }
        let mut dispatch_state = Dispatch {
            handler,
            panic: None,
        };
        self.state.context = (&mut dispatch_state as *mut Dispatch<F>).cast();
        self.state.dispatch = Some(dispatch::<F>);
        let result =
            unsafe { (self.api.re_mojo_poll)(self.native.as_ptr(), timeout.as_millis() as u32) };
        self.state.dispatch = None;
        self.state.context = ptr::null_mut();
        if let Some(panic) = dispatch_state.panic {
            resume_unwind(panic);
        }
        check(result)
    }
    pub fn start_capture(&mut self, window: u32, fps: u32, nv12: bool) -> Result<()> {
        check(unsafe {
            (self.api.re_mojo_start_capture)(self.native.as_ptr(), window, fps, nv12.into())
        })
    }
    pub fn stop_capture(&mut self, window: u32) -> Result<()> {
        check(unsafe { (self.api.re_mojo_stop_capture)(self.native.as_ptr(), window) })
    }
    pub fn release(&mut self, id: FrameId, consumed: bool) -> Result<()> {
        if id.owner != self.state.owner {
            return Err(Error("frame belongs to a different client".into()));
        }
        check(unsafe {
            (self.api.re_mojo_release_frame)(self.native.as_ptr(), id.value, consumed.into())
        })
    }
    pub fn refresh(&mut self, window: u32) -> Result<()> {
        check(unsafe { (self.api.re_mojo_refresh)(self.native.as_ptr(), window) })
    }
    pub fn activate(&mut self, window: u32) -> Result<()> {
        check(unsafe { (self.api.re_mojo_activate)(self.native.as_ptr(), window) })
    }
    pub fn pointer(&mut self, window: u32, input: &Pointer) -> Result<()> {
        check(unsafe { (self.api.re_mojo_pointer_input)(self.native.as_ptr(), window, input) })
    }
    pub fn wheel(&mut self, window: u32, input: &Wheel) -> Result<()> {
        check(unsafe { (self.api.re_mojo_wheel_input)(self.native.as_ptr(), window, input) })
    }
    pub fn key(&mut self, window: u32, input: &Key<'_>) -> Result<()> {
        let raw = KeyInfo {
            down: input.down.into(),
            mods: input.mods,
            repeat: input.repeat.into(),
            code_num: input.key_code,
            code: StringView::new(input.code)?,
            key: StringView::new(input.key)?,
            timestamp_us: input.timestamp_us,
        };
        check(unsafe { (self.api.re_mojo_key_input)(self.native.as_ptr(), window, &raw) })
    }
    pub fn reset_input(&mut self) -> Result<()> {
        check(unsafe { (self.api.re_mojo_reset_input)(self.native.as_ptr()) })
    }
    pub fn ime_commit(&mut self, text: &str, context: u32) -> Result<()> {
        check(unsafe {
            (self.api.re_mojo_ime_commit)(self.native.as_ptr(), StringView::new(text)?, context)
        })
    }
    pub fn ime_compose(&mut self, text: &str, cursor: i32, context: u32) -> Result<()> {
        check(unsafe {
            (self.api.re_mojo_ime_compose)(
                self.native.as_ptr(),
                StringView::new(text)?,
                cursor,
                context,
            )
        })
    }
    pub fn ime_delete(&mut self, before: i32, after: i32, context: u32) -> Result<()> {
        check(unsafe {
            (self.api.re_mojo_ime_delete)(self.native.as_ptr(), before, after, context)
        })
    }
    pub fn focus(&mut self, focused: bool) -> Result<()> {
        check(unsafe { (self.api.re_mojo_focus)(self.native.as_ptr(), focused.into()) })
    }
    pub fn new_window(&mut self, url: &str) -> Result<()> {
        check(unsafe { (self.api.re_mojo_new_window)(self.native.as_ptr(), StringView::new(url)?) })
    }
    pub fn viewport(&mut self, dip: Size, dsf: f32) -> Result<()> {
        check(unsafe { (self.api.re_mojo_set_viewport)(self.native.as_ptr(), dip, dsf) })
    }
    pub fn ime_hidden(&mut self, context: u32) -> Result<()> {
        check(unsafe { (self.api.re_mojo_ime_hidden)(self.native.as_ptr(), context) })
    }
    pub fn touch(&mut self, points: u8, only: bool) -> Result<()> {
        check(unsafe {
            (self.api.re_mojo_touch_capabilities)(self.native.as_ptr(), points, only.into())
        })
    }
    pub fn stats(&self) -> Result<Stats> {
        let mut stats = Stats::default();
        check(unsafe { (self.api.re_mojo_get_stats)(self.native.as_ptr(), &mut stats) })?;
        Ok(stats)
    }
    pub fn now_us(&self) -> i64 {
        unsafe { (self.api.re_mojo_now_us)() }
    }
    pub fn shutdown(&mut self) -> Result<()> {
        check(unsafe { (self.api.re_mojo_shutdown)(self.native.as_ptr()) })
    }
}
impl Drop for Link {
    fn drop(&mut self) {
        unsafe {
            (self.api.re_mojo_destroy)(self.native.as_ptr());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn callback_panic_stays_inside_dispatch() {
        let mut calls = 0;
        let mut callback = Dispatch {
            handler: |_: Event<'_>| -> FrameAction {
                calls += 1;
                panic!("callback panic fixture")
            },
            panic: None,
        };
        fn invoke<F: for<'a> FnMut(Event<'a>) -> FrameAction>(
            callback: &mut Dispatch<F>,
        ) -> FrameAction {
            unsafe { dispatch::<F>((callback as *mut Dispatch<F>).cast(), Event::Disconnected) }
        }
        assert_eq!(invoke(&mut callback), FrameAction::Abandon);
        assert!(callback.panic.is_some());
        assert_eq!(invoke(&mut callback), FrameAction::Abandon);
        drop(callback);
        assert_eq!(calls, 1);
    }
}
