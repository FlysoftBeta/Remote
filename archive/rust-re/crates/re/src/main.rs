mod config;
mod encode;
mod input;
mod platform;
mod transport;
use config::Config;
use encode::{Lease, Raw, Worker};
use re_encode::{FenceWait, d3d::D3dDevice};
use re_mojo::{Client, Event, FrameAction, FrameId};
use serde_json::{Value, json};
use std::{
    collections::{BTreeMap, HashMap},
    sync::{
        Arc, Mutex,
        atomic::AtomicBool,
        mpsc::{self, Receiver, Sender, TryRecvError},
    },
    time::{Duration, Instant},
};
use windows::Win32::Graphics::Direct3D11::ID3D11Texture2D;
type Result<T> = std::result::Result<T, Box<dyn std::error::Error + Send + Sync>>;

struct Peer {
    rtc: re_rtc::Session,
    sid: String,
    handshaken: bool,
    ready: bool,
    paused: bool,
    failed: Option<Instant>,
}
struct Window {
    width: i32,
    height: i32,
    dsf: f32,
}
struct App {
    config: Config,
    device: D3dDevice,
    worker: Worker,
    generation: u64,
    release_tx: Sender<(FrameId, bool)>,
    release_rx: Receiver<(FrameId, bool)>,
    pending: Vec<FrameId>,
    textures: HashMap<(u32, u64, u32), ID3D11Texture2D>,
    fences: HashMap<(u64, u64), FenceWait>,
    peer: Option<Peer>,
    hello: bool,
    windows: BTreeMap<u32, Window>,
    active: u32,
    select: Option<u32>,
    refresh: bool,
    disconnected: bool,
    input: input::Input,
    input_seq: u16,
    ime_seq: u64,
    state: BTreeMap<String, Value>,
    stats_at: Instant,
    frames: u64,
    bytes: u64,
    encode_ms: Vec<f64>,
    backend: String,
}
impl App {
    fn new(config: Config) -> Result<Self> {
        let device = D3dDevice::create(None)?;
        let worker = Worker::start(config.clone(), device.clone());
        let (release_tx, release_rx) = mpsc::channel();
        Ok(Self {
            config,
            device,
            worker,
            generation: 1,
            release_tx,
            release_rx,
            pending: Vec::new(),
            textures: HashMap::new(),
            fences: HashMap::new(),
            peer: None,
            hello: false,
            windows: BTreeMap::new(),
            active: 0,
            select: None,
            refresh: false,
            disconnected: false,
            input: Default::default(),
            input_seq: 0,
            ime_seq: 0,
            state: BTreeMap::new(),
            stats_at: Instant::now(),
            frames: 0,
            bytes: 0,
            encode_ms: Vec::new(),
            backend: String::new(),
        })
    }
    fn reset(&mut self) {
        self.generation += 1;
        self.worker.reset(self.generation);
        self.textures.clear();
        self.fences.clear();
        self.pending.clear();
    }
    fn send_control(&mut self, mut m: Value) -> Result<()> {
        let t = m["t"].as_str().unwrap_or("").to_owned();
        if t == "ime.state" || t == "ime.vk" {
            self.ime_seq += 1;
            m["seq"] = json!(self.ime_seq);
        }
        if matches!(t.as_str(), "cursor" | "ime.state" | "ime.vk") {
            self.state.insert(t, m.clone());
        }
        if let Some(p) = &mut self.peer
            && p.handshaken
        {
            Self::data(&mut p.rtc, 1, &m)?;
        }
        Ok(())
    }
    fn data(rtc: &mut re_rtc::Session, id: i32, v: &Value) -> Result<()> {
        match rtc.send(id, &serde_json::to_vec(v)?, false) {
            Ok(()) => (),
            Err(e) if e.0.ends_with("(-4)") => log::warn!("DataChannel {id} backpressure"),
            Err(e) => return Err(e.into()),
        }
        Ok(())
    }
    fn browser_event(&mut self, event: Event<'_>) -> Result<FrameAction> {
        match event {
            Event::Hello(h) => {
                if h.version != 1 || h.software_compositing {
                    return Err("requires protocol 1 + D3D11 compositing".into());
                }
                self.match_adapter(h.gpu_luid)?;
                self.hello = true;
                self.select = self.windows.keys().next().copied();
                log::info!("browser Hello luid={:x}", h.gpu_luid);
            }
            Event::WindowAdded(w)|Event::WindowChanged(w)=>{
                let changed=self.windows.get(&w.window_id).is_none_or(|g|g.width!=w.size_px.width||g.height!=w.size_px.height||g.dsf!=w.device_scale_factor);
                self.windows.insert(w.window_id,Window{width:w.size_px.width,height:w.size_px.height,dsf:w.device_scale_factor});
                if self.active==0 || self.active==w.window_id && changed {self.select=Some(w.window_id);}
            }
            Event::WindowRemoved(id)=>{self.windows.remove(&id);if self.active==id{self.select=Some(self.windows.keys().next().copied().unwrap_or(0));}}
            Event::WindowActivated(id)=>if self.active!=id{self.select=Some(id)},
            Event::Cursor(css)=>self.send_control(json!({"t":"cursor","css":css}))?,
            Event::Ime(s)=>self.send_control(json!({"t":"ime.state","focused":s.focused,"type":s.input_type,"mode":s.mode,"flags":s.flags,"reason":s.reason,"composing":s.composing,"context":s.context,"rect":{"x":s.caret_norm.x*65535.,"y":s.caret_norm.y*65535.,"w":s.caret_norm.width*65535.,"h":s.caret_norm.height*65535.}}))?,
            Event::VirtualKeyboard{show,..}=>self.send_control(json!({"t":"ime.vk","show":show}))?,
            Event::Reset{window_id,epoch}=>{log::info!("capture Reset window={window_id} epoch={epoch}");self.reset();},
            Event::Recovered{window_id,epoch}=>{log::info!("capture Recovered window={window_id} epoch={epoch}");self.worker.keyframe();self.refresh=true;},
            Event::Disconnected=>{self.disconnected=true;self.hello=false;self.active=0;self.windows.clear();self.state.clear();self.reset();},
            Event::NewBuffer(_)=>(),
            Event::Frame(f)=>{
                if !self.peer.as_ref().is_some_and(|p|p.ready&&!p.paused) || f.window_id!=self.active{return Ok(FrameAction::Consumed);}
                self.match_adapter(f.adapter_luid)?;
                if self.config.trace{log::info!("PRTS TR F seq={} cb={} ce={} rx={} epoch={}",f.frame_seq,f.capture_begin_us,f.capture_end_us,f.received_us,f.epoch);}
                let key=(f.window_id,f.epoch,f.buffer_id);
                if let std::collections::hash_map::Entry::Vacant(e)=self.textures.entry(key){e.insert(self.device.import_texture(&f.texture)?);}
                let texture=self.textures[&key].clone();
                let wait=if let Some(fence)=f.fence{
                    if let std::collections::hash_map::Entry::Vacant(e)=self.fences.entry(f.fence_token){e.insert(self.device.import_fence(&fence,0)?);log::info!("PRTS OpenSharedFence token={:?}",f.fence_token);}
                    let mut wait=self.fences[&f.fence_token].clone();wait.value=f.fence_value;Some(wait)
                }else{None};
                let lease=Arc::new(Lease{id:f.id,consumed:AtomicBool::new(true),keyed:Mutex::new(None),release:self.release_tx.clone()});
                let raw=Raw{gpu:re_encode::GpuFrame{texture,width:f.content_rect.width as u32 & !1,height:f.content_rect.height as u32 & !1,wait,lease:lease.clone()},lease,timing:re_rtc::FrameTiming{capture_begin_us:f.capture_begin_us,capture_end_us:f.capture_end_us,..Default::default()},generation:self.generation,input_seq:self.input_seq,received_us:f.received_us};
                self.pending.push(f.id);self.worker.push(raw);return Ok(FrameAction::Defer);
            }
        }
        Ok(FrameAction::Consumed)
    }
    fn match_adapter(&mut self, luid: u64) -> Result<()> {
        if luid != 0 && luid != self.device.adapter().luid {
            self.reset();
            let device = D3dDevice::create_for_luid(luid)?;
            self.worker = Worker::start(self.config.clone(), device.clone());
            self.worker.reset(self.generation);
            self.device = device;
        }
        Ok(())
    }
    fn browser_actions(&mut self, c: &mut Client) -> Result<()> {
        if self.hello
            && let Some(id) = self.select.take()
        {
            self.input.reset(c)?;
            if self.active != 0 {
                c.stop_capture(self.active)?;
            }
            self.reset();
            self.active = id;
            if let Some(w) = self.windows.get(&id) {
                let message = json!({"t":"frame","w":w.width,"h":w.height,"dsf":w.dsf});
                c.start_capture(id, self.config.params.fps, false)?;
                self.send_control(message)?;
                self.refresh = true;
            }
        }
        if self.refresh && self.active != 0 {
            c.refresh(self.active)?;
            self.worker.keyframe();
            self.refresh = false;
        }
        for (id, consumed) in self.release_rx.try_iter() {
            if let Some(i) = self.pending.iter().position(|n| *n == id) {
                self.pending.swap_remove(i);
                c.release_frame(id, consumed)?;
            }
        }
        Ok(())
    }
    fn signal(&mut self, m: Value, c: &mut Client, out: &transport::Transport) -> Result<()> {
        if m["t"] == "session.open" {
            let Some(sid) = m["sid"].as_str().filter(|s| !s.is_empty()) else {
                return Ok(());
            };
            self.peer = None;
            self.input.reset(c)?;
            self.reset();
            self.worker.rates(self.config.params.target_bitrate);
            let mut config = self.config.rtc.clone();
            if !config.relay_only {
                config.relay_only = m["iceTransportPolicy"] == "relay";
                config.ice_servers_json = m.get("iceServers").unwrap_or(&json!([])).to_string();
            }
            let result = re_rtc::Session::load(&self.config.dll, config);
            match result {
                Ok(mut rtc) => {
                    rtc.offer(false)?;
                    self.peer = Some(Peer {
                        rtc,
                        sid: sid.into(),
                        handshaken: false,
                        ready: false,
                        paused: false,
                        failed: None,
                    });
                    self.worker.keyframe();
                }
                Err(e) => {
                    log::error!("session create: {e}");
                    out.send(json!({"t":"bye","sid":sid,"reason":e.to_string()}))?;
                }
            }
        } else if let Some(p) = &mut self.peer
            && m["sid"] == p.sid
        {
            match m["t"].as_str() {
                Some("answer") => p
                    .rtc
                    .set_remote_description("answer", m["sdp"].as_str().ok_or("missing sdp")?)?,
                Some("ice") => {
                    let v = &m["candidate"];
                    if !v.is_null() {
                        p.rtc.add_ice(
                            v["sdpMid"].as_str().unwrap_or(""),
                            v["sdpMLineIndex"].as_i64().unwrap_or(0) as i32,
                            v["candidate"].as_str(),
                        )?;
                    }
                }
                Some("restart") => p.rtc.offer(true)?,
                Some("bye") => {
                    self.peer = None;
                    self.input.reset(c)?;
                    self.reset();
                }
                _ => (),
            }
        }
        Ok(())
    }
    fn rtc_events(&mut self, c: &mut Client, out: &transport::Transport) -> Result<()> {
        let Some(mut p) = self.peer.take() else {
            return Ok(());
        };
        let mut failure = None;
        for _ in 0..256 {
            let Some(e) = p.rtc.poll(Duration::ZERO)? else {
                break;
            };
            match e{
                re_rtc::Event::Sdp{kind,sdp}=>out.send(json!({"t":kind,"sid":p.sid,"sdp":sdp}))?,
                re_rtc::Event::Ice{mid,mline,candidate}=>out.send(json!({"t":"ice","sid":p.sid,"candidate":candidate.map(|v|json!({"candidate":v,"sdpMid":mid,"sdpMLineIndex":mline}))}))?,
                re_rtc::Event::State(n)=>{p.ready=n==2;p.failed=if n==4{Some(Instant::now())}else{None};log::info!("PeerConnection state={n}");},
                re_rtc::Event::Rates{bitrate_bps,..}=>{p.paused=bitrate_bps==0;self.worker.rates(bitrate_bps);},
                re_rtc::Event::Keyframe=>{self.worker.keyframe();self.refresh=true;},
                re_rtc::Event::Stats(value)=>{
                    if let Ok(v)=serde_json::from_str::<Value>(&value) {
                        if let Some(stats)=v["rtc"].as_array() {
                            for s in stats.iter().filter(|s|s["type"]=="outbound-rtp") {
                                log::info!("PRTS sender stats [{s}]");
                            }
                        }
                        log::info!("PRTS native stats {}",v["native"]);
                    }
                }
                re_rtc::Event::Error(e)=>{failure=Some(e);break;},
                re_rtc::Event::Data{id,bytes,binary,received_wall_ms,received_ticks_us}=>{
                    if id==0 && binary && p.handshaken && self.hello {
                        let g=self.geometry();if let Some(seq)=self.input.binary(c,self.active,g,&bytes)?{self.input_seq=seq;if self.config.trace{log::info!("PRTS TR I seq={seq} rx={received_ticks_us} parsed={}",platform::now_us());}}
                    }else if !binary && let Ok(m)=serde_json::from_slice::<Value>(&bytes){
                        let t=m["t"].as_str().unwrap_or("");
                        if id==2 && t=="ping" && m.get("id").is_some() && m["t0"].is_number(){let t2=p.rtc.wall_ms();Self::data(&mut p.rtc,2,&json!({"t":"pong","id":m["id"],"t0":m["t0"],"t1":received_wall_ms,"t2":t2}))?;}
                        else if id==1 && t=="hello"{
                            if m["v"]!=1 || m["input"]!=1{failure=Some("version".into());break;}
                            p.handshaken=true;let(w,h,dsf)=self.geometry();
                            Self::data(&mut p.rtc,1,&json!({"t":"hello","v":1,"input":1,"name":"re","frame":{"w":w,"h":h},"dsf":dsf}))?;
                            if self.hello{self.input.control(c,self.active,&m)?;}
                            let replay:Vec<_>=self.state.values().cloned().collect();
                            for mut v in replay{if v["t"]=="ime.state"||v["t"]=="ime.vk"{self.ime_seq+=1;v["seq"]=json!(self.ime_seq);}Self::data(&mut p.rtc,1,&v)?;}
                            self.worker.keyframe();self.refresh=true;
                        }else if id==1 && p.handshaken && self.hello{if t=="keyframe"{self.worker.keyframe();}self.input.control(c,self.active,&m)?;}
                    }
                }
                re_rtc::Event::ChannelState{..}|re_rtc::Event::Frame{..}=>(),
            }
        }
        if p.failed
            .is_some_and(|t| t.elapsed() > Duration::from_secs(10))
        {
            failure = Some("connection failed".into());
        }
        if let Some(reason) = failure {
            log::error!("session: {reason}");
            out.send(json!({"t":"bye","sid":p.sid,"reason":reason}))?;
            self.input.reset(c)?;
            self.reset();
        } else {
            self.peer = Some(p);
        }
        Ok(())
    }
    fn geometry(&self) -> (i32, i32, f32) {
        self.windows
            .get(&self.active)
            .map(|w| (w.width, w.height, w.dsf))
            .unwrap_or((1920, 1080, 1.))
    }
    fn encoded(&mut self) -> Result<()> {
        while let Ok(value) = self.worker.output.try_recv() {
            let v = value?;
            if v.generation != self.generation {
                continue;
            }
            if let Some(p) = &mut self.peer {
                p.rtc.submit(re_rtc::EncodedFrame {
                    annex_b: &v.au.annex_b,
                    timestamp_us: v.au.timestamp_us as i64,
                    width: v.width,
                    height: v.height,
                    keyframe: v.au.frame_type == re_encode::FrameType::Idr,
                    timing: v.timing,
                })?;
                self.frames += 1;
                self.bytes += v.au.annex_b.len() as u64;
                self.encode_ms
                    .push((v.timing.encode_end_us - v.timing.encode_begin_us) as f64 / 1000.);
                self.backend = encode::backend_name(v.backend).into();
                if self.backend == "OpenH264" {
                    log::warn!(
                        "PRTS VPL Rust fallback encoder=openh264 reason={}",
                        v.diagnostics
                    );
                }
            }
        }
        if self.stats_at.elapsed() >= Duration::from_secs(5) {
            if let Some(p) = &mut self.peer {
                p.rtc.request_stats()?;
                self.encode_ms.sort_by(f64::total_cmp);
                let at = |n: usize| {
                    self.encode_ms
                        .get(self.encode_ms.len().saturating_sub(1) * n / 100)
                        .copied()
                        .unwrap_or(0.)
                };
                let secs = self.stats_at.elapsed().as_secs_f64();
                let v = json!({"t":"stats","fps":self.frames as f64/secs,"kbps":self.bytes as f64*8./secs/1000.,"backend":self.backend,"encMs":{"p50":at(50),"p95":at(95)},"limit":"none"});
                if p.handshaken {
                    Self::data(&mut p.rtc, 1, &v)?;
                }
                log::info!(
                    "D3D source fps={} frames={} outstanding={}",
                    self.frames as f64 / secs,
                    self.frames,
                    self.pending.len()
                );
            }
            self.stats_at = Instant::now();
            self.frames = 0;
            self.bytes = 0;
            self.encode_ms.clear();
        }
        Ok(())
    }
}
fn run(config: Config) -> Result<()> {
    config.log();
    let mut browser = Client::load(&config.dll)?;
    platform::native_logging(&config.dll)?;
    browser.launch(&config.chrome)?;
    let transport = transport::Transport::start(config.websocket.as_deref())?;
    transport.send(json!({"t":"hello","v":1,"role":"endpoint","instance":config.instance,"name":"re","caps":{"codecs":["H264"],"maxFps":config.params.fps}}))?;
    let mut app = App::new(config)?;
    let mut launched = Instant::now();
    let mut retry = None;
    let mut failures = 0u32;
    while !platform::stopping() {
        for _ in 0..64 {
            match transport.input.try_recv() {
                Ok(m) => app.signal(m?, &mut browser, &transport)?,
                Err(TryRecvError::Disconnected) => {
                    app.peer = None;
                    drop(app);
                    browser.shutdown()?;
                    return Ok(());
                }
                Err(TryRecvError::Empty) => break,
            }
        }
        if let Some(t) = retry {
            if Instant::now() >= t {
                browser = Client::load(&app.config.dll)?;
                match browser.launch(&app.config.chrome) {
                    Ok(()) => {
                        retry = None;
                        launched = Instant::now();
                    }
                    Err(e) => {
                        failures += 1;
                        log::warn!("browser relaunch: {e}");
                        retry = Some(Instant::now() + Duration::from_secs(1u64 << failures.min(5)));
                    }
                }
            } else {
                std::thread::sleep(Duration::from_millis(1));
            }
        } else {
            let mut error = None;
            browser.poll(Duration::from_millis(1), |e| match app.browser_event(e) {
                Ok(a) => a,
                Err(e) => {
                    error = Some(e);
                    FrameAction::Abandon
                }
            })?;
            if let Some(e) = error {
                return Err(e);
            }
            if app.disconnected || !app.hello && launched.elapsed() > Duration::from_secs(15) {
                log::warn!("browser disconnected/Hello timeout; restarting with backoff");
                browser.shutdown()?;
                app.disconnected = false;
                app.hello = false;
                app.windows.clear();
                app.active = 0;
                app.reset();
                failures += 1;
                retry =
                    Some(Instant::now() + Duration::from_millis(250 * (1u64 << failures.min(7))));
            } else {
                app.browser_actions(&mut browser)?;
                if app.hello && launched.elapsed() > Duration::from_secs(30) {
                    failures = 0;
                }
            }
        }
        if app.hello {
            app.rtc_events(&mut browser, &transport)?;
        }
        app.encoded()?;
    }
    app.peer = None;
    drop(app);
    browser.shutdown()?;
    Ok(())
}
fn main() {
    platform::init();
    if std::env::args().any(|a| a == "--help") {
        eprintln!(
            "re --chrome=PATH --user-data-dir=PATH [--instance=default] [--url=URL] [--app] [--fps=60] [--encoder=vpl|auto|nvenc|openh264] [--cr-SWITCH[=VALUE]] [--signal=ws[s]://host/signal?role=endpoint] [--native-dll=PATH]; M4 tuning: tools/latency/PRESETS.md. Default transport: framed stdio; re_native.dll beside re.exe."
        );
        return;
    }
    if let Err(e) = Config::parse(std::env::args().skip(1)).and_then(run) {
        log::error!("{e}");
        std::process::exit(1);
    }
}
