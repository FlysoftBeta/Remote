//! Browser link (D3): three inherited anonymous pipes, one writer thread each.
//! cmd = endpoint->browser (RPC, input, frame release); evt = browser->endpoint (replies, lifecycle, window/popup/IME);
//! frm = browser->endpoint (frame descriptors only). Messages come from the schema in protocol/ (named messages + version).
#![allow(dead_code)]
pub enum Pipe { Cmd, Evt, Frm }
