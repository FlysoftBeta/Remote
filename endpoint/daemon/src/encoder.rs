//! H.264 encoders (D12, 03 section 3): oneVPL (legacy MSDK runtime on UHD 630) primary,
//! Media Foundation Intel MFT and OpenH264 as fallbacks. AsyncDepth=1, IPPP, no B-frames, on-demand IDR.
#[derive(Clone, Copy, Debug)]
pub enum Backend { Vpl, MediaFoundation, OpenH264 }

impl Backend {
    pub fn all() -> &'static [Backend] { &[Backend::Vpl, Backend::MediaFoundation, Backend::OpenH264] }
    pub fn name(self) -> &'static str {
        match self { Backend::Vpl => "onevpl", Backend::MediaFoundation => "mf", Backend::OpenH264 => "openh264" }
    }
}
