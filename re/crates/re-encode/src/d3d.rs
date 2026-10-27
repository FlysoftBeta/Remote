//! D3D11 import and GPU conversion. All context access must be serialized with
//! capture work; do not hold the D3D multithread lock across an encoder call.
use crate::{CpuFrame, Error, FenceWait, GpuFrame, Result, input::OwnedI420};
use std::{
    mem::ManuallyDrop,
    os::windows::io::{AsHandle, AsRawHandle},
    sync::Arc,
};
use windows::{
    Win32::{
        Foundation::{HANDLE, HMODULE, RECT},
        Graphics::{
            Direct3D::D3D_DRIVER_TYPE_UNKNOWN,
            Direct3D10::ID3D10Multithread,
            Direct3D11::*,
            Dxgi::{Common::*, *},
        },
    },
    core::Interface,
};

#[derive(Clone, Debug)]
pub struct AdapterInfo {
    pub vendor_id: u32,
    pub device_id: u32,
    pub luid: u64,
    pub description: String,
}

#[derive(Clone)]
pub struct D3dDevice {
    device: ID3D11Device,
    context: ID3D11DeviceContext,
    adapter: AdapterInfo,
}

impl D3dDevice {
    /// Pick the first hardware adapter, optionally restricted by PCI vendor ID.
    pub fn create(vendor: Option<u32>) -> Result<Self> {
        unsafe {
            let factory: IDXGIFactory1 = CreateDXGIFactory1()?;
            for index in 0.. {
                let adapter = match factory.EnumAdapters1(index) {
                    Ok(adapter) => adapter,
                    Err(e) if e.code() == DXGI_ERROR_NOT_FOUND => break,
                    Err(e) => return Err(e.into()),
                };
                let desc = adapter.GetDesc1()?;
                if desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE.0 as u32 != 0
                    || vendor.is_some_and(|v| v != desc.VendorId)
                {
                    continue;
                }
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
                return Self::from_device(
                    device.ok_or_else(|| Error::Unavailable("D3D11 returned no device".into()))?,
                );
            }
        }
        Err(Error::Unavailable(format!(
            "no hardware D3D11 adapter matching vendor {vendor:?}"
        )))
    }

    pub fn from_device(device: ID3D11Device) -> Result<Self> {
        unsafe {
            if device.GetCreationFlags() & D3D11_CREATE_DEVICE_VIDEO_SUPPORT.0 == 0 {
                return Err(Error::Invalid("D3D11 device needs VIDEO_SUPPORT".into()));
            }
            let context = device.GetImmediateContext()?;
            let multithread: ID3D10Multithread = context.cast()?;
            let _ = multithread.SetMultithreadProtected(true);
            let dxgi: IDXGIDevice = device.cast()?;
            let desc = dxgi.GetAdapter()?.GetDesc()?;
            let end = desc
                .Description
                .iter()
                .position(|&c| c == 0)
                .unwrap_or(desc.Description.len());
            let adapter = AdapterInfo {
                vendor_id: desc.VendorId,
                device_id: desc.DeviceId,
                luid: (desc.AdapterLuid.HighPart as u32 as u64) << 32
                    | desc.AdapterLuid.LowPart as u64,
                description: String::from_utf16_lossy(&desc.Description[..end]),
            };
            Ok(Self {
                device,
                context,
                adapter,
            })
        }
    }

    pub fn device(&self) -> &ID3D11Device {
        &self.device
    }
    pub fn context(&self) -> &ID3D11DeviceContext {
        &self.context
    }
    pub fn adapter(&self) -> &AdapterInfo {
        &self.adapter
    }

    /// Import once per capture buffer token, then cache the COM object at the caller.
    /// The handle is borrowed and remains owned by the caller.
    pub fn import_texture(&self, handle: &impl AsHandle) -> Result<ID3D11Texture2D> {
        unsafe {
            let device: ID3D11Device1 = self.device.cast()?;
            Ok(device.OpenSharedResource1(HANDLE(handle.as_handle().as_raw_handle()))?)
        }
    }

    pub fn import_fence(&self, handle: &impl AsHandle, value: u64) -> Result<FenceWait> {
        unsafe {
            let device: ID3D11Device5 = self.device.cast()?;
            let mut fence = None;
            device.OpenSharedFence(HANDLE(handle.as_handle().as_raw_handle()), &mut fence)?;
            Ok(FenceWait {
                fence: fence
                    .ok_or_else(|| Error::Backend("OpenSharedFence returned null".into()))?,
                value,
            })
        }
    }

    /// Queues a GPU wait before any source read; never CPU-polls the shared fence.
    pub fn wait(&self, wait: &FenceWait) -> Result<()> {
        unsafe {
            let context: ID3D11DeviceContext4 = self.context.cast()?;
            context.Wait(&wait.fence, wait.value)?;
        }
        Ok(())
    }

    pub fn create_texture(
        &self,
        format: DXGI_FORMAT,
        width: u32,
        height: u32,
        bind_flags: u32,
    ) -> Result<ID3D11Texture2D> {
        if width == 0 || height == 0 || width > 4096 || height > 4096 {
            return Err(Error::Invalid("invalid texture dimensions".into()));
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
            BindFlags: bind_flags,
            ..Default::default()
        };
        unsafe { self.texture(&desc) }
    }

    unsafe fn texture(&self, desc: &D3D11_TEXTURE2D_DESC) -> Result<ID3D11Texture2D> {
        let mut texture = None;
        unsafe {
            self.device
                .CreateTexture2D(desc, None, Some(&mut texture))?;
        }
        texture.ok_or_else(|| Error::Backend("CreateTexture2D returned null".into()))
    }

    /// Upload a complete tightly packed BGRA/NV12 texture (including coded padding).
    pub fn upload(&self, texture: &ID3D11Texture2D, pixels: &[u8]) -> Result<()> {
        let desc = texture_desc(texture);
        let (pitch, rows) = match desc.Format {
            DXGI_FORMAT_B8G8R8A8_UNORM => (desc.Width as usize * 4, desc.Height as usize),
            DXGI_FORMAT_NV12 => (desc.Width as usize, desc.Height as usize * 3 / 2),
            _ => return Err(Error::Invalid("upload expects BGRA or NV12".into())),
        };
        if pixels.len() < pitch * rows
            || desc.Usage != D3D11_USAGE_DEFAULT
            || desc.ArraySize != 1
            || desc.MipLevels != 1
            || desc.SampleDesc.Count != 1
        {
            return Err(Error::Invalid("upload buffer/texture mismatch".into()));
        }
        self.check_owner(texture)?;
        unsafe {
            self.context.UpdateSubresource(
                texture,
                0,
                None,
                pixels.as_ptr().cast(),
                pitch as u32,
                0,
            );
        }
        Ok(())
    }

    fn check_owner(&self, texture: &ID3D11Texture2D) -> Result<()> {
        if unsafe { texture.GetDevice()? }.as_raw() != self.device.as_raw() {
            return Err(Error::Invalid(
                "texture must be imported on the encoder's exact D3D11 device".into(),
            ));
        }
        Ok(())
    }

    pub(crate) fn check_frame(&self, frame: &GpuFrame) -> Result<D3D11_TEXTURE2D_DESC> {
        self.check_owner(&frame.texture)?;
        let desc = texture_desc(&frame.texture);
        if frame.width == 0
            || frame.height == 0
            || !frame.width.is_multiple_of(2)
            || !frame.height.is_multiple_of(2)
            || u64::from(frame.width) * u64::from(frame.height) > 1920 * 1080
            || desc.Width < frame.width
            || desc.Height < frame.height
            || desc.Width > 4096
            || desc.Height > 4096
            || desc.ArraySize != 1
            || desc.MipLevels != 1
            || desc.SampleDesc.Count != 1
            || ![DXGI_FORMAT_NV12, DXGI_FORMAT_B8G8R8A8_UNORM].contains(&desc.Format)
        {
            return Err(Error::Invalid(
                "GPU input requires single-subresource BGRA/NV12 with even visible dimensions"
                    .into(),
            ));
        }
        Ok(desc)
    }
}

pub fn texture_desc(texture: &ID3D11Texture2D) -> D3D11_TEXTURE2D_DESC {
    let mut desc = D3D11_TEXTURE2D_DESC::default();
    unsafe {
        texture.GetDesc(&mut desc);
    }
    desc
}

struct Surface {
    texture: ID3D11Texture2D,
}
struct ConversionLease {
    _surface: Arc<Surface>,
    _source: Option<GpuFrame>,
}
struct Vpp {
    key: (u32, u32, u32, u32, u32),
    video: ID3D11VideoDevice,
    context: ID3D11VideoContext1,
    enumerator: ID3D11VideoProcessorEnumerator,
    processor: ID3D11VideoProcessor,
}

#[derive(Default)]
pub(crate) struct FrameConverter {
    surfaces: Vec<Arc<Surface>>,
    vpp: Option<Vpp>,
    staging: Option<ID3D11Texture2D>,
}

impl FrameConverter {
    fn surface(&mut self, device: &D3dDevice, width: u32, height: u32) -> Result<Arc<Surface>> {
        self.surfaces.retain(|slot| {
            let desc = texture_desc(&slot.texture);
            (desc.Width == width && desc.Height == height) || Arc::strong_count(slot) > 1
        });
        if let Some(slot) = self.surfaces.iter().find(|slot| {
            Arc::strong_count(slot) == 1 && {
                let d = texture_desc(&slot.texture);
                d.Width == width && d.Height == height
            }
        }) {
            return Ok(slot.clone());
        }
        if self.surfaces.len() >= 35 {
            return Err(Error::PoolBusy);
        }
        let slot = Arc::new(Surface {
            texture: device.create_texture(
                DXGI_FORMAT_NV12,
                width,
                height,
                D3D11_BIND_RENDER_TARGET.0 as u32,
            )?,
        });
        self.surfaces.push(slot.clone());
        Ok(slot)
    }

    pub fn gpu(&mut self, device: &D3dDevice, frame: &GpuFrame, fps: u32) -> Result<GpuFrame> {
        let desc = device.check_frame(frame)?;
        let width = frame.width.div_ceil(16) * 16;
        let height = frame.height.div_ceil(16) * 16;
        if let Some(wait) = &frame.wait {
            device.wait(wait)?;
        }
        if desc.Format == DXGI_FORMAT_NV12
            && desc.Width == width
            && desc.Height == height
            && desc.Usage == D3D11_USAGE_DEFAULT
            && desc.BindFlags & D3D11_BIND_RENDER_TARGET.0 as u32 != 0
        {
            let mut ready = frame.clone();
            ready.wait = None;
            unsafe {
                device.context.Flush();
            }
            return Ok(ready);
        }
        let slot = self.surface(device, width, height)?;
        if desc.Format == DXGI_FORMAT_NV12 {
            let region = D3D11_BOX {
                left: 0,
                top: 0,
                front: 0,
                right: frame.width,
                bottom: frame.height,
                back: 1,
            };
            unsafe {
                device.context.CopySubresourceRegion(
                    &slot.texture,
                    0,
                    0,
                    0,
                    0,
                    &frame.texture,
                    0,
                    Some(&region),
                );
            }
        } else {
            let key = (desc.Width, desc.Height, frame.width, frame.height, fps);
            if self.vpp.as_ref().is_none_or(|v| v.key != key) {
                self.vpp = Some(Vpp::new(device, key)?);
            }
            self.vpp.as_ref().unwrap().convert(frame, &slot.texture)?;
        }
        unsafe {
            device.context.Flush();
        }
        Ok(GpuFrame {
            texture: slot.texture.clone(),
            width: frame.width,
            height: frame.height,
            wait: None,
            lease: Arc::new(ConversionLease {
                _surface: slot,
                _source: Some(frame.clone()),
            }),
        })
    }

    pub fn cpu(&mut self, device: &D3dDevice, frame: CpuFrame<'_>) -> Result<GpuFrame> {
        frame.validate()?;
        let width = frame.width.div_ceil(16) * 16;
        let height = frame.height.div_ceil(16) * 16;
        let slot = self.surface(device, width, height)?;
        let mut pixels = vec![128; (width * height * 3 / 2) as usize];
        pixels[..(width * height) as usize].fill(16);
        for row in 0..frame.height as usize {
            pixels[row * width as usize..row * width as usize + frame.width as usize]
                .copy_from_slice(
                    &frame.planes[0]
                        [row * frame.strides[0]..row * frame.strides[0] + frame.width as usize],
                );
        }
        for row in 0..frame.height as usize / 2 {
            for col in 0..frame.width as usize / 2 {
                let out = (width * height) as usize + row * width as usize + col * 2;
                pixels[out] = frame.planes[1][row * frame.strides[1] + col];
                pixels[out + 1] = frame.planes[2][row * frame.strides[2] + col];
            }
        }
        device.upload(&slot.texture, &pixels)?;
        unsafe {
            device.context.Flush();
        }
        Ok(GpuFrame {
            texture: slot.texture.clone(),
            width: frame.width,
            height: frame.height,
            wait: None,
            lease: Arc::new(ConversionLease {
                _surface: slot,
                _source: None,
            }),
        })
    }

    pub fn readback(&mut self, device: &D3dDevice, frame: &GpuFrame) -> Result<OwnedI420> {
        let mut desc = device.check_frame(frame)?;
        if let Some(wait) = &frame.wait {
            device.wait(wait)?;
        }
        if self.staging.as_ref().is_none_or(|t| {
            let old = texture_desc(t);
            old.Width != desc.Width || old.Height != desc.Height || old.Format != desc.Format
        }) {
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.MiscFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ.0 as u32;
            self.staging = Some(unsafe { device.texture(&desc)? });
        }
        let staging = self.staging.as_ref().unwrap();
        let mut mapped = D3D11_MAPPED_SUBRESOURCE::default();
        unsafe {
            device.context.CopyResource(staging, &frame.texture);
            device
                .context
                .Map(staging, 0, D3D11_MAP_READ, 0, Some(&mut mapped))?;
        }
        let converted = unsafe { unpack(&mapped, &desc, frame.width, frame.height) };
        unsafe {
            device.context.Unmap(staging, 0);
        }
        converted
    }
}

impl Vpp {
    fn new(device: &D3dDevice, key: (u32, u32, u32, u32, u32)) -> Result<Self> {
        let (sw, sh, w, h, fps) = key;
        let content = D3D11_VIDEO_PROCESSOR_CONTENT_DESC {
            InputFrameFormat: D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE,
            InputFrameRate: DXGI_RATIONAL {
                Numerator: fps,
                Denominator: 1,
            },
            InputWidth: sw,
            InputHeight: sh,
            OutputFrameRate: DXGI_RATIONAL {
                Numerator: fps,
                Denominator: 1,
            },
            OutputWidth: w.div_ceil(16) * 16,
            OutputHeight: h.div_ceil(16) * 16,
            Usage: D3D11_VIDEO_USAGE_PLAYBACK_NORMAL,
        };
        unsafe {
            let video: ID3D11VideoDevice = device.device.cast()?;
            let context: ID3D11VideoContext1 = device.context.cast()?;
            let enumerator = video.CreateVideoProcessorEnumerator(&content)?;
            let formats: ID3D11VideoProcessorEnumerator1 = enumerator.cast()?;
            if !formats
                .CheckVideoProcessorFormatConversion(
                    DXGI_FORMAT_B8G8R8A8_UNORM,
                    DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,
                    DXGI_FORMAT_NV12,
                    DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709,
                )?
                .as_bool()
            {
                return Err(Error::Unavailable(
                    "D3D11 BT.709 BGRA->NV12 conversion unsupported".into(),
                ));
            }
            let processor = video.CreateVideoProcessor(&enumerator, 0)?;
            let target = RECT {
                left: 0,
                top: 0,
                right: content.OutputWidth as i32,
                bottom: content.OutputHeight as i32,
            };
            let visible = RECT {
                left: 0,
                top: 0,
                right: w as i32,
                bottom: h as i32,
            };
            context.VideoProcessorSetOutputTargetRect(&processor, true, Some(&target));
            context.VideoProcessorSetStreamSourceRect(&processor, 0, true, Some(&visible));
            context.VideoProcessorSetStreamDestRect(&processor, 0, true, Some(&visible));
            context.VideoProcessorSetStreamFrameFormat(
                &processor,
                0,
                D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE,
            );
            context.VideoProcessorSetStreamAutoProcessingMode(&processor, 0, false);
            context.VideoProcessorSetStreamColorSpace1(
                &processor,
                0,
                DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,
            );
            context.VideoProcessorSetOutputColorSpace1(
                &processor,
                DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709,
            );
            let black = D3D11_VIDEO_COLOR {
                Anonymous: D3D11_VIDEO_COLOR_0 {
                    YCbCr: D3D11_VIDEO_COLOR_YCbCrA {
                        Y: 16.0 / 255.0,
                        Cb: 128.0 / 255.0,
                        Cr: 128.0 / 255.0,
                        A: 1.0,
                    },
                },
            };
            context.VideoProcessorSetOutputBackgroundColor(&processor, true, &black);
            Ok(Self {
                key,
                video,
                context,
                enumerator,
                processor,
            })
        }
    }

    fn convert(&self, frame: &GpuFrame, output: &ID3D11Texture2D) -> Result<()> {
        unsafe {
            let input_desc = D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC {
                ViewDimension: D3D11_VPIV_DIMENSION_TEXTURE2D,
                ..Default::default()
            };
            let output_desc = D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC {
                ViewDimension: D3D11_VPOV_DIMENSION_TEXTURE2D,
                ..Default::default()
            };
            let mut input = None;
            let mut view = None;
            self.video.CreateVideoProcessorInputView(
                &frame.texture,
                &self.enumerator,
                &input_desc,
                Some(&mut input),
            )?;
            self.video.CreateVideoProcessorOutputView(
                output,
                &self.enumerator,
                &output_desc,
                Some(&mut view),
            )?;
            let view = view.ok_or_else(|| Error::Backend("VPP returned null view".into()))?;
            let mut stream = D3D11_VIDEO_PROCESSOR_STREAM {
                Enable: true.into(),
                pInputSurface: ManuallyDrop::new(input),
                ..Default::default()
            };
            let result = self.context.VideoProcessorBlt(
                &self.processor,
                &view,
                0,
                std::slice::from_ref(&stream),
            );
            ManuallyDrop::drop(&mut stream.pInputSurface);
            result?;
            Ok(())
        }
    }
}

unsafe fn unpack(
    mapped: &D3D11_MAPPED_SUBRESOURCE,
    desc: &D3D11_TEXTURE2D_DESC,
    width: u32,
    height: u32,
) -> Result<OwnedI420> {
    let (w, h, pitch) = (width as usize, height as usize, mapped.RowPitch as usize);
    let bytes_per_row = if desc.Format == DXGI_FORMAT_NV12 {
        desc.Width as usize
    } else {
        desc.Width as usize * 4
    };
    if mapped.pData.is_null() || pitch < bytes_per_row {
        return Err(Error::Backend("invalid D3D11 readback row pitch".into()));
    }
    let rows = if desc.Format == DXGI_FORMAT_NV12 {
        desc.Height as usize * 3 / 2
    } else {
        desc.Height as usize
    };
    let source = unsafe { std::slice::from_raw_parts(mapped.pData.cast::<u8>(), pitch * rows) };
    let mut out = OwnedI420::new(width, height);
    let (y, uv) = out.pixels.split_at_mut(w * h);
    let (u, v) = uv.split_at_mut(w * h / 4);
    if desc.Format == DXGI_FORMAT_NV12 {
        for row in 0..h {
            y[row * w..(row + 1) * w].copy_from_slice(&source[row * pitch..row * pitch + w]);
        }
        for row in 0..h / 2 {
            for col in 0..w / 2 {
                let offset = pitch * desc.Height as usize + row * pitch + col * 2;
                u[row * w / 2 + col] = source[offset];
                v[row * w / 2 + col] = source[offset + 1];
            }
        }
    } else {
        for row in 0..h {
            for col in 0..w {
                let p = &source[row * pitch + col * 4..][..3];
                y[row * w + col] =
                    (((47 * p[2] as i32 + 157 * p[1] as i32 + 16 * p[0] as i32 + 128) >> 8) + 16)
                        .clamp(16, 235) as u8;
            }
        }
        for row in 0..h / 2 {
            for col in 0..w / 2 {
                let mut rgb = [0i32; 3];
                for dy in 0..2 {
                    for dx in 0..2 {
                        let p = &source[(row * 2 + dy) * pitch + (col * 2 + dx) * 4..][..3];
                        for i in 0..3 {
                            rgb[i] += p[i] as i32;
                        }
                    }
                }
                let [b, g, r] = rgb.map(|x| (x + 2) / 4);
                u[row * w / 2 + col] =
                    (((-26 * r - 87 * g + 112 * b + 128) >> 8) + 128).clamp(16, 240) as u8;
                v[row * w / 2 + col] =
                    (((112 * r - 102 * g - 10 * b + 128) >> 8) + 128).clamp(16, 240) as u8;
            }
        }
    }
    Ok(out)
}
