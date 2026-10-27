//! Low latency H.264 encoders for the Windows endpoint.
//!
//! Construct and call an encoder on one queue. NV12 hardware inputs on the same
//! D3D11 device are zero copy. The caller must keep producer buffers immutable
//! through their [`GpuFrame::lease`], which may outlive `encode` (VPL references).
//! NVENC is **untested on NVIDIA hardware**; initialization errors are recoverable.

pub mod d3d;
mod hardware;
mod input;
mod params;
mod selector;
mod software;

pub use input::*;
pub use params::*;
pub use selector::*;

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("invalid encoder input/parameters: {0}")]
    Invalid(String),
    #[error("encoder unavailable: {0}")]
    Unavailable(String),
    #[error("encoder failed: {0}")]
    Backend(String),
    #[error("D3D11: {0}")]
    Windows(#[from] windows::core::Error),
    #[error("raw frame pool is full; drop this raw frame and retry")]
    PoolBusy,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FrameType {
    Idr,
    Intra,
    Predicted,
}

#[derive(Clone, Debug)]
pub struct AccessUnit {
    pub annex_b: Vec<u8>,
    pub timestamp_us: u64,
    pub frame_type: FrameType,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ReconfigureOutcome {
    Seamless,
    /// A sequence boundary (resize/configuration recreation, or a VPL FPS reset).
    /// Next output is IDR. Legacy Intel runtimes insert IDR on FPS changes even
    /// with StartNewSequence=OFF, so those changes are reported conservatively.
    NewSequence,
    /// Backend rejected the seamless request; existing parameters remain active.
    Unchanged(String),
}

pub trait Encoder {
    fn backend(&self) -> Backend;
    fn params(&self) -> &EncoderParams;
    /// None represents a software rate-control skip, never a discarded encoded reference.
    fn encode(&mut self, frame: InputFrame<'_>) -> Result<Option<AccessUnit>>;
    fn reconfigure(&mut self, params: EncoderParams) -> Result<ReconfigureOutcome>;
    /// Coalesced until an IDR has actually been emitted.
    fn force_idr(&mut self);
    fn diagnostics(&self) -> String;
}

pub(crate) fn validate_access_unit(data: &[u8], params: &EncoderParams) -> Result<FrameType> {
    let mut idr = false;
    let mut sps = false;
    let mut pps = false;
    let mut slice = false;
    let mut start = false;
    for (i, bytes) in data.windows(4).enumerate() {
        if bytes[..3] != [0, 0, 1] {
            continue;
        }
        start = true;
        if bytes[3] & 0x80 != 0 {
            return Err(Error::Backend("forbidden_zero_bit set".into()));
        }
        match bytes[3] & 31 {
            1 => slice = true,
            5 => {
                idr = true;
                slice = true;
            }
            7 => {
                let fields = data
                    .get(i + 4..i + 7)
                    .ok_or_else(|| Error::Backend("truncated SPS".into()))?;
                let valid_profile = match params.profile {
                    Profile::ConstrainedBaseline => fields[0] == 66 && fields[1] & 0x40 != 0,
                    Profile::Main => fields[0] == 77,
                    Profile::High => fields[0] == 100,
                };
                if !valid_profile || fields[2] as u32 != params.avc_level()? {
                    return Err(Error::Backend(format!(
                        "SPS profile/level mismatch: {:02x?}",
                        fields
                    )));
                }
                sps = true;
            }
            8 => pps = true,
            _ => (),
        }
    }
    if !start || !slice || (idr && (!sps || !pps)) {
        return Err(Error::Backend(
            "invalid Annex-B AU or IDR missing SPS/PPS".into(),
        ));
    }
    Ok(if idr {
        FrameType::Idr
    } else {
        FrameType::Predicted
    })
}
