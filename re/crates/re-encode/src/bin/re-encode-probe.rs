//! Standalone 300-frame encoder/decode probe; no browser or Chromium build needed.
use openh264::{decoder::Decoder, formats::YUVSource};
use re_encode::{d3d::D3dDevice, *};
use std::{
    error::Error,
    fs::{self, File},
    io::{BufWriter, Write},
    mem::size_of,
    os::windows::io::{FromRawHandle, OwnedHandle},
    path::{Path, PathBuf},
    process::Command,
    sync::Arc,
    time::{Duration, Instant},
};
use windows::{
    Win32::{
        Foundation::HMODULE,
        Graphics::{
            Direct3D::D3D_DRIVER_TYPE_UNKNOWN,
            Direct3D11::*,
            Dxgi::{Common::*, *},
        },
    },
    core::{Interface, PCWSTR},
};

type ProbeResult<T> = std::result::Result<T, Box<dyn Error>>;

struct Options {
    encoder: String,
    frames: u32,
    input: String,
    out: PathBuf,
    upload_complete: bool,
    controls: bool,
    params: EncoderParams,
}

fn main() -> ProbeResult<()> {
    let mut options = Options {
        encoder: "all".into(),
        frames: 300,
        input: "nv12".into(),
        out: "encode-probe".into(),
        upload_complete: false,
        controls: false,
        params: EncoderParams::default(),
    };
    for arg in std::env::args().skip(1) {
        if arg == "--upload-complete" {
            options.upload_complete = true;
            continue;
        }
        if arg == "--check-controls" {
            options.controls = true;
            continue;
        }
        if arg == "--help" {
            println!(
                "re-encode-probe [--encoder=all|auto|vpl|nvenc|openh264] [--frames=300] [--input=nv12|bgra|cpu] [--out=DIR] [--width=1920] [--height=1080] [--fps=60] [--kbps=8000] [--rc=vbr|cbr] [--slices=1] [--nvenc-preset=p1|p2|p3] [--upload-complete] [--check-controls]"
            );
            return Ok(());
        }
        let (key, value) = arg.split_once('=').ok_or("expected --name=value")?;
        match key {
            "--encoder" => options.encoder = value.into(),
            "--frames" => options.frames = value.parse()?,
            "--input" => options.input = value.into(),
            "--out" => options.out = value.into(),
            "--width" => options.params.width = value.parse()?,
            "--height" => options.params.height = value.parse()?,
            "--fps" => options.params.fps = value.parse()?,
            "--kbps" => {
                let bps = value
                    .parse::<u32>()?
                    .checked_mul(1000)
                    .ok_or("bitrate overflow")?;
                options.params.target_bitrate = bps;
                options.params.max_bitrate = bps;
                options.params.min_bitrate = options.params.min_bitrate.min(bps);
            }
            "--rc" => {
                options.params.rate_control = match value {
                    "cbr" => RateControl::Cbr,
                    "vbr" => RateControl::Vbr,
                    _ => return Err("--rc must be cbr|vbr".into()),
                }
            }
            "--slices" => options.params.slices = value.parse()?,
            "--nvenc-preset" => {
                options.params.nvenc_preset = match value {
                    "p1" => NvencPreset::P1,
                    "p2" => NvencPreset::P2,
                    "p3" => NvencPreset::P3,
                    _ => return Err("preset must be p1|p2|p3".into()),
                }
            }
            _ => return Err(format!("unknown option {key}").into()),
        }
    }
    options.params.validate()?;
    if options.frames == 0 || !["nv12", "bgra", "cpu"].contains(&options.input.as_str()) {
        return Err("frames must be positive; input must be nv12|bgra|cpu".into());
    }
    fs::create_dir_all(&options.out)?;
    let choices = if options.encoder == "all" {
        vec![
            EncoderChoice::Vpl,
            EncoderChoice::Nvenc,
            EncoderChoice::OpenH264,
        ]
    } else {
        vec![options.encoder.parse()?]
    };
    let mut completed = 0;
    for choice in choices {
        let vendor = match choice {
            EncoderChoice::Vpl => Some(0x8086),
            EncoderChoice::Nvenc => Some(0x10de),
            _ => None,
        };
        let device = D3dDevice::create(vendor)
            .or_else(|_| D3dDevice::create(None))
            .ok();
        let encoder = match create_encoder(choice, options.params.clone(), device.clone()) {
            Ok(e) => e,
            Err(error) if options.encoder == "all" => {
                println!("{choice:?}: unavailable: {error}");
                continue;
            }
            Err(error) => return Err(error.into()),
        };
        run(encoder, device, &options)?;
        completed += 1;
    }
    if completed == 0 {
        return Err("no encoder was available".into());
    }
    Ok(())
}

struct SharedTexture {
    producer: ID3D11Texture2D,
    consumer: ID3D11Texture2D,
}
struct PatternSource {
    producer: D3dDevice,
    consumer: D3dDevice,
    fence: ID3D11Fence,
    consumer_fence: ID3D11Fence,
    slots: Vec<Arc<SharedTexture>>,
    event: ID3D11Query,
}

impl PatternSource {
    fn new(consumer: D3dDevice) -> ProbeResult<Self> {
        unsafe {
            // A second device on the exact adapter exercises the actual NT-handle/fence handoff.
            let dxgi: IDXGIDevice = consumer.device().cast()?;
            let adapter = dxgi.GetAdapter()?;
            let mut device = None;
            D3D11CreateDevice(
                &adapter,
                D3D_DRIVER_TYPE_UNKNOWN,
                HMODULE::default(),
                D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                None,
                D3D11_SDK_VERSION,
                Some(&mut device),
                None,
                None,
            )?;
            let producer = D3dDevice::from_device(device.ok_or("no producer device")?)?;
            let device5: ID3D11Device5 = producer.device().cast()?;
            let mut fence = None;
            device5.CreateFence(0, D3D11_FENCE_FLAG_SHARED, &mut fence)?;
            let fence: ID3D11Fence = fence.ok_or("no shared fence")?;
            let raw = fence.CreateSharedHandle(None, 0x10000000, PCWSTR::null())?; // GENERIC_ALL
            let handle = OwnedHandle::from_raw_handle(raw.0);
            let consumer_fence = consumer.import_fence(&handle, 0)?.fence;
            let mut event = None;
            producer.device().CreateQuery(
                &D3D11_QUERY_DESC {
                    Query: D3D11_QUERY_EVENT,
                    MiscFlags: 0,
                },
                Some(&mut event),
            )?;
            Ok(Self {
                producer,
                consumer,
                fence,
                consumer_fence,
                slots: Vec::new(),
                event: event.ok_or("no query")?,
            })
        }
    }
    fn frame(
        &mut self,
        p: &EncoderParams,
        input: &str,
        index: u64,
        complete: bool,
    ) -> ProbeResult<GpuFrame> {
        let nv12 = input == "nv12";
        let (width, height) = if nv12 {
            (p.width.div_ceil(16) * 16, p.height.div_ceil(16) * 16)
        } else {
            (p.width, p.height)
        };
        let format = if nv12 {
            DXGI_FORMAT_NV12
        } else {
            DXGI_FORMAT_B8G8R8A8_UNORM
        };
        let slot = self
            .slots
            .iter()
            .find(|s| {
                Arc::strong_count(s) == 1 && {
                    let d = d3d::texture_desc(&s.producer);
                    d.Width == width && d.Height == height && d.Format == format
                }
            })
            .cloned();
        let slot = match slot {
            Some(slot) => slot,
            None => {
                if self.slots.len() >= 35 {
                    return Err(
                        "probe producer pool exhausted; a backend retained too many leases".into(),
                    );
                }
                let desc = D3D11_TEXTURE2D_DESC {
                    Width: width,
                    Height: height,
                    MipLevels: 1,
                    ArraySize: 1,
                    Format: format,
                    SampleDesc: DXGI_SAMPLE_DESC {
                        Count: 1,
                        Quality: 0,
                    },
                    Usage: D3D11_USAGE_DEFAULT,
                    BindFlags: D3D11_BIND_RENDER_TARGET.0 as u32,
                    MiscFlags: D3D11_RESOURCE_MISC_SHARED_NTHANDLE.0 as u32
                        | D3D11_RESOURCE_MISC_SHARED.0 as u32,
                    ..Default::default()
                };
                unsafe {
                    let mut texture = None;
                    self.producer
                        .device()
                        .CreateTexture2D(&desc, None, Some(&mut texture))?;
                    let producer = texture.ok_or("no producer texture")?;
                    let resource: IDXGIResource1 = producer.cast()?;
                    let raw = resource.CreateSharedHandle(
                        None,
                        (DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE).0,
                        PCWSTR::null(),
                    )?;
                    let handle = OwnedHandle::from_raw_handle(raw.0);
                    let consumer = self.consumer.import_texture(&handle)?;
                    let slot = Arc::new(SharedTexture { producer, consumer });
                    self.slots.push(slot.clone());
                    slot
                }
            }
        };
        let pixels = if nv12 {
            nv12_pattern(p, width, height, index)
        } else {
            bgra_pattern(p, index)
        };
        self.producer.upload(&slot.producer, &pixels)?;
        unsafe {
            let context: ID3D11DeviceContext4 = self.producer.context().cast()?;
            context.Signal(&self.fence, index + 1)?;
            if complete {
                context.End(&self.event);
            }
            context.Flush();
            if complete {
                let start = Instant::now();
                let mut done = 0u32;
                loop {
                    context.GetData(
                        &self.event,
                        Some(std::ptr::from_mut(&mut done).cast()),
                        size_of::<u32>() as u32,
                        D3D11_ASYNC_GETDATA_DONOTFLUSH.0 as u32,
                    )?;
                    if done != 0 {
                        break;
                    }
                    if start.elapsed() > Duration::from_secs(2) {
                        return Err("upload GPU completion timeout".into());
                    }
                    std::thread::yield_now();
                }
            }
        }
        Ok(GpuFrame {
            texture: slot.consumer.clone(),
            width: p.width,
            height: p.height,
            wait: Some(FenceWait {
                fence: self.consumer_fence.clone(),
                value: index + 1,
            }),
            lease: slot,
        })
    }
}

fn nv12_pattern(p: &EncoderParams, width: u32, height: u32, index: u64) -> Vec<u8> {
    let (w, h, stride) = (p.width as usize, p.height as usize, width as usize);
    let mut pixels = vec![128; stride * height as usize * 3 / 2];
    pixels[..stride * height as usize].fill(16);
    let offset = (index * 13) as usize;
    for y in 0..h {
        for x in 0..w {
            let checker = (((x + offset) / 48 + y / 48) % 2) * 40;
            pixels[y * stride + x] = (32 + x * 150 / w + checker) as u8;
        }
    }
    for y in 0..h / 2 {
        for x in 0..w / 2 {
            let i = stride * height as usize + y * stride + x * 2;
            pixels[i] = (80 + ((x + offset / 2) / 32) % 80) as u8;
            pixels[i + 1] = (80 + ((y + offset / 3) / 32) % 80) as u8;
        }
    }
    pixels
}

fn bgra_pattern(p: &EncoderParams, index: u64) -> Vec<u8> {
    let (w, h) = (p.width as usize, p.height as usize);
    let mut pixels = vec![0; w * h * 4];
    for y in 0..h {
        for x in 0..w {
            let i = (y * w + x) * 4;
            let checker = ((((x + index as usize * 13) / 48 + y / 48) % 2) * 100) as u8;
            pixels[i] = (x * 150 / w) as u8;
            pixels[i + 1] = (y * 150 / h) as u8;
            pixels[i + 2] = 50 + checker;
            pixels[i + 3] = 255;
        }
    }
    pixels
}

fn cpu_pattern(p: &EncoderParams, index: u64) -> Vec<u8> {
    let nv12 = nv12_pattern(p, p.width, p.height, index);
    let y = (p.width * p.height) as usize;
    let mut i420 = vec![0; y * 3 / 2];
    i420[..y].copy_from_slice(&nv12[..y]);
    for i in 0..y / 4 {
        i420[y + i] = nv12[y + i * 2];
        i420[y + y / 4 + i] = nv12[y + i * 2 + 1];
    }
    i420
}
fn cpu_frame<'a>(p: &EncoderParams, pixels: &'a [u8]) -> CpuFrame<'a> {
    let y = (p.width * p.height) as usize;
    CpuFrame {
        width: p.width,
        height: p.height,
        planes: [&pixels[..y], &pixels[y..y + y / 4], &pixels[y + y / 4..]],
        strides: [p.width as usize, p.width as usize / 2, p.width as usize / 2],
    }
}

fn run(
    mut encoder: Box<dyn Encoder>,
    device: Option<D3dDevice>,
    options: &Options,
) -> ProbeResult<()> {
    let backend = encoder.backend();
    let p = &options.params;
    println!(
        "{backend}: adapter={:?}; {}",
        device.as_ref().map(D3dDevice::adapter),
        encoder.diagnostics()
    );
    let path = options
        .out
        .join(format!("{backend}-{}.h264", options.input));
    let mut output = BufWriter::new(File::create(&path)?);
    let mut csv = BufWriter::new(File::create(path.with_extension("csv"))?);
    writeln!(csv, "frame,timestamp_us,encode_ms,bytes,frame_type")?;
    let mut source = if options.input == "cpu" {
        None
    } else {
        Some(PatternSource::new(
            device.clone().ok_or("GPU probe needs D3D11 hardware")?,
        )?)
    };
    let mut decoder = Decoder::new()?;
    let mut times = Vec::new();
    let (mut bytes, mut encoded, mut decoded, mut idrs) = (0usize, 0u32, 0u32, 0u32);
    for index in 0..options.frames {
        let timestamp_us = index as u64 * 1_000_000 / p.fps as u64;
        let gpu = if let Some(source) = &mut source {
            Some(source.frame(p, &options.input, index as u64, options.upload_complete)?)
        } else {
            None
        };
        let cpu = if source.is_none() {
            Some(cpu_pattern(p, index as u64))
        } else {
            None
        };
        let data = if let Some(gpu) = &gpu {
            FrameData::Gpu(gpu)
        } else {
            FrameData::Cpu(cpu_frame(p, cpu.as_ref().unwrap()))
        };
        let start = Instant::now();
        let result = encoder.encode(InputFrame { timestamp_us, data })?;
        let ms = start.elapsed().as_secs_f64() * 1000.0;
        times.push(ms);
        if let Some(au) = result {
            if au.timestamp_us != timestamp_us {
                return Err("AU timestamp mismatch".into());
            }
            bytes += au.annex_b.len();
            encoded += 1;
            idrs += u32::from(au.frame_type == FrameType::Idr);
            output.write_all(&au.annex_b)?;
            writeln!(
                csv,
                "{index},{timestamp_us},{ms:.6},{},{:?}",
                au.annex_b.len(),
                au.frame_type
            )?;
            if let Some(frame) = decoder.decode(&au.annex_b)? {
                if frame.dimensions() != (p.width as usize, p.height as usize) {
                    return Err("decoded resolution mismatch".into());
                }
                decoded += 1;
            }
        } else {
            writeln!(csv, "{index},{timestamp_us},{ms:.6},0,skip")?;
        }
    }
    decoded += decoder.flush_remaining()?.len() as u32;
    if decoded != encoded {
        return Err(format!("decode count mismatch: encoded={encoded} decoded={decoded}").into());
    }
    output.flush()?;
    csv.flush()?;
    times.sort_by(f64::total_cmp);
    let rank = |q: f64| times[((times.len() as f64 * q).ceil() as usize).saturating_sub(1)];
    println!(
        "{backend}: input={} frames={} encoded={encoded} decoded={decoded} skipped={} idrs={idrs} P50={:.3}ms P99={:.3}ms bitrate={:.3}Mbps bytes={bytes} output={}",
        options.input,
        options.frames,
        options.frames - encoded,
        rank(0.5),
        rank(0.99),
        bytes as f64 * 8.0 * p.fps as f64 / options.frames as f64 / 1e6,
        path.display()
    );
    println!(
        "timing: source generation, source-device upload submission, file writes and decode excluded; GPU upload completion {} encoder timer (GPU input only); NV12 hardware includes shared-fence Wait; BGRA adds VPP; CPU hardware adds repack/upload; software GPU adds staging/conversion",
        if options.upload_complete {
            "excluded from"
        } else {
            "may be included in"
        }
    );
    ffprobe(&path)?;
    if options.controls {
        check_controls(encoder, device, options)?;
    }
    Ok(())
}

fn ffprobe(path: &Path) -> ProbeResult<()> {
    let program = std::env::var_os("RE_FFPROBE").unwrap_or_else(|| "ffprobe".into());
    let output = match Command::new(program)
        .args([
            "-v",
            "error",
            "-count_frames",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=codec_name,profile,level,width,height,nb_read_frames",
            "-of",
            "compact",
        ])
        .arg(path)
        .output()
    {
        Ok(result) => result,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
            println!("ffprobe absent; OpenH264 fully decoded every AU");
            return Ok(());
        }
        Err(e) => return Err(e.into()),
    };
    if !output.status.success() {
        return Err(format!(
            "ffprobe failed: {}",
            String::from_utf8_lossy(&output.stderr)
        )
        .into());
    }
    println!(
        "ffprobe: {}",
        String::from_utf8_lossy(&output.stdout).trim()
    );
    Ok(())
}

fn check_controls(
    mut encoder: Box<dyn Encoder>,
    device: Option<D3dDevice>,
    options: &Options,
) -> ProbeResult<()> {
    // Independent CPU-input checks exercise upload as well as software input, with a fresh sequence.
    let backend = encoder.backend();
    let mut decoder = Decoder::new()?;
    let mut timestamp = options.frames as u64 * 1_000_000 / options.params.fps as u64 + 1_000_000;
    encoder.force_idr();
    let forced = control_frame(&mut *encoder, &mut timestamp, &mut decoder)?;
    if forced != FrameType::Idr {
        return Err("force-IDR did not emit IDR".into());
    }
    let mut p = encoder.params().clone();
    p.target_bitrate = (p.max_bitrate * 3 / 4).max(p.min_bitrate);
    let outcome = encoder.reconfigure(p.clone())?;
    println!("{backend} control bitrate: {outcome:?}");
    for _ in 0..10 {
        let kind = control_frame(&mut *encoder, &mut timestamp, &mut decoder)?;
        if outcome == ReconfigureOutcome::Seamless && kind == FrameType::Idr {
            return Err("bitrate reset inserted unexpected IDR".into());
        }
    }
    p = encoder.params().clone();
    p.fps = 50;
    let outcome = encoder.reconfigure(p)?;
    println!("{backend} control fps: {outcome:?}");
    for _ in 0..10 {
        let kind = control_frame(&mut *encoder, &mut timestamp, &mut decoder)?;
        if outcome == ReconfigureOutcome::Seamless && kind == FrameType::Idr {
            return Err("fps reset inserted unexpected IDR".into());
        }
    }
    p = encoder.params().clone();
    p.target_bitrate = p.max_bitrate;
    println!(
        "{backend} control rate increase: {:?}",
        encoder.reconfigure(p)?
    );
    control_frame(&mut *encoder, &mut timestamp, &mut decoder)?;
    p = encoder.params().clone();
    p.width = 1280;
    p.height = 720;
    p.fps = 60;
    if encoder.reconfigure(p)? != ReconfigureOutcome::NewSequence {
        return Err("resize did not create new sequence".into());
    }
    // Recreation resets the local timestamp guard; caller's capture clock remains monotonic.
    if control_frame(&mut *encoder, &mut timestamp, &mut decoder)? != FrameType::Idr {
        return Err("resize did not start with IDR".into());
    }
    println!(
        "{backend} controls: force-IDR, bitrate decrease/increase, FPS, resize + decode PASS; {}",
        encoder.diagnostics()
    );
    drop(device);
    Ok(())
}

fn control_frame(
    encoder: &mut dyn Encoder,
    timestamp: &mut u64,
    decoder: &mut Decoder,
) -> ProbeResult<FrameType> {
    let p = encoder.params().clone();
    let pixels = cpu_pattern(&p, *timestamp / 16667);
    *timestamp += 1_000_000 / p.fps as u64;
    let au = encoder
        .encode(InputFrame {
            timestamp_us: *timestamp,
            data: FrameData::Cpu(cpu_frame(&p, &pixels)),
        })?
        .ok_or("control frame skipped")?;
    let frame = decoder
        .decode(&au.annex_b)?
        .ok_or("control AU did not decode")?;
    if frame.dimensions() != (p.width as usize, p.height as usize) {
        return Err("control decoded dimensions mismatch".into());
    }
    Ok(au.frame_type)
}
