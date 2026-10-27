use crate::Result;
use re_mojo::{Client, Key, Pointer, Size, Wheel};
use serde_json::Value;

#[derive(Default)]
pub struct Input {
    mods: u8,
}
fn u16_at(b: &[u8], i: usize) -> u16 {
    u16::from_le_bytes([b[i], b[i + 1]])
}
impl Input {
    pub fn reset(&mut self, c: &mut Client) -> Result<()> {
        self.mods = 0;
        c.reset_input()?;
        Ok(())
    }
    pub fn binary(
        &mut self,
        c: &mut Client,
        window: u32,
        g: (i32, i32, f32),
        b: &[u8],
    ) -> Result<Option<u16>> {
        if b.len() < 8 || window == 0 || g.0 <= 0 || g.1 <= 0 || g.2 <= 0. {
            return Ok(None);
        }
        let now = crate::platform::now_us();
        match b[0] {
            1 if b.len() >= 20 && b[1] <= 4 && b[12] <= 2 && b[13] <= 15 => {
                self.mods = b[16];
                c.pointer(
                    window,
                    &Pointer {
                        action: b[1] as i32,
                        kind: b[12] as i32,
                        pid: b[13],
                        x: f32::from(u16_at(b, 8)) / 65535.,
                        y: f32::from(u16_at(b, 10)) / 65535.,
                        buttons: b[14],
                        button: b[15] as i8,
                        mods: self.mods,
                        pressure: f32::from(b[17]) / 255.,
                        radius_x: f32::from(b[18]) / g.2,
                        radius_y: f32::from(b[19]) / g.2,
                        timestamp_us: now,
                    },
                )?;
            }
            2 if b.len() >= 16 && b[1] <= 2 => {
                let scale = match b[1] {
                    0 => (1. / g.2, 1. / g.2),
                    1 => (40., 40.),
                    _ => (g.0 as f32 / g.2, g.1 as f32 / g.2),
                };
                c.wheel(
                    window,
                    &Wheel {
                        x: f32::from(u16_at(b, 8)) / 65535.,
                        y: f32::from(u16_at(b, 10)) / 65535.,
                        dx: (u16_at(b, 12) as i16) as f32 / 4. * scale.0,
                        dy: (u16_at(b, 14) as i16) as f32 / 4. * scale.1,
                        mods: self.mods,
                        timestamp_us: now,
                    },
                )?;
            }
            3 if b.len() >= 14
                && b[1] <= 1
                && b[12] <= 32
                && b[13] <= 32
                && b.len() >= 14 + b[12] as usize + b[13] as usize =>
            {
                let Ok(code) = std::str::from_utf8(&b[14..14 + b[12] as usize]) else {
                    return Ok(None);
                };
                let Ok(key) = std::str::from_utf8(
                    &b[14 + b[12] as usize..14 + b[12] as usize + b[13] as usize],
                ) else {
                    return Ok(None);
                };
                self.mods = b[8];
                c.key(
                    window,
                    &Key {
                        down: b[1] == 0,
                        mods: self.mods,
                        repeat: b[9] & 1 != 0,
                        key_code: u16_at(b, 10),
                        code,
                        key,
                        timestamp_us: now,
                    },
                )?;
                if std::env::var_os("RB_TRACE").is_some() && b[1] == 0 {
                    log::info!("PRTS TR K down t={now}");
                }
            }
            4 => self.reset(c)?,
            _ => return Ok(None),
        }
        Ok(Some(u16_at(b, 2)))
    }
    pub fn control(&mut self, c: &mut Client, window: u32, m: &Value) -> Result<()> {
        let context = m["context"]
            .as_u64()
            .and_then(|n| u32::try_from(n).ok())
            .unwrap_or(0);
        match m["t"].as_str().unwrap_or("") {
            "hello" => c.touch_capabilities(
                m["caps"]["touch"].as_i64().unwrap_or(0).clamp(0, 10) as u8,
                m["caps"]["touchOnly"].as_bool().unwrap_or(false),
            )?,
            "keyframe" if window != 0 => c.refresh(window)?,
            "resize" => {
                if let (Some(w), Some(h), Some(dpr)) =
                    (m["w"].as_f64(), m["h"].as_f64(), m["dpr"].as_f64())
                    && let Some((size, dsf)) = viewport(w, h, dpr)
                {
                    log::info!("PRTS viewport {}x{} @{}", size.width, size.height, dsf);
                    c.set_viewport(size, dsf)?;
                }
            }
            "focus" => {
                if let Some(f) = m["focused"].as_bool() {
                    if !f {
                        self.reset(c)?;
                    } else if window != 0 {
                        c.activate(window)?;
                    }
                    c.focus(f)?;
                }
            }
            "ime.commit" | "ime.compose" if window != 0 => {
                if let Some(t) = m["text"].as_str() {
                    let len = t.encode_utf16().count();
                    if len <= 4096 {
                        if m["t"] == "ime.commit" {
                            c.ime_commit(t, context)?;
                        } else if let Some(cursor) = m["cursor"].as_i64()
                            && (0..=len as i64).contains(&cursor)
                        {
                            c.ime_compose(t, cursor as i32, context)?;
                        }
                    }
                }
            }
            "ime.delete" if window != 0 => {
                if let (Some(a), Some(b)) = (m["before"].as_i64(), m["after"].as_i64())
                    && (0..=4096).contains(&a)
                    && (0..=4096).contains(&b)
                {
                    c.ime_delete(a as i32, b as i32, context)?;
                }
            }
            "ime.hidden" if window != 0 => c.ime_hidden(context)?,
            "ime.key" if window != 0 => {
                if let Some(key) = m["key"].as_str() {
                    let code = match key {
                        "Enter" => 13,
                        "Backspace" => 8,
                        "Tab" => 9,
                        "Delete" => 46,
                        "Escape" => 27,
                        _ => 0,
                    };
                    let mods = m["mods"].as_i64().unwrap_or(0);
                    if code != 0 && (0..=127).contains(&mods) {
                        for down in [true, false] {
                            c.key(
                                window,
                                &Key {
                                    down,
                                    mods: mods as u8,
                                    repeat: false,
                                    key_code: code,
                                    code: key,
                                    key,
                                    timestamp_us: crate::platform::now_us(),
                                },
                            )?;
                        }
                    }
                }
            }
            _ => (),
        }
        Ok(())
    }
}
pub fn viewport(w: f64, h: f64, dpr: f64) -> Option<(Size, f32)> {
    if !w.is_finite()
        || !h.is_finite()
        || !dpr.is_finite()
        || !(1. ..=16384.).contains(&w)
        || !(1. ..=16384.).contains(&h)
        || dpr <= 0.
    {
        return None;
    }
    let budget = 1920. * 1080.;
    let scale = 1f64
        .max(640. / w)
        .max(480. / h)
        .min((budget / (w * h)).sqrt());
    let (mut width, mut height) = (
        ((w * scale / 2.).ceil() * 2.) as i32,
        ((h * scale / 2.).ceil() * 2.) as i32,
    );
    while f64::from(width) * f64::from(height) > budget {
        if width >= height {
            width -= 2;
        } else {
            height -= 2;
        }
    }
    let requested = 1f64.max(dpr.min((budget / (f64::from(width) * f64::from(height))).sqrt()));
    let mut dsf = requested as f32;
    if f64::from(dsf) > requested {
        dsf = f32::from_bits(dsf.to_bits() - 1);
    }
    Some((Size { width, height }, dsf))
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn resize_budget_and_utf16_contract() {
        for (w, h, dpr) in [
            (1920., 1080., 2.),
            (1179., 2556., 3.),
            (320., 240., 1.),
            (16384., 1., 4.),
        ] {
            let (s, d) = viewport(w, h, dpr).unwrap();
            assert_eq!(s.width % 2, 0);
            assert_eq!(s.height % 2, 0);
            assert!((s.width as f64 * s.height as f64 * d as f64 * d as f64) <= 1920. * 1080. + 1.);
        }
        assert!(viewport(f64::NAN, 100., 1.).is_none());
        assert!(viewport(100., 100., 0.).is_none());
    }
}
