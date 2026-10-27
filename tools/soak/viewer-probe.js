// Installed by CDP after load; the existing viewer owns marker decoding/clock sync.
(() => {
  if (window.soakProbe) return;
  const video = document.getElementById('video');
  if (!video) return;
  const p = window.soakProbe = { frames: 0, freezes: 0, maxGapMs: 0, last: null, frozen: false };
  setInterval(() => {
    if (p.last !== null && performance.now() - p.last > 500 && !p.frozen) {
      p.freezes++; p.frozen = true;
    }
  }, 100);
  const frame = now => {
    if (p.last !== null) p.maxGapMs = Math.max(p.maxGapMs, now - p.last);
    p.last = now; p.frozen = false; p.frames++;
    video.requestVideoFrameCallback(frame);
  };
  video.requestVideoFrameCallback(frame);
  window.soakSnapshot = async drain => {
    let inbound = null;
    if (window.rk?.pc) (await window.rk.pc.getStats()).forEach(s => {
      if (s.type === 'inbound-rtp' && (s.kind || s.mediaType) === 'video') inbound = s;
    });
    const m = window.rk?.measure;
    const lat = m ? RKMarker.percentiles(m.latDecoded, [50, 99]) : null;
    const rect = video.getBoundingClientRect();
    const out = {
      at: Date.now(), frames: p.frames, freezesOver500ms: p.freezes,
      maxGapMs: Math.max(p.maxGapMs, p.last === null ? 0 : performance.now() - p.last),
      lastFrameAgeMs: p.last === null ? null : performance.now() - p.last,
      width: video.videoWidth, height: video.videoHeight,
      expectedWidth: Math.round(rect.width * devicePixelRatio / 2) * 2,
      expectedHeight: Math.round(rect.height * devicePixelRatio / 2) * 2,
      inbound, decodedLatencyMs: lat,
      markerFailures: m?.failed ?? null, markerFrames: m?.frames ?? null,
      statusText: document.getElementById('status')?.textContent ?? null
    };
    if (drain && m) {
      // Drain per-window marker storage so a 90 minute run stays bounded. Keep
      // clock, marker alignment and frame continuity intact.
      for (const key of ['lat', 'latDecoded', 'rx', 'i2p', 'i2decoded', 'i2display', 'rows', 'colorRows', 'displayGaps']) m[key].length = 0;
      m.frames = m.failed = m.decoded = 0; m.stages = {};
    }
    return out;
  };
})();
