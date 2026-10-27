use re_mojo::{Client, Event, FrameAction, Key, Pointer, Size, Wheel};
use std::{
    error::Error,
    fs::File,
    io::{BufWriter, Write},
    time::{Duration, Instant},
};

fn option(args: &[String], name: &str) -> Option<String> {
    args.windows(2)
        .find(|pair| pair[0] == name)
        .map(|pair| pair[1].clone())
}
fn percentile(values: &mut [i64], p: f64) -> i64 {
    values.sort_unstable();
    values[((values.len() - 1) as f64 * p).round() as usize]
}
fn data_url() -> String {
    let html = r#"<!doctype html><meta charset=utf-8><title>RE-PROBE p=0 w=0 k=0</title>
<style>html,body{margin:0;overflow:hidden}canvas{width:100vw;height:100vh}</style><canvas></canvas>
<script>let p=0,w=0,k=0;function title(){document.title=`RE-PROBE p=${p} w=${w} k=${k}`}
addEventListener('pointerdown',()=>{p++;title()});addEventListener('wheel',()=>{w++;title()},{passive:true});
addEventListener('keydown',()=>{k++;title()});let c=document.querySelector('canvas'),g=c.getContext('2d');
function draw(t){c.width=innerWidth;c.height=innerHeight;g.fillStyle=`hsl(${t/25%360} 65% 40%)`;
g.fillRect(0,0,c.width,c.height);g.fillStyle='white';g.font='24px sans-serif';g.fillText(document.title,40,60);
g.fillRect(t/5%c.width,100,80,80);requestAnimationFrame(draw)}requestAnimationFrame(draw)</script>"#;
    let mut url = String::from("data:text/html,");
    for byte in html.bytes() {
        use std::fmt::Write;
        write!(url, "%{byte:02X}").unwrap();
    }
    url
}
fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<_> = std::env::args().skip(1).collect();
    let dll = option(&args, "--dll").ok_or("--dll <absolute re_native.dll path> required")?;
    let chrome = option(&args, "--chrome").ok_or("--chrome <chrome.exe path> required")?;
    let profile =
        option(&args, "--profile").ok_or("--profile <dedicated user-data-dir> required")?;
    let target: usize = option(&args, "--frames").unwrap_or("600".into()).parse()?;
    if !(100..=6000).contains(&target) {
        return Err("--frames must be 100..6000".into());
    }
    let timeout: u64 = option(&args, "--timeout").unwrap_or("180".into()).parse()?;
    let mode = option(&args, "--sync");
    let url = option(&args, "--url");
    let check_input = url.is_none();
    let steady = args.iter().any(|arg| arg == "--steady");
    let size = option(&args, "--size").unwrap_or("1920x1080".into());
    let (width, height) = size.split_once('x').ok_or("--size must be WIDTHxHEIGHT")?;
    let width: u32 = width.parse()?;
    let height: u32 = height.parse()?;
    if width == 0 || height == 0 || width > 8192 || height > 8192 {
        return Err("--size dimensions must be 1..8192".into());
    }
    let mut client = Client::load(dll)?;
    let mut command = vec![
        chrome,
        format!("--user-data-dir={profile}"),
        "--no-first-run".into(),
        "--no-default-browser-check".into(),
        "--disable-legacy-window".into(),
        format!("--screen-info={{{width}x{height}}}"),
        format!("--window-size={width},{height}"),
        "--fake-vsync-rate=60".into(),
        url.unwrap_or_else(data_url),
    ];
    if let Some(ref mode) = mode {
        command.push(format!("--rb-capture-sync={mode}"));
    }
    client.launch(&command)?;
    let mut frames = 0usize;
    let mut fenced = 0usize;
    let mut valid = 0usize;
    let mut resets = 0;
    let mut recovered = 0;
    let mut hello = false;
    let mut window = None;
    let mut active = None;
    let mut disconnected = false;
    let mut input_sent = false;
    let mut input_seen = false;
    let mut resized = false;
    let mut capture_latency = Vec::with_capacity(6000);
    let mut bridge_latency = Vec::with_capacity(6000);
    let mut rows = Vec::with_capacity(6000);
    let mut pending = Vec::with_capacity(64);
    let mut deferred = 0;
    let begin = Instant::now();
    while frames < target && begin.elapsed() < Duration::from_secs(timeout) && !disconnected {
        client.poll(Duration::from_millis(10), |event| {
            match event {
                Event::Hello(info) => {
                    println!("hello version={} luid={} software={}", info.version, info.gpu_luid, info.software_compositing);
                    hello = info.version == 1 && !info.software_compositing;
                }
                Event::WindowAdded(w) | Event::WindowChanged(w) => {
                    println!("window id={} size={}x{} dsf={} title={}", w.window_id, w.size_px.width, w.size_px.height, w.device_scale_factor, w.title);
                    window.get_or_insert(w.window_id);
                    input_seen |= w.title.contains("p=1 w=1 k=1");
                }
                Event::NewBuffer(b) => println!("buffer window={} id={} epoch={} size={}x{} texture={:?}", b.window_id, b.buffer_id, b.epoch, b.coded_size.width, b.coded_size.height, b.texture),
                Event::Frame(f) => {
                    frames += 1;
                    fenced += usize::from(f.fence.is_some());
                    valid += usize::from(f.handles_valid());
                    let capture_us = f.received_us - f.capture_end_us;
                    let bridge_us = f.rust_callback_us - f.received_us;
                    capture_latency.push(capture_us);
                    bridge_latency.push(bridge_us);
                    rows.push((f.frame_seq, f.epoch, f.capture_begin_us, f.capture_end_us, f.received_us,
                        f.callback_us, f.rust_callback_us, f.fence_value, f.coded_size));
                    if frames == 1 || frames.is_multiple_of(60) {
                        println!("frame n={} seq={} size={}x{} fence={} token={:016x}{:016x} value={} capture_to_receive_us={} bridge_to_rust_us={}", frames, f.frame_seq, f.coded_size.width, f.coded_size.height, f.fence.is_some(), f.fence_token.0, f.fence_token.1, f.fence_value, capture_us, bridge_us);
                    }
                    if frames.is_multiple_of(120) {
                        pending.push(f.id);
                        deferred += 1;
                        return FrameAction::Defer;
                    }
                }
                Event::Reset { window_id, epoch } => { resets += 1; println!("reset window={window_id} epoch={epoch}"); }
                Event::Recovered { window_id, epoch } => { recovered += 1; println!("recovered window={window_id} epoch={epoch}"); }
                Event::Disconnected => { disconnected = true; println!("disconnect"); }
                _ => {}
            }
            // This probe submits no GPU read, so immediate true is correct.
            FrameAction::Consumed
        })?;
        for id in pending.drain(..) {
            client.release_frame(id, true)?;
            if client.release_frame(id, true).is_ok() {
                return Err("duplicate frame release unexpectedly succeeded".into());
            }
        }
        if hello
            && active.is_none()
            && let Some(window) = window
        {
            client.start_capture(window, 60, false)?;
            active = Some(window);
        }
        if frames >= 30
            && !input_sent
            && let Some(window) = active
        {
            client.activate(window)?;
            client.focus(true)?;
            client.pointer(
                window,
                &Pointer {
                    action: 0,
                    kind: 0,
                    x: 0.5,
                    y: 0.5,
                    buttons: 1,
                    button: 0,
                    ..Default::default()
                },
            )?;
            client.pointer(
                window,
                &Pointer {
                    action: 2,
                    kind: 0,
                    x: 0.5,
                    y: 0.5,
                    button: 0,
                    ..Default::default()
                },
            )?;
            client.wheel(
                window,
                &Wheel {
                    x: 0.5,
                    y: 0.5,
                    dy: 40.0,
                    ..Default::default()
                },
            )?;
            for down in [true, false] {
                client.key(
                    window,
                    &Key {
                        down,
                        mods: 0,
                        repeat: false,
                        key_code: 65,
                        code: "KeyA",
                        key: "a",
                        timestamp_us: 0,
                    },
                )?;
            }
            println!("input sent: pointer down/up, wheel, KeyA down/up");
            input_sent = true;
        }
        if frames >= target / 2 && !resized && !steady {
            client.set_viewport(
                Size {
                    width: 960,
                    height: 540,
                },
                1.0,
            )?;
            resized = true;
        }
    }
    if !disconnected {
        client.reset_input()?;
    }
    let stats = client.stats()?;
    println!(
        "summary frames={frames} valid_handles={valid} fenced={fenced} resets={resets} recovered={recovered} deferred={deferred} input_checked={check_input} input_seen={input_seen} elapsed_s={:.3} stats={stats:?}",
        begin.elapsed().as_secs_f64()
    );
    if !capture_latency.is_empty() {
        println!(
            "latency_us capture_to_receive_p50={} p95={} bridge_to_rust_p50={} p95={}",
            percentile(&mut capture_latency, 0.5),
            percentile(&mut capture_latency, 0.95),
            percentile(&mut bridge_latency, 0.5),
            percentile(&mut bridge_latency, 0.95)
        );
    }
    if let Some(csv) = option(&args, "--csv") {
        let mut out = BufWriter::new(File::create(csv)?);
        writeln!(
            out,
            "seq,epoch,capture_begin_us,capture_end_us,received_us,callback_us,rust_callback_us,fence_value,width,height"
        )?;
        for (seq, epoch, cb, ce, rcv, ccb, rcb, fence, size) in rows {
            writeln!(
                out,
                "{seq},{epoch},{cb},{ce},{rcv},{ccb},{rcb},{fence},{},{}",
                size.width, size.height
            )?;
        }
    }
    client.shutdown()?;
    if disconnected
        || frames < target
        || valid != frames
        || (check_input && !input_seen)
        || (mode.as_deref() == Some("fence") && fenced != frames)
        || (mode.as_deref() == Some("mutex") && fenced != 0)
        || stats.outstanding != 0
    {
        return Err("probe acceptance failed: frames/handles/input/lease drain".into());
    }
    Ok(())
}
