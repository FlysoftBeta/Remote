use crate::{Result, config::Config, platform::now_us};
use re_encode::{d3d::D3dDevice, *};
use re_mojo::FrameId;
use std::{
    sync::{
        Arc, Condvar, Mutex,
        atomic::{AtomicBool, Ordering},
        mpsc::{self, Receiver, Sender, SyncSender},
    },
    thread::JoinHandle,
};
use windows::{Win32::Graphics::Dxgi::IDXGIKeyedMutex, core::Interface};

pub struct Lease {
    pub id: FrameId,
    pub consumed: AtomicBool,
    pub keyed: Mutex<Option<IDXGIKeyedMutex>>,
    pub release: Sender<(FrameId, bool)>,
}
impl Drop for Lease {
    fn drop(&mut self) {
        let mut consumed = self.consumed.load(Ordering::Relaxed);
        if let Some(m) = self.keyed.get_mut().unwrap().take() {
            consumed &= unsafe { m.ReleaseSync(0) }.is_ok();
        }
        let _ = self.release.send((self.id, consumed));
    }
}
pub struct Raw {
    pub gpu: GpuFrame,
    pub lease: Arc<Lease>,
    pub timing: re_rtc::FrameTiming,
    pub generation: u64,
    pub input_seq: u16,
    pub received_us: i64,
}
pub struct Encoded {
    pub au: AccessUnit,
    pub timing: re_rtc::FrameTiming,
    pub generation: u64,
    pub width: u32,
    pub height: u32,
    pub backend: Backend,
    pub diagnostics: String,
}
struct Mailbox {
    raw: Option<Raw>,
    generation: u64,
    bitrate: u32,
    keyframe: bool,
    stop: bool,
}
pub struct Worker {
    shared: Arc<(Mutex<Mailbox>, Condvar)>,
    pub output: Receiver<Result<Encoded>>,
    thread: Option<JoinHandle<()>>,
}
impl Worker {
    pub fn start(config: Config, device: D3dDevice) -> Self {
        let shared = Arc::new((
            Mutex::new(Mailbox {
                raw: None,
                generation: 1,
                bitrate: config.params.target_bitrate,
                keyframe: true,
                stop: false,
            }),
            Condvar::new(),
        ));
        let (tx, output) = mpsc::sync_channel(8);
        let state = shared.clone();
        let thread = std::thread::Builder::new()
            .name("re-encode".into())
            .spawn(move || {
                if let Err(e) = run(&config, device, &state, &tx) {
                    let _ = tx.send(Err(e));
                }
            })
            .expect("encoding thread");
        Self {
            shared,
            output,
            thread: Some(thread),
        }
    }
    pub fn push(&self, raw: Raw) {
        let (lock, ready) = &*self.shared;
        let mut m = lock.lock().unwrap();
        m.raw = Some(raw);
        ready.notify_one();
    }
    pub fn reset(&self, generation: u64) {
        let (lock, ready) = &*self.shared;
        let mut m = lock.lock().unwrap();
        m.generation = generation;
        m.raw = None;
        m.keyframe = true;
        ready.notify_one();
    }
    pub fn rates(&self, bitrate: u32) {
        self.shared.0.lock().unwrap().bitrate = bitrate;
    }
    pub fn keyframe(&self) {
        self.shared.0.lock().unwrap().keyframe = true;
    }
}
impl Drop for Worker {
    fn drop(&mut self) {
        let (lock, ready) = &*self.shared;
        {
            let mut m = lock.lock().unwrap();
            m.stop = true;
            m.raw = None;
        }
        ready.notify_one();
        // Drain encoded output while joining: a full output queue cannot deadlock shutdown.
        if let Some(t) = self.thread.take() {
            while !t.is_finished() {
                let _ = self
                    .output
                    .recv_timeout(std::time::Duration::from_millis(5));
            }
            let _ = t.join();
        }
    }
}
fn run(
    config: &Config,
    device: D3dDevice,
    shared: &(Mutex<Mailbox>, Condvar),
    tx: &SyncSender<Result<Encoded>>,
) -> Result<()> {
    let mut encoder: Option<Box<dyn Encoder>> = None;
    let mut generation = 0;
    let mut last_timestamp = 0;
    let mut last_rate = 0;
    let mut last_rate_at = 0;
    let mut last_diagnostics = String::new();
    loop {
        let (raw, keyframe, rate, next_generation) = {
            let (lock, ready) = shared;
            let mut m = lock.lock().unwrap();
            while !m.stop && m.raw.is_none() && m.generation == generation {
                m = ready.wait(m).unwrap();
            }
            if m.stop {
                return Ok(());
            }
            let key = std::mem::take(&mut m.keyframe);
            (m.raw.take(), key, m.bitrate, m.generation)
        };
        if generation != next_generation {
            encoder = None;
            generation = next_generation;
            last_timestamp = 0;
            last_rate = 0;
            last_diagnostics.clear();
        }
        let Some(mut raw) = raw else { continue };
        if raw.generation != generation || rate == 0 || raw.timing.capture_end_us <= last_timestamp
        {
            continue;
        }
        if raw.gpu.wait.is_none() {
            let mutex: IDXGIKeyedMutex = raw.gpu.texture.cast()?;
            let hr = unsafe {
                (Interface::vtable(&mutex).AcquireSync)(Interface::as_raw(&mutex), 0, 100)
            };
            if hr.0 != 0 {
                raw.lease.consumed.store(false, Ordering::Relaxed);
                log::warn!("keyed AcquireSync failed {hr:?}");
                continue;
            }
            *raw.lease.keyed.lock().unwrap() = Some(mutex);
        }
        let mut params = config.params.clone();
        params.width = raw.gpu.width;
        params.height = raw.gpu.height;
        params.target_bitrate = rate.clamp(params.min_bitrate, params.max_bitrate);
        if encoder.is_none() {
            let choice = if config.encoder == EncoderChoice::Vpl {
                EncoderChoice::Auto
            } else {
                config.encoder
            };
            // Legacy --encoder=vpl permits the software fallback. Explicit NVENC remains strict.
            encoder = Some(create_encoder(
                choice,
                params.clone(),
                Some(device.clone()),
            )?);
            let e = encoder.as_ref().unwrap();
            let impl_name = backend_name(e.backend());
            log::info!(
                "PRTS encoder InitEncode {}x{} impl={} status=0 max_fps={} cores=4 start_kbps={} min_kbps={} max_kbps={}",
                params.width,
                params.height,
                impl_name,
                params.fps,
                params.target_bitrate / 1000,
                params.min_bitrate / 1000,
                params.max_bitrate / 1000
            );
            log::info!(
                "PRTS encoder active={} selected={}",
                e.backend(),
                e.backend()
            );
            log::info!("PRTS encoder selected={}", e.backend());
            last_rate = params.target_bitrate;
        }
        let e = encoder.as_mut().unwrap();
        if e.params().width != params.width || e.params().height != params.height {
            e.reconfigure(params.clone())?;
            last_rate = params.target_bitrate;
        } else if params.target_bitrate != last_rate {
            let now = now_us();
            let previous = e.params().target_bitrate;
            // Match legacy BRC reset coalescing: significant decreases are immediate.
            if params.target_bitrate * 10 <= previous * 7 || now - last_rate_at >= 1_000_000 {
                log::info!(
                    "PRTS encoder rate={} outcome={:?}",
                    params.target_bitrate,
                    e.reconfigure(params.clone())?
                );
                last_rate = params.target_bitrate;
                last_rate_at = now;
            }
        }
        if keyframe {
            e.force_idr();
        }
        raw.timing.encode_begin_us = now_us();
        if config.trace {
            log::info!(
                "PRTS TR S ce={} t={} in={}",
                raw.timing.capture_end_us,
                raw.timing.encode_begin_us,
                raw.input_seq
            );
            log::info!(
                "PRTS TR Q ce={} received={} begin={}",
                raw.timing.capture_end_us,
                raw.received_us,
                raw.timing.encode_begin_us
            );
        }
        let result = e.encode(InputFrame {
            timestamp_us: raw.timing.capture_end_us as u64,
            data: FrameData::Gpu(&raw.gpu),
        });
        raw.timing.encode_end_us = now_us();
        let au = match result {
            Ok(v) => v,
            Err(Error::PoolBusy) => {
                log::warn!("raw pool busy");
                continue;
            }
            Err(error) => {
                raw.lease.consumed.store(false, Ordering::Relaxed);
                return Err(error.into());
            }
        };
        last_timestamp = raw.timing.capture_end_us;
        let diagnostics = e.diagnostics();
        if diagnostics != last_diagnostics {
            log::info!("PRTS VPL GetVideoParam {diagnostics}");
            last_diagnostics = diagnostics.clone();
        }
        if let Some(au) = au {
            tx.send(Ok(Encoded {
                au,
                timing: raw.timing,
                generation,
                width: raw.gpu.width,
                height: raw.gpu.height,
                backend: e.backend(),
                diagnostics,
            }))?;
        }
    }
}
pub fn backend_name(b: Backend) -> &'static str {
    match b {
        Backend::Vpl => "PRTS oneVPL",
        Backend::Nvenc => "NVENC",
        Backend::OpenH264 => "OpenH264",
    }
}
