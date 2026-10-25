//! Pacing controller (D6, 03): phase 1 = vsync + capture throttling; phase 2 = external BeginFrame driven by the endpoint.
#![allow(dead_code)]
pub enum Mode { CaptureThrottle, ExternalBeginFrame }
