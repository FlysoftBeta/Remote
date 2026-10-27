use crate::Result;
use re_encode::{EncoderChoice, EncoderParams, FrameCap, RateControl, Vbv};
use std::{collections::BTreeMap, path::PathBuf};

#[derive(Clone, Debug)]
pub struct Config {
    pub dll: PathBuf,
    pub chrome: Vec<String>,
    pub instance: String,
    pub websocket: Option<String>,
    pub encoder: EncoderChoice,
    pub params: EncoderParams,
    pub rtc: re_rtc::Config,
    pub trace: bool,
}

impl Config {
    pub fn parse(args: impl Iterator<Item = String>) -> Result<Self> {
        let mut switches = BTreeMap::new();
        for arg in args {
            let arg = arg.strip_prefix("--").ok_or("expected --switch[=value]")?;
            let (k, v) = arg.split_once('=').unwrap_or((arg, ""));
            switches.insert(k.to_owned(), v.to_owned());
        }
        let mut take =
            |name: &str, default: &str| switches.remove(name).unwrap_or_else(|| default.into());
        let instance = take("instance", "default");
        let dll = PathBuf::from(take("native-dll", ""));
        let chrome_path = take("chrome", "");
        let profile = take("user-data-dir", "");
        let url = take("url", "about:blank");
        let app = take("app", "absent") != "absent";
        let websocket = take("signal", "");
        let encoder = take("encoder", "vpl").parse()?;
        let mut params = EncoderParams {
            fps: take("fps", "60").parse()?,
            min_bitrate: take("min-kbps", "2000")
                .parse::<u32>()?
                .checked_mul(1000)
                .ok_or("bitrate overflow")?,
            target_bitrate: take("start-kbps", "8000")
                .parse::<u32>()?
                .checked_mul(1000)
                .ok_or("bitrate overflow")?,
            max_bitrate: take("max-kbps", "8000")
                .parse::<u32>()?
                .checked_mul(1000)
                .ok_or("bitrate overflow")?,
            rate_control: match take("vpl-rc", "vbr").as_str() {
                "vbr" => RateControl::Vbr,
                "cbr" => RateControl::Cbr,
                _ => return Err("--vpl-rc must be vbr|cbr".into()),
            },
            ..EncoderParams::default()
        };
        let factor = take("vpl-max-frame-factor", "");
        if !factor.is_empty() {
            params.frame_cap = FrameCap::TargetFactor(factor.parse()?);
        }
        let frames = take("vpl-vbv-frames", "");
        let ms = take("vpl-vbv-ms", "");
        if !frames.is_empty() && !ms.is_empty() {
            return Err("choose VBV frames or ms".into());
        }
        params.vbv = if !ms.is_empty() {
            Vbv::Milliseconds(ms.parse()?)
        } else {
            Vbv::Frames(if frames.is_empty() {
                1
            } else {
                frames.parse()?
            })
        };
        params.target_usage = take("vpl-tu", "7").parse()?;
        params.low_power = match take("vpl-lowpower", "on").as_str() {
            "on" => true,
            "off" => false,
            _ => return Err("--vpl-lowpower must be on|off".into()),
        };
        let refresh: u32 = take("vpl-intra-refresh", "0").parse()?;
        params.intra_refresh = (refresh != 0).then_some(refresh);
        params.slices = take("vpl-slices", "1").parse()?;
        if take("vpl-profile", "cb") != "cb" {
            return Err(
                "only constrained baseline is negotiated; --vpl-profile=cb required".into(),
            );
        }
        let old_delay = take("latency-no-playout-delay", "absent") == "absent";
        let delay = take("playout-delay", "");
        let mut rtc = re_rtc::Config {
            fps: params.fps as i32,
            min_kbps: (params.min_bitrate / 1000) as i32,
            start_kbps: (params.target_bitrate / 1000) as i32,
            max_kbps: (params.max_bitrate / 1000) as i32,
            ..Default::default()
        };
        // M4 tuning is validated here, once, before opening any process or pipe.
        rtc.playout_metadata = old_delay;
        if delay == "none" {
            rtc.playout_delay = false;
            rtc.playout_metadata = false;
        } else if !delay.is_empty() {
            let (a, b) = delay
                .split_once(',')
                .ok_or("--playout-delay must be none|min,max")?;
            rtc.playout_min_ms = a.parse()?;
            rtc.playout_max_ms = b.parse()?;
            if rtc.playout_min_ms < 0
                || rtc.playout_max_ms < rtc.playout_min_ms
                || rtc.playout_max_ms > 40950
                || rtc.playout_min_ms % 10 != 0
                || rtc.playout_max_ms % 10 != 0
            {
                return Err("invalid playout-delay RTP units".into());
            }
            rtc.playout_metadata = true;
        }
        rtc.fec = match take("fec", "off").as_str() {
            "off" => 0,
            "ulpfec" => 1,
            "flexfec" => 2,
            _ => return Err("--fec must be off|ulpfec|flexfec".into()),
        };
        let trials = take("webrtc-field-trials", "default");
        rtc.field_trials = match trials.as_str() {
            "default" => None,
            "none" => Some(String::new()),
            _ => Some(trials),
        };
        let turn = take("turn", "");
        if !turn.is_empty() {
            let parts: Vec<_> = turn.split(',').collect();
            if parts.len() != 3 {
                return Err("--turn requires url,username,credential".into());
            }
            rtc.relay_only = true;
            rtc.ice_servers_json =
                serde_json::json!([{"urls":parts[0],"username":parts[1],"credential":parts[2]}])
                    .to_string();
        }
        take("no-hires-timer", ""); // Native runtime reads this diagnostic from process argv.
        take("enable-features", "");
        take("disable-features", "");
        if chrome_path.is_empty() || profile.is_empty() {
            return Err("require --chrome and a dedicated --user-data-dir".into());
        }
        let mut chrome = vec![
            chrome_path,
            "--screen-info={1920x1080}".into(),
            "--no-first-run".into(),
            "--no-default-browser-check".into(),
            "--disable-legacy-window".into(),
            format!("--user-data-dir={profile}"),
            "--window-size=1920,1080".into(),
        ];
        for (k, v) in switches {
            if let Some(k) = k.strip_prefix("cr-") {
                chrome.push(if v.is_empty() {
                    format!("--{k}")
                } else {
                    format!("--{k}={v}")
                });
            } else {
                return Err(format!("unknown switch --{k}").into());
            }
        }
        chrome.push(if app { format!("--app={url}") } else { url });
        params.validate()?;
        let dll = if dll.as_os_str().is_empty() {
            std::env::current_exe()?.with_file_name("re_native.dll")
        } else {
            dll
        };
        Ok(Self {
            dll,
            chrome,
            instance,
            websocket: (!websocket.is_empty()).then_some(websocket),
            encoder,
            params,
            rtc,
            trace: std::env::var_os("RB_TRACE").is_some(),
        })
    }
    pub fn log(&self) {
        let p = &self.params;
        log::info!("PRTS endpoint implementation=re");
        log::info!("PRTS encoder requested={:?}", self.encoder);
        log::info!(
            "PRTS experiment options rc={} cap_basis={} cap_policy=init_only vbv_frames={} vbv_ms={} tu={} lowpower={} intra_refresh={} slices={} profile=cb min_kbps={} start_kbps={} max_kbps={} fps={} playout_extension={} playout_metadata={} playout_min_ms={} playout_max_ms={} fec={}",
            if p.rate_control == RateControl::Cbr {
                "cbr"
            } else {
                "vbr"
            },
            if matches!(p.frame_cap, FrameCap::TargetFactor(_)) {
                "target"
            } else {
                "max"
            },
            if let Vbv::Frames(n) = p.vbv { n } else { 0 },
            if let Vbv::Milliseconds(n) = p.vbv {
                n
            } else {
                0
            },
            p.target_usage,
            if p.low_power { "on" } else { "off" },
            p.intra_refresh.unwrap_or(0),
            p.slices,
            p.min_bitrate / 1000,
            p.target_bitrate / 1000,
            p.max_bitrate / 1000,
            p.fps,
            self.rtc.playout_delay,
            self.rtc.playout_metadata,
            self.rtc.playout_min_ms,
            self.rtc.playout_max_ms,
            ["off", "ulpfec", "flexfec"][self.rtc.fec as usize]
        );
    }
}
