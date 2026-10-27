use crate::{d3d::D3dDevice, hardware::Hardware, software::Software, *};
use std::str::FromStr;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Backend {
    Vpl,
    Nvenc,
    OpenH264,
}

impl std::fmt::Display for Backend {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            Self::Vpl => "vpl",
            Self::Nvenc => "nvenc",
            Self::OpenH264 => "openh264",
        })
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum EncoderChoice {
    #[default]
    Auto,
    Vpl,
    Nvenc,
    OpenH264,
}
impl FromStr for EncoderChoice {
    type Err = Error;
    fn from_str(s: &str) -> Result<Self> {
        match s {
            "auto" => Ok(Self::Auto),
            "vpl" => Ok(Self::Vpl),
            "nvenc" => Ok(Self::Nvenc),
            "openh264" => Ok(Self::OpenH264),
            _ => Err(Error::Invalid(
                "--encoder must be auto|vpl|nvenc|openh264".into(),
            )),
        }
    }
}

/// Auto uses the supplied adapter's vendor, then OpenH264. Explicit choices are
/// strict and return their error. Auto also falls back on a hardware encode failure,
/// re-encodes the same raw frame as IDR, and retains every successful reference AU.
pub fn create_encoder(
    choice: EncoderChoice,
    params: EncoderParams,
    device: Option<D3dDevice>,
) -> Result<Box<dyn Encoder>> {
    params.validate()?;
    match choice {
        EncoderChoice::OpenH264 => Ok(Box::new(Software::new(params, device)?)),
        EncoderChoice::Vpl | EncoderChoice::Nvenc => {
            let vendor = if choice == EncoderChoice::Vpl {
                0x8086
            } else {
                0x10de
            };
            let device = match device {
                Some(d) => d,
                None => D3dDevice::create(Some(vendor))?,
            };
            let backend = if choice == EncoderChoice::Vpl {
                Backend::Vpl
            } else {
                Backend::Nvenc
            };
            Ok(Box::new(Hardware::new(backend, params, device)?))
        }
        EncoderChoice::Auto => {
            let mut failures = Vec::new();
            let device = device.or_else(|| {
                D3dDevice::create(None)
                    .map_err(|e| failures.push(e.to_string()))
                    .ok()
            });
            let preferred = device.as_ref().and_then(|d| match d.adapter().vendor_id {
                0x8086 => Some(Backend::Vpl),
                0x10de => Some(Backend::Nvenc),
                _ => None,
            });
            if let Some(backend) = preferred {
                match Hardware::new(backend, params.clone(), device.as_ref().unwrap().clone()) {
                    Ok(hardware) => {
                        return Ok(Box::new(AutoEncoder {
                            inner: Box::new(hardware),
                            device,
                            failures,
                        }));
                    }
                    Err(error) => failures.push(error.to_string()),
                }
            }
            let software = Software::new(params, device.clone())?;
            Ok(Box::new(AutoEncoder {
                inner: Box::new(software),
                device,
                failures,
            }))
        }
    }
}

struct AutoEncoder {
    inner: Box<dyn Encoder>,
    device: Option<D3dDevice>,
    failures: Vec<String>,
}
impl Encoder for AutoEncoder {
    fn backend(&self) -> Backend {
        self.inner.backend()
    }
    fn params(&self) -> &EncoderParams {
        self.inner.params()
    }
    fn encode(&mut self, frame: InputFrame<'_>) -> Result<Option<AccessUnit>> {
        match self.inner.encode(frame) {
            Err(e @ (Error::Backend(_) | Error::Unavailable(_)))
                if self.inner.backend() != Backend::OpenH264 =>
            {
                self.failures.push(e.to_string());
                let mut software = Software::new(self.inner.params().clone(), self.device.clone())?;
                software.force_idr();
                self.inner = Box::new(software);
                self.inner.encode(frame)
            }
            result => result,
        }
    }
    fn reconfigure(&mut self, params: EncoderParams) -> Result<ReconfigureOutcome> {
        self.inner.reconfigure(params)
    }
    fn force_idr(&mut self) {
        self.inner.force_idr();
    }
    fn diagnostics(&self) -> String {
        format!(
            "{}; auto fallback reasons: {:?}",
            self.inner.diagnostics(),
            self.failures
        )
    }
}
