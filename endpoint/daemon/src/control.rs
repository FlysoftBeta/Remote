//! Control channel to the control-plane: framed JSON on stdin/stdout (01 section 2).
//! Record = u32 little-endian length + UTF-8 JSON object with `"v": 1` (same framing as control-plane/framing.js).
use anyhow::{Result, bail, ensure};
use serde_json::Value;
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};

pub const MAX_RECORD: usize = 4 * 1024 * 1024;
pub const VERSION: u64 = 1;

pub async fn read_record<R: AsyncRead + Unpin>(r: &mut R) -> Result<Option<Value>> {
    let mut len = [0u8; 4];
    match r.read_exact(&mut len).await {
        Ok(_) => {}
        Err(e) if e.kind() == std::io::ErrorKind::UnexpectedEof => return Ok(None),
        Err(e) => return Err(e.into()),
    }
    let n = u32::from_le_bytes(len) as usize;
    ensure!((1..=MAX_RECORD).contains(&n), "invalid record length {n}");
    let mut body = vec![0u8; n];
    r.read_exact(&mut body).await?;
    let v: Value = serde_json::from_slice(&body)?;
    if !v.is_object() || v.get("v").and_then(Value::as_u64) != Some(VERSION) {
        bail!("invalid protocol object/version");
    }
    Ok(Some(v))
}

pub async fn write_record<W: AsyncWrite + Unpin>(w: &mut W, v: &Value) -> Result<()> {
    let body = serde_json::to_vec(v)?;
    ensure!((1..=MAX_RECORD).contains(&body.len()), "record too large");
    w.write_all(&(body.len() as u32).to_le_bytes()).await?;
    w.write_all(&body).await?;
    w.flush().await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[tokio::test]
    async fn roundtrip() {
        let mut buf = Vec::new();
        let msg = serde_json::json!({"v": 1, "id": 7, "method": "hello"});
        write_record(&mut buf, &msg).await.unwrap();
        let got = read_record(&mut buf.as_slice()).await.unwrap().unwrap();
        assert_eq!(got, msg);
        assert!(read_record(&mut &[][..]).await.unwrap().is_none());
    }
}
