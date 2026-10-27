use crate::Result;
use serde_json::Value;
use std::{
    io::{self, Read, Write},
    sync::{
        Arc,
        atomic::{AtomicUsize, Ordering},
        mpsc::{self, Receiver, SyncSender},
    },
    time::Duration,
};
const MAX: usize = 4 * 1024 * 1024;
const QUEUE: usize = 8 * 1024 * 1024;
pub struct Transport {
    pub input: Receiver<Result<Value>>,
    output: SyncSender<Vec<u8>>,
    bytes: Arc<AtomicUsize>,
}
fn record(r: &mut impl Read) -> Result<Option<Value>> {
    let mut prefix = [0; 4];
    if r.read(&mut prefix[..1])? == 0 {
        return Ok(None);
    }
    r.read_exact(&mut prefix[1..])?;
    let n = u32::from_le_bytes(prefix) as usize;
    if n == 0 || n > MAX {
        return Err("invalid stdio record length".into());
    }
    let mut bytes = vec![0; n];
    r.read_exact(&mut bytes)?;
    let v: Value = serde_json::from_slice(&bytes)?;
    if !v.is_object() {
        return Err("expected signaling object".into());
    }
    Ok(Some(v))
}
impl Transport {
    pub fn start(ws: Option<&str>) -> Result<Self> {
        let (tx, input) = mpsc::sync_channel(64);
        let (output, rx) = mpsc::sync_channel::<Vec<u8>>(64);
        let bytes = Arc::new(AtomicUsize::new(0));
        let count = bytes.clone();
        if let Some(url) = ws {
            let (mut socket, _) = tungstenite::connect(url)?;
            match socket.get_mut() {
                tungstenite::stream::MaybeTlsStream::Plain(s) => {
                    s.set_read_timeout(Some(Duration::from_millis(2)))?
                }
                tungstenite::stream::MaybeTlsStream::Rustls(s) => {
                    s.sock.set_read_timeout(Some(Duration::from_millis(2)))?
                }
                _ => return Err("unsupported websocket transport".into()),
            }
            std::thread::spawn(move || {
                loop {
                    for b in rx.try_iter() {
                        let n = b.len();
                        if socket
                            .send(tungstenite::Message::Text(
                                String::from_utf8(b).unwrap().into(),
                            ))
                            .is_err()
                        {
                            return;
                        }
                        count.fetch_sub(n, Ordering::Relaxed);
                    }
                    match socket.read() {
                        Ok(tungstenite::Message::Text(t)) => {
                            let v = serde_json::from_str::<Value>(&t)
                                .map_err(Into::into)
                                .and_then(|v| {
                                    if v.is_object() {
                                        Ok(v)
                                    } else {
                                        Err("expected signaling object".into())
                                    }
                                });
                            if tx.send(v).is_err() {
                                return;
                            }
                        }
                        Ok(tungstenite::Message::Close(_)) => return,
                        Ok(_) => (),
                        Err(tungstenite::Error::Io(e))
                            if matches!(
                                e.kind(),
                                io::ErrorKind::WouldBlock | io::ErrorKind::TimedOut
                            ) => {}
                        Err(e) => {
                            let _ = tx.send(Err(e.into()));
                            return;
                        }
                    }
                }
            });
        } else {
            std::thread::spawn(move || {
                let mut r = io::stdin().lock();
                loop {
                    match record(&mut r) {
                        Ok(Some(v)) => {
                            if tx.send(Ok(v)).is_err() {
                                return;
                            }
                        }
                        Ok(None) => return,
                        Err(e) => {
                            let _ = tx.send(Err(e));
                            return;
                        }
                    }
                }
            });
            std::thread::spawn(move || {
                let mut w = io::stdout().lock();
                for b in rx {
                    if w.write_all(&(b.len() as u32).to_le_bytes())
                        .and_then(|_| w.write_all(&b))
                        .and_then(|_| w.flush())
                        .is_err()
                    {
                        std::process::exit(1);
                    }
                    count.fetch_sub(b.len(), Ordering::Relaxed);
                }
            });
        }
        Ok(Self {
            input,
            output,
            bytes,
        })
    }
    pub fn send(&self, v: Value) -> Result<()> {
        let b = serde_json::to_vec(&v)?;
        let n = b.len();
        if n > MAX || self.bytes.fetch_add(n, Ordering::Relaxed) + n > QUEUE {
            return Err("signaling queue overflow".into());
        }
        if self.output.try_send(b).is_err() {
            self.bytes.fetch_sub(n, Ordering::Relaxed);
            return Err("signaling writer stopped or queue full".into());
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn framing_rejects_truncation_and_nonobjects() {
        for b in [
            &[1, 0, 0][..],
            &[3, 0, 0, 0, b'{'][..],
            &[2, 0, 0, 0, b'[', b']'][..],
            &[0, 0, 0, 0][..],
            &[255, 255, 255, 255][..],
        ] {
            assert!(record(&mut &*b).is_err());
        }
        assert!(record(&mut &[][..]).unwrap().is_none());
        assert_eq!(
            record(&mut &[2, 0, 0, 0, b'{', b'}'][..]).unwrap(),
            Some(serde_json::json!({}))
        );
    }
}
