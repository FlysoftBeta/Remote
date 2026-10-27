use crate::{
    d3d::{D3dDevice, FrameConverter},
    *,
};
use std::{
    ffi::{CStr, c_char, c_void},
    ptr::NonNull,
};
use windows::core::Interface;

#[repr(C)]
struct NativeParams {
    width: u32,
    height: u32,
    fps: u32,
    target_kbps: u32,
    min_kbps: u32,
    max_kbps: u32,
    cbr: u32,
    gop: u32,
    refresh: u32,
    slices: u32,
    vbv_kb: u32,
    cap_bytes: u32,
    low_power: u32,
    tu: u32,
    profile: u32,
    level: u32,
    preset: u32,
}
impl NativeParams {
    fn new(p: &EncoderParams) -> Result<Self> {
        p.validate()?;
        Ok(Self {
            width: p.width,
            height: p.height,
            fps: p.fps,
            target_kbps: p.target_bitrate.div_ceil(1000),
            min_kbps: p.min_bitrate.div_ceil(1000),
            max_kbps: p.max_bitrate.div_ceil(1000),
            cbr: u32::from(p.rate_control == RateControl::Cbr),
            gop: p.gop_frames.unwrap_or(0),
            refresh: p.intra_refresh.unwrap_or(0),
            slices: p.slices,
            vbv_kb: p.vbv_kb(),
            cap_bytes: p.frame_cap_bytes(),
            low_power: u32::from(p.low_power),
            tu: p.target_usage,
            profile: match p.profile {
                Profile::ConstrainedBaseline => 578,
                Profile::Main => 77,
                Profile::High => 100,
            },
            level: p.avc_level()?,
            preset: match p.nvenc_preset {
                NvencPreset::P1 => 1,
                NvencPreset::P2 => 2,
                NvencPreset::P3 => 3,
            },
        })
    }
}
#[repr(C)]
#[derive(Default)]
struct NativeAu {
    data: *const u8,
    size: usize,
    timestamp: u64,
    frame_type: u32,
}

unsafe extern "C" {
    fn re_encoder_create(
        kind: u32,
        device: *mut c_void,
        params: &NativeParams,
        error: *mut c_char,
        capacity: usize,
    ) -> *mut c_void;
    fn re_encoder_destroy(encoder: *mut c_void);
    fn re_encoder_error(encoder: *mut c_void) -> *const c_char;
    fn re_encoder_diagnostics(encoder: *mut c_void) -> *const c_char;
    fn re_encoder_encode(
        encoder: *mut c_void,
        texture: *mut c_void,
        lease: *mut c_void,
        release: extern "C" fn(*mut c_void),
        timestamp: u64,
        idr: u32,
        output: &mut NativeAu,
    ) -> i32;
    fn re_encoder_reset(encoder: *mut c_void, params: &NativeParams) -> i32;
}
struct Native(NonNull<c_void>);
impl Drop for Native {
    fn drop(&mut self) {
        unsafe {
            re_encoder_destroy(self.0.as_ptr());
        }
    }
}
extern "C" fn release_frame(pointer: *mut c_void) {
    // Exactly one Box is transferred per call; C++ releases it after MFX unlock or Close.
    unsafe {
        drop(Box::from_raw(pointer.cast::<GpuFrame>()));
    }
}

pub(crate) struct Hardware {
    native: Option<Native>,
    backend: Backend,
    params: EncoderParams,
    device: D3dDevice,
    converter: FrameConverter,
    force_idr: bool,
    last_timestamp: Option<u64>,
    init_vbv_kb: u32,
    init_cap_bytes: u32,
}
impl Hardware {
    pub fn new(backend: Backend, params: EncoderParams, device: D3dDevice) -> Result<Self> {
        let p = NativeParams::new(&params)?;
        let mut error = [0 as c_char; 4096];
        let raw = unsafe {
            re_encoder_create(
                u32::from(backend == Backend::Nvenc),
                device.device().as_raw(),
                &p,
                error.as_mut_ptr(),
                error.len(),
            )
        };
        let native = NonNull::new(raw).ok_or_else(|| {
            Error::Unavailable(
                unsafe { CStr::from_ptr(error.as_ptr()) }
                    .to_string_lossy()
                    .into_owned(),
            )
        })?;
        Ok(Self {
            native: Some(Native(native)),
            backend,
            params,
            device,
            converter: FrameConverter::default(),
            force_idr: true,
            last_timestamp: None,
            init_vbv_kb: p.vbv_kb,
            init_cap_bytes: p.cap_bytes,
        })
    }
    fn pointer(&self) -> Result<*mut c_void> {
        self.native
            .as_ref()
            .map(|p| p.0.as_ptr())
            .ok_or_else(|| Error::Backend("hardware encoder failed; recreate or fall back".into()))
    }
    fn error(&self) -> Error {
        let message = self
            .native
            .as_ref()
            .map(|p| {
                unsafe { CStr::from_ptr(re_encoder_error(p.0.as_ptr())) }
                    .to_string_lossy()
                    .into_owned()
            })
            .unwrap_or_else(|| "encoder is closed".into());
        Error::Backend(message)
    }
}
impl Encoder for Hardware {
    fn backend(&self) -> Backend {
        self.backend
    }
    fn params(&self) -> &EncoderParams {
        &self.params
    }
    fn encode(&mut self, frame: InputFrame<'_>) -> Result<Option<AccessUnit>> {
        let pointer = self.pointer()?;
        if frame.timestamp_us > i64::MAX as u64
            || self
                .last_timestamp
                .is_some_and(|last| frame.timestamp_us <= last)
        {
            return Err(Error::Invalid(
                "timestamps must be strictly increasing and fit i64".into(),
            ));
        }
        let (width, height) = match frame.data {
            FrameData::Gpu(g) => (g.width, g.height),
            FrameData::Cpu(c) => (c.width, c.height),
        };
        if width != self.params.width || height != self.params.height {
            return Err(Error::Invalid(
                "frame resolution differs from encoder; reconfigure first".into(),
            ));
        }
        let prepared = match frame.data {
            FrameData::Gpu(gpu) => self.converter.gpu(&self.device, gpu, self.params.fps)?,
            FrameData::Cpu(cpu) => self.converter.cpu(&self.device, cpu)?,
        };
        let texture = prepared.texture.as_raw();
        let lease = Box::into_raw(Box::new(prepared)).cast::<c_void>();
        let mut output = NativeAu::default();
        let timestamp = (frame.timestamp_us as u128 * 9 / 100) as u64;
        // Ownership of lease transfers even when the native encode returns an error.
        let code = unsafe {
            re_encoder_encode(
                pointer,
                texture,
                lease,
                release_frame,
                timestamp,
                u32::from(self.force_idr),
                &mut output,
            )
        };
        if code < 0 {
            let error = self.error();
            self.native.take();
            return Err(error);
        }
        if code != 0
            || output.data.is_null()
            || output.size == 0
            || output.size > 16 * 1024 * 1024
            || output.timestamp != timestamp
        {
            self.native.take();
            return Err(Error::Backend(
                "hardware backend returned invalid AU metadata".into(),
            ));
        }
        let data = unsafe { std::slice::from_raw_parts(output.data, output.size) }.to_vec();
        let parsed = match validate_access_unit(&data, &self.params) {
            Ok(t) => t,
            Err(e) => {
                self.native.take();
                return Err(e);
            }
        };
        let frame_type = if parsed == FrameType::Idr {
            FrameType::Idr
        } else if output.frame_type == 1 {
            FrameType::Intra
        } else {
            FrameType::Predicted
        };
        if frame_type == FrameType::Idr {
            self.force_idr = false;
        }
        self.last_timestamp = Some(frame.timestamp_us);
        Ok(Some(AccessUnit {
            annex_b: data,
            timestamp_us: frame.timestamp_us,
            frame_type,
        }))
    }
    fn reconfigure(&mut self, params: EncoderParams) -> Result<ReconfigureOutcome> {
        params.validate()?;
        let mut rates_only = self.params.clone();
        rates_only.target_bitrate = params.target_bitrate;
        rates_only.fps = params.fps;
        if rates_only == params && params.avc_level()? == self.params.avc_level()? {
            let mut raw = NativeParams::new(&params)?;
            raw.vbv_kb = self.init_vbv_kb;
            raw.cap_bytes = self.init_cap_bytes;
            let code = unsafe { re_encoder_reset(self.pointer()?, &raw) };
            match code {
                0 => {
                    let fps_boundary =
                        self.backend == Backend::Vpl && self.params.fps != params.fps;
                    self.params = params;
                    if fps_boundary {
                        self.force_idr = true;
                        Ok(ReconfigureOutcome::NewSequence)
                    } else {
                        Ok(ReconfigureOutcome::Seamless)
                    }
                }
                1 => Ok(ReconfigureOutcome::Unchanged(self.error().to_string())),
                _ => {
                    let error = self.error();
                    self.native.take();
                    Err(error)
                }
            }
        } else {
            let replacement = Self::new(self.backend, params, self.device.clone())?;
            *self = replacement;
            Ok(ReconfigureOutcome::NewSequence)
        }
    }
    fn force_idr(&mut self) {
        self.force_idr = true;
    }
    fn diagnostics(&self) -> String {
        self.native
            .as_ref()
            .map(|p| {
                unsafe { CStr::from_ptr(re_encoder_diagnostics(p.0.as_ptr())) }
                    .to_string_lossy()
                    .into_owned()
            })
            .unwrap_or_else(|| "hardware encoder closed after failure".into())
    }
}
