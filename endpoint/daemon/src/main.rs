//! remotekit browser-endpoint: one process per host, manages browser instances (01 section 2).
//! M0 skeleton: only the control channel is live (hello/ping/shutdown). Module stubs mark where M1 work lands.
mod browser_link;
mod control;
mod encoder;
mod frame_import;
mod instance;
mod pacing;
mod session;

use anyhow::Result;
use serde_json::{Value, json};

fn handle(req: &Value) -> (Value, bool) {
    let id = req.get("id").cloned().unwrap_or(Value::Null);
    let method = req.get("method").and_then(Value::as_str).unwrap_or("");
    let ok = |result: Value| json!({"v": control::VERSION, "id": id, "result": result});
    match method {
        "hello" => (
            ok(json!({"name": "remotekit-endpoint", "version": env!("CARGO_PKG_VERSION"),
                      "encoders": encoder::Backend::all().iter().map(|b| b.name()).collect::<Vec<_>>()})),
            false,
        ),
        "ping" => (ok(json!({})), false),
        "shutdown" => (ok(json!({})), true),
        _ => (
            json!({"v": control::VERSION, "id": id,
                   "error": {"code": "unknown_method", "message": format!("unknown method '{method}'")}}),
            false,
        ),
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    let mut stdin = tokio::io::stdin();
    let mut stdout = tokio::io::stdout();
    while let Some(req) = control::read_record(&mut stdin).await? {
        let (resp, stop) = handle(&req);
        control::write_record(&mut stdout, &resp).await?;
        if stop {
            break;
        }
    }
    Ok(())
}
