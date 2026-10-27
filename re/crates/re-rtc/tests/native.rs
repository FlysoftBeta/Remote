//! Explicit native ABI/lifetime checks; no build of Chromium is triggered.
//! RE_NATIVE_DLL=... cargo test -p re-rtc --test native --release -- --ignored
use re_rtc::{Config, EncodedFrame, Event, FrameTiming, Session};
use std::time::{Duration, Instant};

#[test]
#[ignore = "requires a configured pinned re_native.dll"]
fn native_sessions_negotiation_teardown_and_bounded_callbacks() {
    for i in 0..12 {
        let delay = i % 2 == 0;
        let mut s = Session::from_env(Config {
            playout_delay: delay,
            ..Config::default()
        })
        .unwrap();
        assert!(s.create_channel(0, "duplicate", true, None).is_err());
        s.create_channel(3, "extra", true, None).unwrap();
        assert!(s.send(1, b"not open yet", false).is_err());
        assert!(s.set_remote_description("answer", "invalid SDP").is_err());
        assert!(s.add_ice("0", 0, Some("invalid ICE")).is_err());
        assert!(
            s.submit(EncodedFrame {
                annex_b: &[],
                timestamp_us: 0,
                width: 1280,
                height: 720,
                keyframe: false,
                timing: FrameTiming::default()
            })
            .is_err()
        );
        s.offer(false).unwrap();
        s.request_stats().unwrap();
        let deadline = Instant::now() + Duration::from_secs(10);
        let mut got_offer = false;
        let mut got_stats = false;
        while Instant::now() < deadline && !(got_offer && got_stats) {
            match s.poll(Duration::from_millis(100)).unwrap() {
                Some(Event::Sdp { kind, sdp }) => {
                    assert_eq!(kind, "offer");
                    assert!(sdp.contains("packetization-mode=1"));
                    assert!(sdp.contains("profile-level-id=42e01f"));
                    assert!(sdp.contains("transport-cc"));
                    assert_eq!(sdp.contains("experiments/rtp-hdrext/playout-delay"), delay);
                    got_offer = true;
                }
                Some(Event::Stats(stats)) => {
                    let snapshot: serde_json::Value = serde_json::from_str(&stats).unwrap();
                    assert!(snapshot["rtc"].is_array());
                    assert!(snapshot["native"]["submitted"].is_u64());
                    assert!(snapshot["native"]["handed"].is_u64());
                    assert!(snapshot["native"]["suppressed"].is_u64());
                    got_stats = true;
                }
                Some(Event::Error(e)) => panic!("native error: {e}"),
                _ => (),
            }
        }
        assert!(
            got_offer && got_stats,
            "native callback timeout at iteration {i}"
        );
        // Pending ICE and WebRTC tasks must quiesce before callback storage dies.
        drop(s);
    }
    let mut s = Session::from_env(Config::default()).unwrap();
    for _ in 0..2050 {
        s.request_keyframe().unwrap();
    }
    assert!(
        s.poll(Duration::ZERO)
            .unwrap_err()
            .0
            .contains("queue overflow")
    );
    drop(s);
    let mut invalid = Config::default();
    invalid.min_kbps = invalid.max_kbps + 1;
    assert!(Session::from_env(invalid).is_err());
    let invalid_servers = Session::from_env(Config {
        ice_servers_json: "[{}]".into(),
        ..Config::default()
    });
    assert!(invalid_servers.err().unwrap().0.contains("no urls"));
    assert!(
        Session::from_env(Config {
            field_trials: Some("missing trailing slash".into()),
            ..Config::default()
        })
        .is_err()
    );
}
