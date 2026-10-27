use crate::{Error, Result};
use std::{any::Any, sync::Arc};
use windows::Win32::Graphics::Direct3D11::{ID3D11Fence, ID3D11Texture2D};

#[derive(Clone)]
pub struct FenceWait {
    pub fence: ID3D11Fence,
    pub value: u64,
}

/// A producer lease prevents buffer reuse while the backend retains it. A COM
/// reference alone does not protect shared capture-pool contents from overwrite.
/// The fence must be signaled by a live producer, and the texture/fence must be
/// opened on the encoder's exact device. Only one mip/subresource is supported.
#[derive(Clone)]
pub struct GpuFrame {
    pub texture: ID3D11Texture2D,
    pub width: u32,
    pub height: u32,
    pub wait: Option<FenceWait>,
    pub lease: Arc<dyn Any + Send + Sync>,
}

/// Borrowed BT.709 limited-range, even-sized I420 planes. Strides are bytes.
#[derive(Clone, Copy)]
pub struct CpuFrame<'a> {
    pub width: u32,
    pub height: u32,
    pub planes: [&'a [u8]; 3],
    pub strides: [usize; 3],
}

impl CpuFrame<'_> {
    pub fn validate(&self) -> Result<()> {
        if self.width == 0
            || self.height == 0
            || self.width > 4096
            || self.height > 4096
            || !self.width.is_multiple_of(2)
            || !self.height.is_multiple_of(2)
            || u64::from(self.width) * u64::from(self.height) > 1920 * 1080
        {
            return Err(Error::Invalid("invalid CPU frame dimensions".into()));
        }
        for i in 0..3 {
            let width = (self.width >> u32::from(i != 0)) as usize;
            let height = (self.height >> u32::from(i != 0)) as usize;
            let required = self.strides[i]
                .checked_mul(height - 1)
                .and_then(|n| n.checked_add(width));
            if self.strides[i] < width
                || self.strides[i] > i32::MAX as usize
                || required.is_none_or(|n| self.planes[i].len() < n)
            {
                return Err(Error::Invalid(
                    "CPU plane is too short or has invalid stride".into(),
                ));
            }
        }
        Ok(())
    }
}

#[derive(Clone, Copy)]
pub enum FrameData<'a> {
    Gpu(&'a GpuFrame),
    Cpu(CpuFrame<'a>),
}

#[derive(Clone, Copy)]
pub struct InputFrame<'a> {
    pub timestamp_us: u64,
    pub data: FrameData<'a>,
}

pub(crate) struct OwnedI420 {
    pub width: u32,
    pub height: u32,
    pub pixels: Vec<u8>,
}
impl OwnedI420 {
    pub fn new(width: u32, height: u32) -> Self {
        Self {
            width,
            height,
            pixels: vec![0; (width * height * 3 / 2) as usize],
        }
    }
    pub fn frame(&self) -> CpuFrame<'_> {
        let y = (self.width * self.height) as usize;
        CpuFrame {
            width: self.width,
            height: self.height,
            planes: [
                &self.pixels[..y],
                &self.pixels[y..y + y / 4],
                &self.pixels[y + y / 4..],
            ],
            strides: [
                self.width as usize,
                self.width as usize / 2,
                self.width as usize / 2,
            ],
        }
    }
}
