use crate::Error;
use crate::{
    d3d::{D3dDevice, FrameConverter},
    *,
};
use openh264_sys2::*;
use std::{ffi::c_void, ptr};

pub(crate) struct Software {
    raw: *mut ISVCEncoder,
    api: DynamicAPI,
    api_description: String,
    params: EncoderParams,
    force_idr: bool,
    device: Option<D3dDevice>,
    converter: FrameConverter,
    last_timestamp: Option<u64>,
    skipped: u64,
    poisoned: bool,
}

fn check(code: i32, operation: &str) -> Result<()> {
    if code == 0 {
        Ok(())
    } else {
        Err(Error::Backend(format!(
            "OpenH264 {operation}, status={code}"
        )))
    }
}

impl Software {
    pub fn new(params: EncoderParams, device: Option<D3dDevice>) -> Result<Self> {
        params.validate()?;
        if params.profile != Profile::ConstrainedBaseline || params.intra_refresh.is_some() {
            return Err(Error::Unavailable("OpenH264 fallback supports constrained baseline and on-demand/periodic IDR; intra refresh is unsupported".into()));
        }
        let (api, api_description) = match std::env::var_os("RE_OPENH264_DLL") {
            Some(path) => {
                // The crate verifies the binary against its known Cisco SHA-256 list.
                let api = DynamicAPI::from_blob_path(&path)
                    .map_err(|e| Error::Unavailable(format!("OpenH264 DLL: {e}")))?;
                (
                    api,
                    format!("Cisco DLL {}", std::path::Path::new(&path).display()),
                )
            }
            None => (DynamicAPI::from_source(), "source build".into()),
        };
        let mut encoder = Self {
            raw: ptr::null_mut(),
            api,
            api_description,
            params,
            device,
            converter: FrameConverter::default(),
            force_idr: true,
            last_timestamp: None,
            skipped: 0,
            poisoned: false,
        };
        unsafe {
            check(
                encoder.api.WelsCreateSVCEncoder(&mut encoder.raw),
                "CreateSVCEncoder",
            )?;
            if encoder.raw.is_null() {
                return Err(Error::Backend("OpenH264 returned null encoder".into()));
            }
            let vtable = &**encoder.raw;
            let mut q = SEncParamExt::default();
            check(
                vtable.GetDefaultParams.unwrap()(encoder.raw, &mut q),
                "GetDefaultParams",
            )?;
            let p = &encoder.params;
            let screen = p.software_screen_content;
            q.iUsageType = if screen {
                SCREEN_CONTENT_REAL_TIME
            } else {
                CAMERA_VIDEO_REAL_TIME
            };
            q.iPicWidth = p.width as i32;
            q.iPicHeight = p.height as i32;
            q.fMaxFrameRate = p.fps as f32;
            q.iTargetBitrate = p.target_bitrate as i32;
            q.iMaxBitrate = p.max_bitrate as i32;
            q.iRCMode = RC_BITRATE_MODE;
            q.iComplexityMode = LOW_COMPLEXITY;
            q.iMultipleThreadIdc = p.software_threads;
            q.iTemporalLayerNum = 1;
            q.iSpatialLayerNum = 1;
            q.iNumRefFrame = 1;
            q.uiIntraPeriod = p.gop_frames.unwrap_or(0);
            q.bEnableFrameSkip = true;
            q.bEnableDenoise = false;
            q.bEnableLongTermReference = false;
            q.bEnableAdaptiveQuant = false;
            q.bEnableBackgroundDetection = !screen;
            q.bEnableSceneChangeDetect = screen;
            q.iMinQp = if screen { 26 } else { 12 };
            q.iMaxQp = if screen { 35 } else { 42 };
            q.bFixRCOverShoot = true;
            q.iIdrBitrateRatio = 400;
            q.bEnableSSEI = false;
            q.bPrefixNalAddingCtrl = false;
            q.eSpsPpsIdStrategy = CONSTANT_ID;
            q.iPaddingFlag = 0;
            let layer = &mut q.sSpatialLayers[0];
            layer.iVideoWidth = p.width as i32;
            layer.iVideoHeight = p.height as i32;
            layer.fFrameRate = p.fps as f32;
            layer.iSpatialBitrate = p.target_bitrate as i32;
            layer.iMaxSpatialBitrate = p.max_bitrate as i32;
            layer.uiProfileIdc = PRO_BASELINE;
            layer.uiLevelIdc = p.avc_level()? as i32;
            layer.sSliceArgument.uiSliceMode = SM_FIXEDSLCNUM_SLICE;
            layer.sSliceArgument.uiSliceNum = p.software_slices.unwrap_or(p.slices);
            layer.bVideoSignalTypePresent = true;
            layer.uiVideoFormat = 5;
            layer.bFullRange = false;
            layer.bColorDescriptionPresent = true;
            layer.uiColorPrimaries = 1;
            layer.uiTransferCharacteristics = 1;
            layer.uiColorMatrix = 1;
            check(
                vtable.InitializeExt.unwrap()(encoder.raw, &q),
                "InitializeExt",
            )?;
            let mut format = videoFormatI420;
            check(
                vtable.SetOption.unwrap()(
                    encoder.raw,
                    ENCODER_OPTION_DATAFORMAT,
                    ptr::from_mut(&mut format).cast(),
                ),
                "SetDataFormat",
            )?;
        }
        Ok(encoder)
    }

    fn encode_cpu(&mut self, frame: CpuFrame<'_>, timestamp_us: u64) -> Result<Option<AccessUnit>> {
        frame.validate()?;
        if frame.width != self.params.width || frame.height != self.params.height {
            return Err(Error::Invalid(
                "frame resolution differs from encoder; reconfigure first".into(),
            ));
        }
        // OpenH264's ABI takes mutable pointers, but it only reads the source picture.
        let picture = SSourcePicture {
            iColorFormat: videoFormatI420,
            iStride: [
                frame.strides[0] as i32,
                frame.strides[1] as i32,
                frame.strides[2] as i32,
                0,
            ],
            pData: [
                frame.planes[0].as_ptr().cast_mut(),
                frame.planes[1].as_ptr().cast_mut(),
                frame.planes[2].as_ptr().cast_mut(),
                ptr::null_mut(),
            ],
            iPicWidth: frame.width as i32,
            iPicHeight: frame.height as i32,
            uiTimeStamp: (timestamp_us / 1000) as i64,
            ..Default::default()
        };
        unsafe {
            let v = &**self.raw;
            if self.force_idr {
                check(
                    v.ForceIntraFrame.unwrap()(self.raw, true),
                    "ForceIntraFrame",
                )?;
            }
            let mut info = SFrameBSInfo::default();
            check(
                v.EncodeFrame.unwrap()(self.raw, &picture, &mut info),
                "EncodeFrame",
            )?;
            if info.eFrameType == videoFrameTypeSkip {
                self.skipped += 1;
                return Ok(None);
            }
            if info.iLayerNum < 0 || info.iLayerNum as usize > info.sLayerInfo.len() {
                return Err(Error::Backend(
                    "OpenH264 returned invalid layer count".into(),
                ));
            }
            let mut data = Vec::new();
            for layer in &info.sLayerInfo[..info.iLayerNum as usize] {
                if layer.iNalCount < 0
                    || layer.iNalCount > 4096
                    || layer.pNalLengthInByte.is_null()
                    || layer.pBsBuf.is_null()
                {
                    return Err(Error::Backend(
                        "OpenH264 returned invalid NAL metadata".into(),
                    ));
                }
                let lengths =
                    std::slice::from_raw_parts(layer.pNalLengthInByte, layer.iNalCount as usize);
                let mut total = 0usize;
                for &n in lengths {
                    if n < 0 {
                        return Err(Error::Backend("OpenH264 negative NAL length".into()));
                    }
                    total = total
                        .checked_add(n as usize)
                        .filter(|&n| n <= 16 * 1024 * 1024)
                        .ok_or_else(|| Error::Backend("OpenH264 access unit too large".into()))?;
                }
                if data.len() + total > 16 * 1024 * 1024 {
                    return Err(Error::Backend("OpenH264 access unit too large".into()));
                }
                data.extend_from_slice(std::slice::from_raw_parts(layer.pBsBuf, total));
            }
            let parsed = validate_access_unit(&data, &self.params)?;
            let frame_type = if parsed == FrameType::Idr {
                FrameType::Idr
            } else if info.eFrameType == videoFrameTypeI {
                FrameType::Intra
            } else {
                FrameType::Predicted
            };
            if frame_type == FrameType::Idr {
                self.force_idr = false;
            }
            Ok(Some(AccessUnit {
                annex_b: data,
                timestamp_us,
                frame_type,
            }))
        }
    }
}

impl Encoder for Software {
    fn backend(&self) -> Backend {
        Backend::OpenH264
    }
    fn params(&self) -> &EncoderParams {
        &self.params
    }
    fn encode(&mut self, frame: InputFrame<'_>) -> Result<Option<AccessUnit>> {
        if self.poisoned {
            return Err(Error::Backend("OpenH264 failed; recreate encoder".into()));
        }
        if frame.timestamp_us > i64::MAX as u64
            || self
                .last_timestamp
                .is_some_and(|last| frame.timestamp_us <= last)
        {
            return Err(Error::Invalid(
                "timestamps must be strictly increasing and fit i64".into(),
            ));
        }
        let result = match frame.data {
            FrameData::Cpu(cpu) => self.encode_cpu(cpu, frame.timestamp_us),
            FrameData::Gpu(gpu) => {
                let device = self
                    .device
                    .as_ref()
                    .ok_or_else(|| Error::Invalid("GPU input needs a supplied D3dDevice".into()))?;
                let cpu = self.converter.readback(device, gpu)?;
                self.encode_cpu(cpu.frame(), frame.timestamp_us)
            }
        };
        if result.is_ok() {
            self.last_timestamp = Some(frame.timestamp_us);
        }
        if matches!(result, Err(Error::Backend(_))) {
            self.poisoned = true;
        }
        result
    }
    fn reconfigure(&mut self, params: EncoderParams) -> Result<ReconfigureOutcome> {
        params.validate()?;
        let mut rates_only = self.params.clone();
        rates_only.target_bitrate = params.target_bitrate;
        rates_only.fps = params.fps;
        if rates_only == params && params.avc_level()? == self.params.avc_level()? {
            // Use the extended option atomically so rate and FPS cannot half-apply.
            unsafe {
                let v = &**self.raw;
                let mut config = SEncParamExt::default();
                check(
                    v.GetOption.unwrap()(
                        self.raw,
                        ENCODER_OPTION_SVC_ENCODE_PARAM_EXT,
                        ptr::from_mut(&mut config).cast(),
                    ),
                    "GetParams",
                )?;
                config.iTargetBitrate = params.target_bitrate as i32;
                config.fMaxFrameRate = params.fps as f32;
                config.sSpatialLayers[0].iSpatialBitrate = params.target_bitrate as i32;
                config.sSpatialLayers[0].fFrameRate = params.fps as f32;
                let code = v.SetOption.unwrap()(
                    self.raw,
                    ENCODER_OPTION_SVC_ENCODE_PARAM_EXT,
                    ptr::from_mut(&mut config).cast::<c_void>(),
                );
                if code != 0 {
                    return Ok(ReconfigureOutcome::Unchanged(format!(
                        "OpenH264 rate/FPS reset rejected: {code}"
                    )));
                }
            }
            self.params = params;
            return Ok(ReconfigureOutcome::Seamless);
        }
        let replacement = Self::new(params, self.device.clone())?;
        *self = replacement;
        Ok(ReconfigureOutcome::NewSequence)
    }
    fn force_idr(&mut self) {
        self.force_idr = true;
    }
    fn diagnostics(&self) -> String {
        format!(
            "OpenH264 {}; camera={} threads={} slices={} RC_BITRATE_MODE skip=true skipped={}; VBV/frame-cap are unsupported hints",
            self.api_description,
            !self.params.software_screen_content,
            self.params.software_threads,
            self.params.software_slices.unwrap_or(self.params.slices),
            self.skipped
        )
    }
}
impl Drop for Software {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            unsafe {
                if let Some(uninitialize) = (**self.raw).Uninitialize {
                    uninitialize(self.raw);
                }
                self.api.WelsDestroySVCEncoder(self.raw);
            }
        }
    }
}
