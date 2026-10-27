use crate::{Error, Result};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RateControl {
    Cbr,
    Vbr,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Vbv {
    Frames(u32),
    Milliseconds(u32),
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum FrameCap {
    /// M4 default: one average frame at the negotiated maximum bitrate.
    MaxRateFrame,
    /// Same semantics as --vpl-max-frame-factor: target bitrate basis, init only.
    TargetFactor(f64),
    Bytes(u32),
    Disabled,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Profile {
    ConstrainedBaseline,
    Main,
    High,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NvencPreset {
    P1,
    P2,
    P3,
}

/// Rates are bits/second; dimensions are visible pixels. Input color is BT.709 limited
/// for YUV and BT.709 full range for BGRA. min_bitrate is the caller's adaptation floor.
#[derive(Clone, Debug, PartialEq)]
pub struct EncoderParams {
    pub width: u32,
    pub height: u32,
    pub fps: u32,
    pub target_bitrate: u32,
    pub min_bitrate: u32,
    pub max_bitrate: u32,
    pub rate_control: RateControl,
    /// None means on-demand IDR (VPL represents this as GopPicSize=65535).
    pub gop_frames: Option<u32>,
    /// Vertical refresh cycle; None disables refresh.
    pub intra_refresh: Option<u32>,
    pub slices: u32,
    pub vbv: Vbv,
    pub frame_cap: FrameCap,
    pub low_latency: bool,
    pub low_power: bool,
    pub target_usage: u32,
    pub profile: Profile,
    /// AVC level_idc (e.g. 42). None selects the smallest honest supported level.
    pub level: Option<u32>,
    pub nvenc_preset: NvencPreset,
    /// Cisco fallback's current four-thread/four-slice camera preset.
    /// None makes software honor the shared `slices` field.
    pub software_slices: Option<u32>,
    pub software_threads: u16,
    pub software_screen_content: bool,
}

impl Default for EncoderParams {
    fn default() -> Self {
        Self {
            width: 1920,
            height: 1080,
            fps: 60,
            target_bitrate: 8_000_000,
            min_bitrate: 2_000_000,
            max_bitrate: 8_000_000,
            rate_control: RateControl::Vbr,
            gop_frames: None,
            intra_refresh: None,
            slices: 1,
            vbv: Vbv::Frames(1),
            frame_cap: FrameCap::MaxRateFrame,
            low_latency: true,
            low_power: true,
            target_usage: 7,
            profile: Profile::ConstrainedBaseline,
            level: None,
            nvenc_preset: NvencPreset::P1,
            software_slices: Some(4),
            software_threads: 4,
            software_screen_content: false,
        }
    }
}

impl EncoderParams {
    pub fn validate(&self) -> Result<()> {
        let bad = |message: &str| Err(Error::Invalid(message.into()));
        if self.width == 0
            || self.height == 0
            || self.width > 4096
            || self.height > 4096
            || !self.width.is_multiple_of(2)
            || !self.height.is_multiple_of(2)
            || u64::from(self.width) * u64::from(self.height) > 1920 * 1080
        {
            return bad("require even dimensions <=4096 and <=1920*1080 pixels");
        }
        if !(1..=120).contains(&self.fps) {
            return bad("fps must be 1..120");
        }
        if self.min_bitrate < 1000
            || self.min_bitrate > self.target_bitrate
            || self.target_bitrate > self.max_bitrate
            || self.max_bitrate > 50_000_000
        {
            return bad("require 1000 <= min <= target <= max <= 50000000 bps");
        }
        if !(1..=256).contains(&self.slices)
            || !(1..=7).contains(&self.target_usage)
            || self.gop_frames.is_some_and(|x| !(1..=65535).contains(&x))
            || self
                .intra_refresh
                .is_some_and(|x| !(2..=65535).contains(&x))
        {
            return bad("invalid slices, target usage, GOP or refresh cycle");
        }
        if !self.low_latency {
            return bad("only low_latency=true (no reorder/lookahead) is implemented");
        }
        if !(1..=4).contains(&self.software_threads)
            || !(1..=128).contains(&self.software_slices.unwrap_or(self.slices))
        {
            return bad("software threads must be 1..4 and slices 1..128");
        }
        match self.vbv {
            Vbv::Frames(n) if !(1..=120).contains(&n) => return bad("VBV frames must be 1..120"),
            Vbv::Milliseconds(n) if !(1..=1000).contains(&n) => {
                return bad("VBV milliseconds must be 1..1000");
            }
            _ => (),
        }
        if let FrameCap::TargetFactor(f) = self.frame_cap
            && (!f.is_finite() || !(0.1..=100.0).contains(&f))
        {
            return bad("frame factor must be finite and 0.1..100");
        }
        if self.vbv_kb() > 65535 || self.frame_cap_bytes() > 16 * 1024 * 1024 {
            return bad("VBV exceeds 65535 KB or frame cap exceeds 16 MiB");
        }
        self.avc_level()?;
        Ok(())
    }

    /// oneVPL units are decimal KB, rounded up, with kbps rounded up first.
    pub fn vbv_kb(&self) -> u32 {
        let kbps = self.max_bitrate.div_ceil(1000) as u64;
        match self.vbv {
            Vbv::Frames(n) => (kbps * n as u64).div_ceil(8 * self.fps.max(1) as u64) as u32,
            Vbv::Milliseconds(n) => (kbps * n as u64).div_ceil(8000) as u32,
        }
    }

    /// Encoder hints, never a hard bound on emitted access units. CBR disables the VPL cap.
    pub fn frame_cap_bytes(&self) -> u32 {
        if self.rate_control == RateControl::Cbr {
            return 0;
        }
        let fps = self.fps.max(1) as f64;
        match self.frame_cap {
            FrameCap::MaxRateFrame => {
                (self.max_bitrate.div_ceil(1000) as f64 * 1000.0 / (8.0 * fps)) as u32
            }
            FrameCap::TargetFactor(f) => {
                (f * self.target_bitrate.div_ceil(1000) as f64 * 1000.0 / (8.0 * fps)) as u32
            }
            FrameCap::Bytes(n) => n,
            FrameCap::Disabled => 0,
        }
    }

    pub fn avc_level(&self) -> Result<u32> {
        let w = self.width.div_ceil(16) as u64;
        let h = self.height.div_ceil(16) as u64;
        let kbps = self.max_bitrate.div_ceil(1000) as u64;
        let minimum = [
            (31, 3600, 108000, 14000),
            (32, 5120, 216000, 20000),
            (40, 8192, 245760, 20000),
            (42, 8704, 522240, 50000),
        ]
        .into_iter()
        .find(|&(_, fs, mbps, rate)| {
            w * h <= fs
                && w * h * self.fps as u64 <= mbps
                && kbps <= rate
                && w * w <= fs * 8
                && h * h <= fs * 8
        })
        .map(|x| x.0)
        .ok_or_else(|| Error::Invalid("configuration exceeds AVC level 4.2".into()))?;
        match self.level {
            Some(level) if ![31, 32, 40, 42].contains(&level) || level < minimum => {
                Err(Error::Invalid(
                    "requested AVC level is unsupported or understates stream limits".into(),
                ))
            }
            Some(level) => Ok(level),
            None => Ok(minimum),
        }
    }
}
