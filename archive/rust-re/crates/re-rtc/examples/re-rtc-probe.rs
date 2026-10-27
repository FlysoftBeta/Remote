//! stdio endpoint for the existing control plane. Stdout is framed JSON only.
//! ENDPOINT_PATH=<...>/examples/re-rtc-probe.exe node control-plane --port 8360
use re_encode::{
    CpuFrame, Encoder, EncoderChoice, EncoderParams, FrameData, FrameType, InputFrame,
    create_encoder,
};
use re_rtc::{Config, EncodedFrame, Event, FrameTiming, Session};
use serde_json::{Value, json};
use std::{
    error::Error,
    io::{self, Read, Write},
    sync::mpsc::{self, Receiver, SyncSender, TryRecvError},
    time::{Duration, Instant},
};
type Result<T> = std::result::Result<T, Box<dyn Error>>;

fn transport() -> (Receiver<Value>, SyncSender<Value>) {
    let (input_tx, input_rx) = mpsc::sync_channel(64);
    std::thread::spawn(move || {
        let mut stdin = io::stdin().lock();
        loop {
            let mut header = [0; 4];
            if stdin.read_exact(&mut header).is_err() {
                break;
            }
            let n = u32::from_le_bytes(header) as usize;
            if n == 0 || n > 4 * 1024 * 1024 {
                break;
            }
            let mut bytes = vec![0; n];
            if stdin.read_exact(&mut bytes).is_err() {
                break;
            }
            let Ok(value) = serde_json::from_slice(&bytes) else {
                break;
            };
            if input_tx.send(value).is_err() {
                break;
            }
        }
    });
    let (output_tx, output_rx) = mpsc::sync_channel(64);
    std::thread::spawn(move || {
        let mut stdout = io::stdout().lock();
        for value in output_rx {
            let Ok(bytes) = serde_json::to_vec(&value) else {
                break;
            };
            if stdout
                .write_all(&(bytes.len() as u32).to_le_bytes())
                .is_err()
                || stdout.write_all(&bytes).is_err()
                || stdout.flush().is_err()
            {
                break;
            }
        }
    });
    (input_rx, output_tx)
}

struct Stream {
    rtc: Session,
    encoder: Box<dyn Encoder>,
    sid: String,
    params: EncoderParams,
    yuv: Vec<u8>,
    frame: u32,
    next_frame: Instant,
    next_stats: Instant,
    suspended: bool,
    ready: bool,
    keyframe: bool,
    rates: Option<(u32, f64)>,
    dump_au: Option<std::path::PathBuf>,
}
impl Stream {
    fn new(message: &Value, width: u32, height: u32, fps: u32, delay: bool) -> Result<Self> {
        let config = Config {
            fps: fps as i32,
            playout_delay: delay,
            ice_servers_json: message.get("iceServers").unwrap_or(&json!([])).to_string(),
            relay_only: message["iceTransportPolicy"] == "relay",
            ..Config::default()
        };
        let mut rtc = Session::from_env(config)?;
        rtc.offer(false)?;
        let params = EncoderParams {
            width,
            height,
            fps,
            level: Some(42),
            ..EncoderParams::default()
        };
        let encoder = create_encoder(EncoderChoice::OpenH264, params.clone(), None)?;
        eprintln!(
            "re-rtc-probe encoder={} {}x{}@{} SPS-level=4.2",
            encoder.backend(),
            width,
            height,
            fps
        );
        Ok(Self {
            rtc,
            encoder,
            sid: message["sid"].as_str().ok_or("missing sid")?.into(),
            params,
            yuv: vec![128; (width * height * 3 / 2) as usize],
            frame: 0,
            next_frame: Instant::now(),
            next_stats: Instant::now(),
            suspended: false,
            ready: false,
            keyframe: true,
            rates: None,
            dump_au: std::env::var_os("RE_RTC_PROBE_FIRST_AU").map(Into::into),
        })
    }
    fn signal(&mut self, message: &Value) -> Result<()> {
        if message["sid"] != self.sid {
            return Ok(());
        }
        match message["t"].as_str() {
            Some("answer") => self
                .rtc
                .set_remote_description("answer", message["sdp"].as_str().ok_or("missing sdp")?)?,
            Some("ice") => {
                let c = &message["candidate"];
                self.rtc.add_ice(
                    c["sdpMid"].as_str().unwrap_or(""),
                    c["sdpMLineIndex"].as_i64().unwrap_or(0) as i32,
                    c["candidate"].as_str(),
                )?;
            }
            Some("restart") => self.rtc.offer(true)?,
            _ => (),
        }
        Ok(())
    }
    fn events(&mut self, out: &SyncSender<Value>) -> Result<()> {
        // Bounded per iteration, so a burst cannot starve frame production.
        for _ in 0..256 {
            let Some(event) = self.rtc.poll(Duration::ZERO)? else {
                break;
            };
            match event {
                Event::Sdp { kind, sdp } => out.try_send(json!({"t":kind,"sid":self.sid,"sdp":sdp}))?,
                Event::Ice { mid, mline, candidate } => out.try_send(json!({"t":"ice","sid":self.sid,
                    "candidate":candidate.map(|c| json!({"candidate":c,"sdpMid":mid,"sdpMLineIndex":mline}))}))?,
                Event::State(state) => { self.ready = state == 2; eprintln!("RTC state={state}"); }
                Event::ChannelState { id, state } => eprintln!("DataChannel id={id} state={state}"),
                Event::Rates { bitrate_bps, fps } => {
                    eprintln!("BWE bitrate_bps={bitrate_bps} fps={fps:.2}");
                    self.rates = Some((bitrate_bps, fps));
                }
                Event::Keyframe => { self.keyframe = true; eprintln!("keyframe requested"); }
                Event::Stats(value) => eprintln!("RTC stats {value}"),
                Event::Error(value) => return Err(value.into()),
                Event::Data { id, bytes, binary, received_wall_ms, .. } => {
                    if id == 0 { self.rtc.send(id, &bytes, binary)?; continue; }
                    if binary { continue; }
                    let Ok(m) = serde_json::from_slice::<Value>(&bytes) else { continue };
                    let response = match (id, m["t"].as_str()) {
                        (1, Some("hello")) if m["v"] == 1 && m["input"] == 1 => {
                            self.keyframe = true;
                            Some(json!({"t":"hello","v":1,"input":1,
                                "frame":{"w":self.params.width,"h":self.params.height},"dsf":1,"name":"re-rtc-probe"}))
                        }
                        (1, Some("keyframe")) => { self.keyframe = true; None }
                        (2, Some("ping")) => Some(json!({"t":"pong","id":m["id"],"t0":m["t0"],
                            "t1":received_wall_ms,"t2":self.rtc.wall_ms()})),
                        (2, Some("echo")) => { eprintln!("DataChannel echo"); Some(m) }
                        _ => None,
                    };
                    if let Some(response) = response { self.rtc.send(id, &serde_json::to_vec(&response)?, false)?; }
                }
                Event::Frame { .. } => (),
            }
        }
        if let Some((bitrate, fps)) = self.rates.take() {
            self.suspended = bitrate == 0;
            if !self.suspended {
                let mut next = self.params.clone();
                next.target_bitrate = bitrate.clamp(next.min_bitrate, next.max_bitrate);
                next.fps = (fps.round() as u32).clamp(1, self.params.fps);
                if next != *self.encoder.params() {
                    self.encoder.reconfigure(next)?;
                }
            }
        }
        Ok(())
    }
    fn tick(&mut self) -> Result<()> {
        let now = Instant::now();
        if now >= self.next_stats {
            self.rtc.request_stats()?;
            self.next_stats = now + Duration::from_secs(5);
        }
        if !self.ready || self.suspended || now < self.next_frame {
            return Ok(());
        }
        // SetRates reports WebRTC's observed input FPS too. Keep the raw source
        // cadence independent, or feeding that estimate back lowers FPS again.
        self.next_frame = now + Duration::from_secs_f64(1.0 / f64::from(self.params.fps));
        if self.keyframe {
            self.encoder.force_idr();
            self.keyframe = false;
        }
        let capture = self.rtc.now_us();
        let w = self.params.width as usize;
        let h = self.params.height as usize;
        // Moving bars and an oscillating square make freezes and color errors
        // visible without involving a browser capture or a hardware encoder.
        for y in 0..h {
            for x in 0..w {
                self.yuv[y * w + x] =
                    32 + (((x / 80 + y / 80 + self.frame as usize / 4) % 6) * 32) as u8;
            }
        }
        let begin = self.rtc.now_us();
        let input = CpuFrame {
            width: self.params.width,
            height: self.params.height,
            planes: [
                &self.yuv[..w * h],
                &self.yuv[w * h..w * h * 5 / 4],
                &self.yuv[w * h * 5 / 4..],
            ],
            strides: [w, w / 2, w / 2],
        };
        if let Some(au) = self.encoder.encode(InputFrame {
            timestamp_us: capture as u64,
            data: FrameData::Cpu(input),
        })? {
            if let Some(path) = self.dump_au.take() {
                std::fs::write(path, &au.annex_b)?;
            }
            let end = self.rtc.now_us();
            self.rtc.submit(EncodedFrame {
                annex_b: &au.annex_b,
                timestamp_us: capture,
                width: self.params.width,
                height: self.params.height,
                keyframe: au.frame_type == FrameType::Idr,
                timing: FrameTiming {
                    capture_begin_us: capture,
                    capture_end_us: begin,
                    encode_begin_us: begin,
                    encode_end_us: end,
                },
            })?;
        }
        self.frame += 1;
        Ok(())
    }
}
fn main() -> Result<()> {
    let mut width = 1280;
    let mut height = 720;
    let mut fps = 30;
    let mut delay = true;
    for arg in std::env::args().skip(1) {
        if let Some(v) = arg.strip_prefix("--width=") {
            width = v.parse()?;
        } else if let Some(v) = arg.strip_prefix("--height=") {
            height = v.parse()?;
        } else if let Some(v) = arg.strip_prefix("--fps=") {
            fps = v.parse()?;
        } else if arg == "--no-playout-delay" {
            delay = false;
        } else if arg == "--help" {
            eprintln!(
                "re-rtc-probe --width=1280 --height=720 --fps=30 [--no-playout-delay]; stdio control-plane endpoint, set RE_NATIVE_DLL"
            );
            return Ok(());
        } else if !arg.starts_with("--instance=") {
            return Err(format!("unknown argument {arg}").into());
        }
    }
    let (input, output) = transport();
    output.try_send(json!({"t":"hello","v":1,"name":"re-rtc-probe"}))?;
    let mut stream: Option<Stream> = None;
    loop {
        match input.try_recv() {
            Ok(m) if m["t"] == "session.open" => {
                // Tear down old native queues before creating a replacement.
                stream = None;
                match Stream::new(&m, width, height, fps, delay) {
                    Ok(s) => stream = Some(s),
                    Err(e) => {
                        eprintln!("session create: {e}");
                        output
                            .try_send(json!({"t":"bye","sid":m["sid"],"reason":e.to_string()}))?;
                    }
                }
            }
            Ok(m) if m["t"] == "bye" => {
                if stream.as_ref().is_some_and(|s| m["sid"] == s.sid) {
                    stream = None;
                }
            }
            Ok(m) => {
                if let Some(s) = &mut stream {
                    s.signal(&m)?;
                }
            }
            Err(TryRecvError::Disconnected) => return Ok(()),
            Err(TryRecvError::Empty) => (),
        }
        if let Some(s) = &mut stream {
            s.events(&output)?;
            s.tick()?;
        }
        std::thread::sleep(Duration::from_millis(1));
    }
}
