//! rb.mojom through the versioned re_native C ABI. Cargo never runs GN.
//! A Client stays on its creation thread; callbacks run only inside `poll`.
//! Frame/handle borrows cannot escape a callback. Import resources there and
//! return Defer if GPU reads remain; call release_frame after GPU completion.
//! Reset/disconnect invalidates every outstanding lease in that capture.
#![cfg(windows)]
#![deny(unsafe_code)]

#[allow(unsafe_code)]
mod ffi;

pub use ffi::{Pointer, Rect, RectF, Size, Stats, Wheel};
use std::os::windows::io::BorrowedHandle;
use std::{fmt, path::Path, time::Duration};

#[derive(Debug)]
pub struct Error(pub(crate) String);
impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.fmt(f)
    }
}
impl std::error::Error for Error {}
pub type Result<T> = std::result::Result<T, Error>;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FrameId {
    pub(crate) value: u64,
    pub(crate) owner: u64,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FrameAction {
    /// No GPU read remains (also correct when dropping without reading).
    Consumed,
    /// A failed/timed out GPU read may remain; retire the allocation.
    Abandon,
    /// Keep the lease, then explicitly release it using Frame::id.
    Defer,
}
#[derive(Debug)]
pub struct Hello {
    pub version: u32,
    pub gpu_luid: u64,
    pub software_compositing: bool,
}
#[derive(Debug)]
pub struct Window<'a> {
    pub window_id: u32,
    pub size_dip: Size,
    pub device_scale_factor: f32,
    pub size_px: Size,
    pub title: &'a str,
}
#[derive(Debug)]
pub struct Ime<'a> {
    pub seq: u32,
    pub focused: bool,
    pub input_type: &'a str,
    pub mode: &'a str,
    pub flags: u32,
    pub caret_norm: RectF,
    pub reason: &'a str,
    pub composing: bool,
    pub context: u32,
}
#[derive(Debug)]
pub struct Buffer<'a> {
    pub window_id: u32,
    pub buffer_id: u32,
    pub epoch: u64,
    pub texture: BorrowedHandle<'a>,
    pub coded_size: Size,
}
#[derive(Debug)]
pub struct Frame<'a> {
    pub id: FrameId,
    pub window_id: u32,
    pub buffer_id: u32,
    pub epoch: u64,
    pub frame_seq: u64,
    pub texture: BorrowedHandle<'a>,
    pub fence: Option<BorrowedHandle<'a>>,
    pub fence_token: (u64, u64),
    pub fence_value: u64,
    pub adapter_luid: u64,
    pub coded_size: Size,
    pub visible_rect: Rect,
    pub content_rect: Rect,
    pub capture_begin_us: i64,
    pub capture_end_us: i64,
    pub reference_time_us: i64,
    pub received_us: i64,
    pub callback_us: i64,
    pub rust_callback_us: i64,
}
impl Frame<'_> {
    /// Checks that both borrowed NT handles are valid in this process.
    pub fn handles_valid(&self) -> bool {
        ffi::valid_handle(self.texture) && self.fence.is_none_or(ffi::valid_handle)
    }
}
#[derive(Debug)]
pub enum Event<'a> {
    Hello(Hello),
    WindowAdded(Window<'a>),
    WindowChanged(Window<'a>),
    WindowRemoved(u32),
    WindowActivated(u32),
    Cursor(&'a str),
    Ime(Ime<'a>),
    VirtualKeyboard { show: bool, seq: u32 },
    NewBuffer(Buffer<'a>),
    Frame(Frame<'a>),
    Reset { window_id: u32, epoch: u64 },
    Recovered { window_id: u32, epoch: u64 },
    Disconnected,
}
#[derive(Debug)]
pub struct Key<'a> {
    pub down: bool,
    pub mods: u8,
    pub repeat: bool,
    pub key_code: u16,
    pub code: &'a str,
    pub key: &'a str,
    pub timestamp_us: i64,
}

/// Owns the launched browser, invitation and capture leases. !Send + !Sync.
/// The DLL remains loaded until process exit; common/Mojo runtime is shared.
///
/// Borrowed frame handles cannot escape `poll`:
/// ```compile_fail
/// use re_mojo::{Client, Event, FrameAction};
/// use std::time::Duration;
/// fn retain(client: &mut Client) {
///     let mut handle = None;
///     client.poll(Duration::ZERO, |event| {
///         if let Event::Frame(frame) = event { handle = Some(frame.texture); }
///         FrameAction::Consumed
///     }).unwrap();
/// }
/// ```
pub struct Client {
    inner: ffi::Link,
}
impl Client {
    /// Loads an absolute DLL path and checks both common ABI and Mojo version.
    pub fn load(dll: impl AsRef<Path>) -> Result<Self> {
        Ok(Self {
            inner: ffi::Link::load(dll.as_ref())?,
        })
    }
    /// argv[0] must be chrome.exe; supply a dedicated --user-data-dir argument.
    pub fn launch(&mut self, argv: &[String]) -> Result<()> {
        self.inner.launch(argv)
    }
    /// Polls for at most 1000 ms. A panic in the callback is caught before the
    /// C boundary, the frame abandoned, and resumed after poll returns.
    /// FrameAction is ignored for events other than Frame.
    pub fn poll(
        &mut self,
        timeout: Duration,
        handler: impl for<'a> FnMut(Event<'a>) -> FrameAction,
    ) -> Result<()> {
        self.inner.poll(timeout, handler)
    }
    pub fn start_capture(&mut self, window: u32, fps: u32, nv12: bool) -> Result<()> {
        self.inner.start_capture(window, fps, nv12)
    }
    pub fn stop_capture(&mut self, window: u32) -> Result<()> {
        self.inner.stop_capture(window)
    }
    pub fn release_frame(&mut self, id: FrameId, consumed: bool) -> Result<()> {
        self.inner.release(id, consumed)
    }
    pub fn refresh(&mut self, window: u32) -> Result<()> {
        self.inner.refresh(window)
    }
    pub fn activate(&mut self, window: u32) -> Result<()> {
        self.inner.activate(window)
    }
    pub fn pointer(&mut self, window: u32, input: &Pointer) -> Result<()> {
        self.inner.pointer(window, input)
    }
    pub fn wheel(&mut self, window: u32, input: &Wheel) -> Result<()> {
        self.inner.wheel(window, input)
    }
    pub fn key(&mut self, window: u32, input: &Key<'_>) -> Result<()> {
        self.inner.key(window, input)
    }
    pub fn reset_input(&mut self) -> Result<()> {
        self.inner.reset_input()
    }
    pub fn ime_commit(&mut self, text: &str, context: u32) -> Result<()> {
        self.inner.ime_commit(text, context)
    }
    pub fn ime_compose(&mut self, text: &str, cursor: i32, context: u32) -> Result<()> {
        self.inner.ime_compose(text, cursor, context)
    }
    pub fn ime_delete(&mut self, before: i32, after: i32, context: u32) -> Result<()> {
        self.inner.ime_delete(before, after, context)
    }
    pub fn focus(&mut self, focused: bool) -> Result<()> {
        self.inner.focus(focused)
    }
    pub fn new_window(&mut self, url: &str) -> Result<()> {
        self.inner.new_window(url)
    }
    pub fn set_viewport(&mut self, dip: Size, dsf: f32) -> Result<()> {
        self.inner.viewport(dip, dsf)
    }
    pub fn ime_hidden(&mut self, context: u32) -> Result<()> {
        self.inner.ime_hidden(context)
    }
    pub fn touch_capabilities(&mut self, points: u8, touch_only: bool) -> Result<()> {
        self.inner.touch(points, touch_only)
    }
    pub fn stats(&self) -> Result<Stats> {
        self.inner.stats()
    }
    pub fn now_us(&self) -> i64 {
        self.inner.now_us()
    }
    pub fn shutdown(&mut self) -> Result<()> {
        self.inner.shutdown()
    }
}
