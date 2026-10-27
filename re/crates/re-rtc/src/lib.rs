//! Safe, dynamically loaded C ABI for the pinned official libwebrtc shim.
//!
//! No GN or bindgen runs during Cargo builds. Keep a session on its creating
//! thread. Callbacks are copied into a bounded event queue; drain it promptly.
//! The DLL stays loaded for process lifetime, as required by the Mojo embedder.
mod ffi;

use std::{path::Path, time::Duration};

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Debug)]
pub struct Error(pub String);
impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}
impl std::error::Error for Error {}

#[derive(Clone, Debug)]
pub struct Config {
    pub min_kbps: i32,
    pub start_kbps: i32,
    pub max_kbps: i32,
    pub fps: i32,
    pub playout_delay: bool,
    pub relay_only: bool,
    /// None selects the tuned pacing factor 2.5. Some("") disables overrides.
    pub field_trials: Option<String>,
    pub ice_servers_json: String,
}
impl Default for Config {
    fn default() -> Self {
        Self {
            min_kbps: 2000,
            start_kbps: 8000,
            max_kbps: 8000,
            fps: 30,
            playout_delay: true,
            relay_only: false,
            field_trials: None,
            ice_servers_json: "[]".into(),
        }
    }
}

#[derive(Clone, Debug)]
pub enum Event {
    Sdp {
        kind: String,
        sdp: String,
    },
    Ice {
        mid: String,
        mline: i32,
        candidate: Option<String>,
    },
    State(i32),
    Data {
        id: i32,
        bytes: Vec<u8>,
        binary: bool,
        received_wall_ms: f64,
        received_ticks_us: i64,
    },
    ChannelState {
        id: i32,
        state: i32,
    },
    Rates {
        bitrate_bps: u32,
        fps: f64,
    },
    Keyframe,
    Stats(String),
    Error(String),
    Frame {
        capture_us: i64,
        rtp_timestamp: u32,
        handoff_us: i64,
        bytes: usize,
        keyframe: bool,
    },
}

#[derive(Clone, Copy, Debug, Default)]
pub struct FrameTiming {
    pub capture_begin_us: i64,
    pub capture_end_us: i64,
    pub encode_begin_us: i64,
    pub encode_end_us: i64,
}
#[derive(Clone, Copy, Debug)]
pub struct EncodedFrame<'a> {
    pub annex_b: &'a [u8],
    pub timestamp_us: i64,
    pub width: u32,
    pub height: u32,
    pub keyframe: bool,
    pub timing: FrameTiming,
}

/// Thread-affine session. `load` accepts an absolute re_native.dll path, or
/// `from_env` reads RE_NATIVE_DLL. Windows loads dependencies from that DLL's
/// directory and the system directories; PATH modifications are unnecessary.
pub struct Session(ffi::Native);
impl Session {
    pub fn load(path: impl AsRef<Path>, config: Config) -> Result<Self> {
        ffi::Native::load(path.as_ref(), config).map(Self)
    }
    pub fn from_env(config: Config) -> Result<Self> {
        let path = std::env::var_os("RE_NATIVE_DLL")
            .ok_or_else(|| Error("set RE_NATIVE_DLL to the built re_native.dll".into()))?;
        Self::load(Path::new(&path), config)
    }
    pub fn offer(&mut self, restart: bool) -> Result<()> {
        self.0.offer(restart)
    }
    pub fn answer(&mut self) -> Result<()> {
        self.0.answer()
    }
    pub fn set_remote_description(&mut self, kind: &str, sdp: &str) -> Result<()> {
        self.0.remote(kind, sdp)
    }
    pub fn add_ice(&mut self, mid: &str, mline: i32, candidate: Option<&str>) -> Result<()> {
        self.0.ice(mid, mline, candidate)
    }
    /// Copies one complete access unit; timestamps use `now_us`'s clock.
    pub fn submit(&mut self, frame: EncodedFrame<'_>) -> Result<()> {
        self.0.submit(frame)
    }
    pub fn create_channel(
        &mut self,
        id: i32,
        label: &str,
        ordered: bool,
        max_retransmits: Option<u16>,
    ) -> Result<()> {
        self.0.channel(id, label, ordered, max_retransmits)
    }
    pub fn send(&mut self, id: i32, bytes: &[u8], binary: bool) -> Result<()> {
        self.0.send(id, bytes, binary)
    }
    pub fn request_keyframe(&mut self) -> Result<()> {
        self.0.keyframe()
    }
    pub fn request_stats(&mut self) -> Result<()> {
        self.0.stats()
    }
    /// Queue overflow is fatal: returns Err instead of silently losing SDP/ICE.
    pub fn poll(&mut self, timeout: Duration) -> Result<Option<Event>> {
        self.0.poll(timeout)
    }
    pub fn now_us(&self) -> i64 {
        self.0.now_us()
    }
    pub fn wall_ms(&self) -> f64 {
        self.0.wall_ms()
    }
}
