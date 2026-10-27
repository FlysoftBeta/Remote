// Hand-written bindings to re/native/include/re_native_rtc.h, ABI v1.
// All unsafe code, borrowed-pointer handling and DLL lifetime live here.
use crate::{Config, EncodedFrame, Error, Event, Result};
use libloading::Library;
use std::{
    ffi::{CStr, CString, c_char, c_void},
    marker::PhantomData,
    mem::{ManuallyDrop, size_of},
    path::{Path, PathBuf},
    ptr,
    rc::Rc,
    sync::{
        Arc, Mutex, OnceLock,
        atomic::{AtomicBool, Ordering},
        mpsc::{self, Receiver, RecvTimeoutError, SyncSender},
    },
    time::Duration,
};
const ABI: u32 = 1;

#[repr(C)]
struct RawConfig {
    struct_size: u32,
    abi_version: u32,
    min_kbps: i32,
    start_kbps: i32,
    max_kbps: i32,
    fps: i32,
    playout_delay: i32,
    relay_only: i32,
    field_trials: *const c_char,
    ice_servers_json: *const c_char,
}
#[repr(C)]
struct RawFrame {
    struct_size: u32,
    abi_version: u32,
    annex_b: *const u8,
    size: usize,
    timestamp_us: i64,
    capture_begin_us: i64,
    capture_end_us: i64,
    encode_begin_us: i64,
    encode_end_us: i64,
    width: u32,
    height: u32,
    keyframe: i32,
}
#[repr(C)]
struct Callbacks {
    struct_size: u32,
    abi_version: u32,
    user: *mut c_void,
    on_sdp: extern "C" fn(*mut c_void, *const c_char, *const c_char),
    on_ice: extern "C" fn(*mut c_void, *const c_char, i32, *const c_char),
    on_state: extern "C" fn(*mut c_void, i32),
    on_data: extern "C" fn(*mut c_void, i32, *const u8, usize, i32, f64, i64),
    on_channel_state: extern "C" fn(*mut c_void, i32, i32),
    on_rates: extern "C" fn(*mut c_void, u32, f64),
    on_keyframe: extern "C" fn(*mut c_void),
    on_stats: extern "C" fn(*mut c_void, *const c_char),
    on_error: extern "C" fn(*mut c_void, *const c_char),
    on_frame: extern "C" fn(*mut c_void, i64, u32, i64, usize, i32),
}
type Handle = *mut c_void;
struct Api {
    // Do not unload: Mojo embedder/runtime queues are process lifetime.
    _library: ManuallyDrop<Library>,
    #[cfg(windows)]
    _search_directory: ManuallyDrop<DllDirectory>,
    create: unsafe extern "C" fn(*const RawConfig, *const Callbacks, *mut Handle) -> i32,
    destroy: unsafe extern "C" fn(Handle),
    offer: unsafe extern "C" fn(Handle, i32) -> i32,
    answer: unsafe extern "C" fn(Handle) -> i32,
    remote: unsafe extern "C" fn(Handle, *const c_char, *const c_char) -> i32,
    ice: unsafe extern "C" fn(Handle, *const c_char, i32, *const c_char) -> i32,
    submit: unsafe extern "C" fn(Handle, *const RawFrame) -> i32,
    channel: unsafe extern "C" fn(Handle, i32, *const c_char, i32, i32) -> i32,
    send: unsafe extern "C" fn(Handle, i32, *const u8, usize, i32) -> i32,
    keyframe: unsafe extern "C" fn(Handle) -> i32,
    stats: unsafe extern "C" fn(Handle) -> i32,
    now: unsafe extern "C" fn() -> i64,
    wall: unsafe extern "C" fn() -> f64,
}
#[cfg(windows)]
#[link(name = "kernel32")]
unsafe extern "system" {
    fn AddDllDirectory(path: *const u16) -> *mut c_void;
    fn RemoveDllDirectory(cookie: *mut c_void) -> i32;
}
#[cfg(windows)]
struct DllDirectory(usize);
#[cfg(windows)]
impl DllDirectory {
    fn add(path: &Path) -> Result<Self> {
        use std::os::windows::ffi::OsStrExt;
        let parent = path
            .parent()
            .ok_or_else(|| Error("DLL has no parent directory".into()))?;
        let wide: Vec<u16> = parent.as_os_str().encode_wide().chain(Some(0)).collect();
        // Some Chromium component DLLs load further modules during process
        // attachment. Their nested LoadLibrary calls need the configured
        // directory registered as well as DLL_LOAD_DIR on the top-level load.
        let cookie = unsafe { AddDllDirectory(wide.as_ptr()) };
        if cookie.is_null() {
            return Err(Error(format!(
                "AddDllDirectory: {}",
                std::io::Error::last_os_error()
            )));
        }
        Ok(Self(cookie as usize))
    }
}
#[cfg(windows)]
impl Drop for DllDirectory {
    fn drop(&mut self) {
        unsafe { RemoveDllDirectory(self.0 as *mut c_void) };
    }
}
fn string(s: &str) -> Result<CString> {
    CString::new(s).map_err(|_| Error("interior NUL in string".into()))
}
fn check(code: i32) -> Result<()> {
    if code == 0 {
        return Ok(());
    }
    let reason = match code {
        -1 => "invalid argument",
        -2 => "invalid state",
        -3 => "native failure (see error event)",
        -4 => "not open or backpressure",
        -5 => "ABI mismatch",
        _ => "unknown native error",
    };
    Err(Error(format!("{reason} ({code})")))
}
impl Api {
    fn shared(path: &Path) -> Result<Arc<Self>> {
        type Loaded = Option<(PathBuf, Arc<Api>)>;
        static LOADED: OnceLock<Mutex<Loaded>> = OnceLock::new();
        if !path.is_absolute() {
            return Err(Error("RE_NATIVE_DLL must be an absolute path".into()));
        }
        let path = path
            .canonicalize()
            .map_err(|e| Error(format!("DLL path: {e}")))?;
        let mut loaded = LOADED
            .get_or_init(|| Mutex::new(None))
            .lock()
            .map_err(|_| Error("native loader lock poisoned".into()))?;
        if let Some((existing, api)) = &*loaded {
            if *existing != path {
                return Err(Error(
                    "one re_native DLL path per process is supported".into(),
                ));
            }
            return Ok(Arc::clone(api));
        }
        let api = Arc::new(Self::load(&path)?);
        *loaded = Some((path, Arc::clone(&api)));
        Ok(api)
    }
    fn load(path: &Path) -> Result<Self> {
        if !path.is_absolute() {
            return Err(Error("RE_NATIVE_DLL must be an absolute path".into()));
        }
        let path = path
            .canonicalize()
            .map_err(|e| Error(format!("DLL path: {e}")))?;
        #[cfg(windows)]
        let directory = DllDirectory::add(&path)?;
        // SAFETY: this is the explicitly configured native shim. Check the
        // version before using any struct ABI. Symbol types match the header.
        unsafe {
            #[cfg(windows)]
            let library: Library = libloading::os::windows::Library::load_with_flags(
                &path,
                libloading::os::windows::LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
                    | libloading::os::windows::LOAD_LIBRARY_SEARCH_DEFAULT_DIRS,
            )
            .map_err(|e| Error(format!("load {}: {e:?}", path.display())))?
            .into();
            #[cfg(not(windows))]
            let library =
                Library::new(path).map_err(|e| Error(format!("load {}: {e:?}", path.display())))?;
            let abi = library
                .get::<unsafe extern "C" fn() -> u32>(b"re_native_abi_version\0")
                .map_err(|e| Error(e.to_string()))?;
            if abi() != ABI {
                return Err(Error("re_native ABI mismatch".into()));
            }
            macro_rules! symbol {
                ($name:literal) => {
                    *library
                        .get(concat!($name, "\0").as_bytes())
                        .map_err(|e| Error(e.to_string()))?
                };
            }
            Ok(Self {
                create: symbol!("re_rtc_create"),
                destroy: symbol!("re_rtc_destroy"),
                offer: symbol!("re_rtc_offer"),
                answer: symbol!("re_rtc_answer"),
                remote: symbol!("re_rtc_set_remote_description"),
                ice: symbol!("re_rtc_add_ice"),
                submit: symbol!("re_rtc_submit"),
                channel: symbol!("re_rtc_create_channel"),
                send: symbol!("re_rtc_send"),
                keyframe: symbol!("re_rtc_request_keyframe"),
                stats: symbol!("re_rtc_stats"),
                now: symbol!("re_rtc_now_us"),
                wall: symbol!("re_rtc_wall_ms"),
                _library: ManuallyDrop::new(library),
                #[cfg(windows)]
                _search_directory: ManuallyDrop::new(directory),
            })
        }
    }
}
struct Sink {
    tx: SyncSender<Event>,
    overflow: AtomicBool,
}
fn emit(user: *mut c_void, event: Event) {
    // SAFETY: user points to Native.sink, boxed before create; destroy joins
    // every native callback before the box is freed. Sink is thread-safe.
    let sink = unsafe { &*user.cast::<Sink>() };
    if sink.tx.try_send(event).is_err() {
        sink.overflow.store(true, Ordering::Release);
    }
}
fn text(p: *const c_char) -> String {
    if p.is_null() {
        return String::new();
    }
    // SAFETY: shim callback contracts guarantee a borrowed NUL-terminated UTF-8
    // string for the duration of this call. We copy before returning.
    unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned()
}
extern "C" fn sdp(u: *mut c_void, kind: *const c_char, value: *const c_char) {
    emit(
        u,
        Event::Sdp {
            kind: text(kind),
            sdp: text(value),
        },
    );
}
extern "C" fn ice(u: *mut c_void, mid: *const c_char, mline: i32, c: *const c_char) {
    emit(
        u,
        Event::Ice {
            mid: text(mid),
            mline,
            candidate: if c.is_null() { None } else { Some(text(c)) },
        },
    );
}
extern "C" fn state(u: *mut c_void, value: i32) {
    emit(u, Event::State(value));
}
extern "C" fn data(
    u: *mut c_void,
    id: i32,
    p: *const u8,
    n: usize,
    binary: i32,
    wall: f64,
    ticks: i64,
) {
    // SAFETY: native OnMessage holds the DataBuffer throughout the callback;
    // shim caps the size at 64 KiB. Empty buffers need no pointer dereference.
    let bytes = if n == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(p, n) }.to_vec()
    };
    emit(
        u,
        Event::Data {
            id,
            bytes,
            binary: binary != 0,
            received_wall_ms: wall,
            received_ticks_us: ticks,
        },
    );
}
extern "C" fn channel(u: *mut c_void, id: i32, state: i32) {
    emit(u, Event::ChannelState { id, state });
}
extern "C" fn rates(u: *mut c_void, bitrate_bps: u32, fps: f64) {
    emit(u, Event::Rates { bitrate_bps, fps });
}
extern "C" fn keyframe(u: *mut c_void) {
    emit(u, Event::Keyframe);
}
extern "C" fn stats(u: *mut c_void, p: *const c_char) {
    emit(u, Event::Stats(text(p)));
}
extern "C" fn error(u: *mut c_void, p: *const c_char) {
    emit(u, Event::Error(text(p)));
}
extern "C" fn frame(
    u: *mut c_void,
    capture_us: i64,
    rtp_timestamp: u32,
    handoff_us: i64,
    bytes: usize,
    key: i32,
) {
    emit(
        u,
        Event::Frame {
            capture_us,
            rtp_timestamp,
            handoff_us,
            bytes,
            keyframe: key != 0,
        },
    );
}

pub(super) struct Native {
    api: Arc<Api>,
    handle: Handle,
    sink: Box<Sink>,
    rx: Receiver<Event>,
    _thread: PhantomData<Rc<()>>,
}
impl Native {
    pub fn load(path: &Path, config: Config) -> Result<Self> {
        let api = Api::shared(path)?;
        let (tx, rx) = mpsc::sync_channel(2048);
        let mut sink = Box::new(Sink {
            tx,
            overflow: AtomicBool::new(false),
        });
        let callbacks = Callbacks {
            struct_size: size_of::<Callbacks>() as u32,
            abi_version: ABI,
            user: ptr::from_mut(&mut *sink).cast(),
            on_sdp: sdp,
            on_ice: ice,
            on_state: state,
            on_data: data,
            on_channel_state: channel,
            on_rates: rates,
            on_keyframe: keyframe,
            on_stats: stats,
            on_error: error,
            on_frame: frame,
        };
        let trials = config.field_trials.as_deref().map(string).transpose()?;
        let servers = string(&config.ice_servers_json)?;
        let raw = RawConfig {
            struct_size: size_of::<RawConfig>() as u32,
            abi_version: ABI,
            min_kbps: config.min_kbps,
            start_kbps: config.start_kbps,
            max_kbps: config.max_kbps,
            fps: config.fps,
            playout_delay: config.playout_delay.into(),
            relay_only: config.relay_only.into(),
            field_trials: trials.as_ref().map_or(ptr::null(), |s| s.as_ptr()),
            ice_servers_json: servers.as_ptr(),
        };
        let mut handle = ptr::null_mut();
        // SAFETY: structs exactly match ABI, strings/box outlive call; shim
        // copies config and callbacks. Failed create quiesces before returning.
        if let Err(mut error) = check(unsafe { (api.create)(&raw, &callbacks, &mut handle) }) {
            for event in rx.try_iter() {
                if let Event::Error(message) = event {
                    error.0.push_str(": ");
                    error.0.push_str(&message);
                }
            }
            return Err(error);
        }
        if handle.is_null() {
            return Err(Error("create returned null".into()));
        }
        Ok(Self {
            api,
            handle,
            sink,
            rx,
            _thread: PhantomData,
        })
    }
    pub fn offer(&mut self, restart: bool) -> Result<()> {
        check(unsafe { (self.api.offer)(self.handle, restart.into()) })
    }
    pub fn answer(&mut self) -> Result<()> {
        check(unsafe { (self.api.answer)(self.handle) })
    }
    pub fn remote(&mut self, kind: &str, sdp: &str) -> Result<()> {
        let kind = string(kind)?;
        let sdp = string(sdp)?;
        check(unsafe { (self.api.remote)(self.handle, kind.as_ptr(), sdp.as_ptr()) })
    }
    pub fn ice(&mut self, mid: &str, mline: i32, candidate: Option<&str>) -> Result<()> {
        let mid = string(mid)?;
        let c = candidate.map(string).transpose()?;
        check(unsafe {
            (self.api.ice)(
                self.handle,
                mid.as_ptr(),
                mline,
                c.as_ref().map_or(ptr::null(), |s| s.as_ptr()),
            )
        })
    }
    pub fn submit(&mut self, f: EncodedFrame<'_>) -> Result<()> {
        let raw = RawFrame {
            struct_size: size_of::<RawFrame>() as u32,
            abi_version: ABI,
            annex_b: f.annex_b.as_ptr(),
            size: f.annex_b.len(),
            timestamp_us: f.timestamp_us,
            capture_begin_us: f.timing.capture_begin_us,
            capture_end_us: f.timing.capture_end_us,
            encode_begin_us: f.timing.encode_begin_us,
            encode_end_us: f.timing.encode_end_us,
            width: f.width,
            height: f.height,
            keyframe: f.keyframe.into(),
        };
        check(unsafe { (self.api.submit)(self.handle, &raw) })
    }
    pub fn channel(
        &mut self,
        id: i32,
        label: &str,
        ordered: bool,
        retransmits: Option<u16>,
    ) -> Result<()> {
        let label = string(label)?;
        check(unsafe {
            (self.api.channel)(
                self.handle,
                id,
                label.as_ptr(),
                ordered.into(),
                retransmits.map_or(-1, i32::from),
            )
        })
    }
    pub fn send(&mut self, id: i32, bytes: &[u8], binary: bool) -> Result<()> {
        check(unsafe {
            (self.api.send)(self.handle, id, bytes.as_ptr(), bytes.len(), binary.into())
        })
    }
    pub fn keyframe(&mut self) -> Result<()> {
        check(unsafe { (self.api.keyframe)(self.handle) })
    }
    pub fn stats(&mut self) -> Result<()> {
        check(unsafe { (self.api.stats)(self.handle) })
    }
    pub fn now_us(&self) -> i64 {
        unsafe { (self.api.now)() }
    }
    pub fn wall_ms(&self) -> f64 {
        unsafe { (self.api.wall)() }
    }
    pub fn poll(&mut self, timeout: Duration) -> Result<Option<Event>> {
        if self.sink.overflow.load(Ordering::Acquire) {
            return Err(Error("RTC event queue overflow; close session".into()));
        }
        match self.rx.recv_timeout(timeout) {
            Ok(e) => Ok(Some(e)),
            Err(RecvTimeoutError::Timeout) => Ok(None),
            Err(RecvTimeoutError::Disconnected) => Err(Error("RTC callbacks disconnected".into())),
        }
    }
}
impl Drop for Native {
    fn drop(&mut self) {
        // SAFETY: thread-affine owner, no calls may race with drop. Native
        // destroy joins encoder/signaling callbacks before sink is released.
        unsafe { (self.api.destroy)(self.handle) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn abi_v1_layout_matches_windows_x64_header() {
        assert_eq!(size_of::<RawConfig>(), 48);
        assert_eq!(size_of::<RawFrame>(), 80);
        assert_eq!(size_of::<Callbacks>(), 96);
        assert_eq!(std::mem::offset_of!(RawFrame, timestamp_us), 24);
        assert_eq!(std::mem::offset_of!(Callbacks, on_frame), 88);
    }
}
