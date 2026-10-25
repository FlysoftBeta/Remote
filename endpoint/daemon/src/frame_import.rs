//! Frame import (D4/D5): GPU frames arrive as D3D11 NT handles (DuplicateHandle from the browser process,
//! keyed mutex key 0), CPU frames as read-only sections. Target: zero extra copy (03); fallback: one private copy.
#![allow(dead_code)]
pub enum FrameKind { GpuNtHandle, CpuSection }
