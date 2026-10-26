// remotekit viewer (D13, design/06-protocol.md). Must run on Chrome 89+: newer APIs only behind feature detection.
// URL params: instance, token, measure=1 (pixel-marker latency), report=<s> (POST summary to /api/report after s seconds),
//             my=<source px> (fixed marker row; default auto-detect),
//             delay=<ms>|off (receiver jitter-buffer hint, default 0), stats=0 (hide overlay), cp=<host:port>.
//             measure=<seconds> enables the optional field kit's tap-to-start mode.
(function () {
  'use strict';
  var q = new URLSearchParams(location.search);
  var field = window.RKField || null;
  var video = document.getElementById('video'), stage = document.getElementById('stage');
  var overlay = document.getElementById('overlay'), statusEl = document.getElementById('status');
  var touchOnly = (navigator.maxTouchPoints || 0) > 0 && matchMedia('(pointer: coarse)').matches &&
    !matchMedia('(any-pointer: fine)').matches && !matchMedia('(any-hover: hover)').matches;
  var isTouch = (navigator.maxTouchPoints || 0) > 0 && matchMedia('(pointer: coarse)').matches;
  var pc = null, ch = null, sid = null, ws = null, clock = null, measure = null, epHello = null, epStats = null, hintInfo = 'n/a';
  var session = null, generation = 0, retryTimer = null, retryDelay = 1000, terminal = false;
  var input = new RKInput(stage, video, sendInput);
  var ime = new RKIme(document.getElementById('ime'), document.getElementById('kbd'), input, sendCtl, isTouch);
  window.rk = { input: input, ime: ime, get pc() { return pc; }, get measure() { return measure; }, get clock() { return clock; } };

  function status(s) { statusEl.textContent = s; statusEl.style.display = s ? 'block' : 'none'; console.log('[viewer] ' + s); }
  function sendInput(buf, seq) {
    if (!input.enabled || !ch || ch.input.readyState !== 'open') return;
    ch.input.send(buf);
    if (measure) measure.noteInput(seq);
  }
  function sendCtl(m) { if (ch && ch.ctl.readyState === 'open') ch.ctl.send(JSON.stringify(m)); }
  input.backedUp = function () { return ch && ch.input.bufferedAmount > 4096; };

  // ---- signalling ----
  function relayUrl() { var r = q.get('relay'); return r ? (r.indexOf('?') < 0 ? r + '?transport=udp' : r) : null; }
  function sendSig(m) { if (ws && ws.readyState === 1) ws.send(JSON.stringify(m)); }
  function connect() {
    clearTimeout(retryTimer); retryTimer = null; terminal = false;
    statusEl.style.cursor = '';
    statusEl.style.pointerEvents = 'none';
    var host = q.get('cp') || location.host;
    ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + host + '/signal?role=viewer&instance=' +
      encodeURIComponent(q.get('instance') || 'default') + (q.get('token') ? '&token=' + encodeURIComponent(q.get('token')) : ''));
    var socket = ws;
    ws.onopen = function () {
      if (ws !== socket) return;
      sendSig({ t: 'hello', v: RK.V, role: 'viewer', ua: navigator.userAgent, relay: relayUrl() || undefined,
        caps: { touch: navigator.maxTouchPoints || 0, touchOnly: touchOnly, dpr: devicePixelRatio, osk: isTouch, codecs: h264Caps().map(function (c) { return c.sdpFmtpLine || ''; }) } });
      status('waiting for endpoint');
    };
    ws.onmessage = function (e) { if (ws === socket) { try { onSignal(JSON.parse(e.data)); } catch (err) { console.warn('signalling', err); } } };
    ws.onerror = function () { if (ws === socket) socket.close(); };
    ws.onclose = function (e) {
      if (ws !== socket) return;
      ws = null; closePc();
      terminal = e.code === 4000 || e.code === 4400 || e.code === 4401;
      if (terminal) {
        status((e.code === 4000 ? 'viewer replaced' : e.code === 4400 ? 'protocol version mismatch' : 'authentication failed') + '; click to reconnect');
        statusEl.style.cursor = 'pointer';
        statusEl.style.pointerEvents = 'auto';
      } else {
        status('signalling closed (' + e.code + '); reconnecting in ' + retryDelay / 1000 + ' s');
        retryTimer = setTimeout(connect, retryDelay); retryDelay = Math.min(10000, retryDelay * 2);
      }
    };
  }
  statusEl.addEventListener('click', function () { if (terminal) { retryDelay = 1000; connect(); } });
  function closePc() {
    if (field) field.disconnect();
    generation++;
    if (session) clearTimeout(session.failedTimer);
    session = null; sid = null;
    input.enabled = false;
    if (typeof input.clearSession === 'function') input.clearSession();
    if (typeof ime.clearSession === 'function') ime.clearSession();
    if (ch) { ch.input.close(); ch.ctl.close(); ch.probe.close(); }
    if (pc) { var old = pc; pc = null; old.close(); }
    if (clock) { clock.close(); clock = null; }
    if (measure) { measure.reset(); measure.clock = { offset: null }; measure.inputSent = {}; measure.lastInputSeq = measure.lastFrameId = -1; }
    ch = null; epHello = null; epStats = null; prev = null; statsSamples = []; window.rk.last = null; video.srcObject = null;
    stage.style.cursor = video.style.cursor = 'default';
  }
  function current(s, p) { return session === s && generation === s.generation && pc === p; }
  function h264Caps() {
    if (!window.RTCRtpReceiver || !RTCRtpReceiver.getCapabilities) return [];
    return RTCRtpReceiver.getCapabilities('video').codecs.filter(function (c) { return /h264/i.test(c.mimeType); });
  }
  function preferH264(tr) {
    if (!tr.setCodecPreferences || !RTCRtpReceiver.getCapabilities) return 'unsupported';
    var codecs = RTCRtpReceiver.getCapabilities('video').codecs;
    function rank(c) {
      if (!/h264/i.test(c.mimeType)) return /rtx|red|ulpfec|flexfec/i.test(c.mimeType) ? 5 : 8;
      var f = c.sdpFmtpLine || '';
      return (f.indexOf('packetization-mode=1') >= 0 ? 0 : 2) + (f.indexOf('42e01f') >= 0 ? 0 : 1);
    }
    try { tr.setCodecPreferences(codecs.slice().sort(function (a, b) { return rank(a) - rank(b); })); return 'ok'; }
    catch (e) { return 'failed: ' + e.message; }
  }
  // Receiver latency hints (06 section 5): jitterBufferTarget (ms, Chrome 124+) else playoutDelayHint (s, Chrome 79+).
  function applyHints(r) {
    var d = q.get('delay'); if (d === 'off') { hintInfo = 'off'; return; }
    var ms = d == null ? 0 : +d;
    if ('jitterBufferTarget' in r) { r.jitterBufferTarget = ms; hintInfo = 'jitterBufferTarget=' + ms + ' -> ' + r.jitterBufferTarget; }
    else if ('playoutDelayHint' in r) { r.playoutDelayHint = ms / 1000; hintInfo = 'playoutDelayHint=' + ms / 1000 + ' -> ' + r.playoutDelayHint; }
    else hintInfo = 'none supported';
  }

  function onSignal(m) {
    if (m.t === 'welcome') {
      if (m.sid !== sid) {
        closePc(); sid = m.sid;
        session = { sid: sid, generation: generation, iceServers: m.iceServers || [], candidates: [], remoteReady: false,
          negotiation: Promise.resolve(), ice: Promise.resolve(), restarted: false, failedTimer: null };
      }
      status('session ' + sid.slice(0, 8) + ': negotiating');
    } else if (m.t === 'error') status(m.code === 'no-endpoint' ? 'waiting for endpoint' : 'error: ' + m.code);
    else if (!session || m.sid !== sid) return;
    else if (m.t === 'offer') onOffer(m);
    else if (m.t === 'ice') { session.candidates.push(m.candidate); drainIce(session, pc); }
    else if (m.t === 'bye') { status('session ended: ' + m.reason); closePc(); }
  }
  function drainIce(s, p) {
    if (!p || !current(s, p) || !s.remoteReady) return s.ice;
    s.ice = s.ice.then(async function () {
      while (current(s, p) && s.remoteReady && s.candidates.length) {
        var c = s.candidates.shift();
        try { await p.addIceCandidate(c === null ? { candidate: '' } : c); }
        catch (e) { if (c !== null && current(s, p)) console.warn('ice', e); }
      }
    }).catch(function (e) { if (current(s, p)) console.warn('ice', e); });
    return s.ice;
  }
  function createPc(s) {
    var rtcConfig = { iceServers: s.iceServers, bundlePolicy: 'max-bundle' };
    if (relayUrl()) { // netem lab: relay-only through the shaping TURN server (tools/netem)
      rtcConfig.iceServers = [{ urls: relayUrl(), username: 'viewer', credential: 'netem' }];
      rtcConfig.iceTransportPolicy = 'relay';
    }
    pc = new RTCPeerConnection(rtcConfig);
    var p = pc;
    ch = RK.createChannels(pc);
    var channels = ch;
    ch.ctl.onopen = function () {
      if (!current(s, p)) return;
      sendCtl({ t: 'hello', v: RK.V, input: RK.INPUT_V, caps: { touch: navigator.maxTouchPoints || 0, touchOnly: touchOnly, osk: isTouch, dpr: devicePixelRatio },
        view: { w: innerWidth, h: innerHeight, dpr: devicePixelRatio } });
      sendViewport();
      sendCtl({ t: 'focus', focused: document.hasFocus() && !document.hidden });
    };
    ch.ctl.onmessage = function (e) { if (current(s, p)) { try { onCtl(JSON.parse(e.data)); } catch (err) { console.warn('ctl', err); } } };
    ch.probe.onopen = function () { if (!current(s, p)) return; clock = new RKClock(channels.probe); if (q.get('measure') === '1' && !measure) measure = new RKMeasure(video, clock); else if (measure) measure.clock = clock; };
    ch.probe.onmessage = function (e) { if (current(s, p) && clock) { try { clock.onMessage(JSON.parse(e.data)); } catch (err) { console.warn('probe', err); } } };
    ch.input.onopen = function () { if (current(s, p)) { input.enabled = !!epHello; ime.refresh(); } };
    ch.input.bufferedAmountLowThreshold = 1024;
    ch.input.onbufferedamountlow = function () { if (current(s, p) && input.enabled) input.flush(); };
    pc.ontrack = function (e) {
      if (!current(s, p)) return;
      applyHints(e.receiver);
      video.srcObject = e.streams && e.streams[0] ? e.streams[0] : new MediaStream([e.track]);
      var play = video.play(); if (play && play.catch) play.catch(function () { if (current(s, p)) status('tap to start video'); });
    };
    pc.onicecandidate = function (e) { if (current(s, p)) sendSig({ t: 'ice', sid: s.sid, candidate: e.candidate ? e.candidate.toJSON() : null }); };
    pc.onconnectionstatechange = pc.oniceconnectionstatechange = function () {
      if (!current(s, p)) return;
      if (p.connectionState === 'failed' || p.iceConnectionState === 'failed') {
        status(s.restarted ? 'connection failed; waiting for recovery' : 'connection failed; restarting ICE');
        if (!s.restarted) { s.restarted = true; sendSig({ t: 'restart', sid: s.sid }); }
        if (!s.failedTimer) s.failedTimer = setTimeout(function () {
          if (!current(s, p)) return;
          sendSig({ t: 'bye', sid: s.sid, reason: 'ice-failed' }); closePc(); status('session ended: ice-failed');
        }, 10000);
      } else if (p.connectionState === 'connected' || p.iceConnectionState === 'connected' || p.iceConnectionState === 'completed') {
        clearTimeout(s.failedTimer); s.failedTimer = null; retryDelay = 1000; status('');
      } else status('connection ' + (p.connectionState || p.iceConnectionState));
    };
  }
  function onOffer(m) {
    var s = session;
    if (!pc) createPc(s);
    var p = pc;
    s.negotiation = s.negotiation.then(async function () {
      if (!current(s, p)) return;
      s.remoteReady = false;
      await p.setRemoteDescription({ type: 'offer', sdp: m.sdp });
      if (!current(s, p)) return;
      s.remoteReady = true; await drainIce(s, p);
      if (!current(s, p)) return;
      p.getTransceivers().forEach(function (t) {
        if (t.receiver.track && t.receiver.track.kind === 'video') { t.direction = 'recvonly'; console.log('[viewer] setCodecPreferences: ' + preferH264(t)); applyHints(t.receiver); }
      });
      var a = await p.createAnswer();
      if (!current(s, p)) return;
      await p.setLocalDescription(a);
      if (current(s, p)) sendSig({ t: 'answer', sid: s.sid, sdp: p.localDescription.sdp });
    }).catch(function (e) { if (current(s, p)) status('negotiation failed: ' + e.message); });
  }
  function onCtl(m) {
    if (m.t === 'hello') {
      if (m.v !== 1 || m.input !== 1) { sendSig({ t: 'bye', sid: sid, reason: 'version' }); closePc(); status('session ended: protocol version mismatch'); return; }
      epHello = m; input.enabled = ch.input.readyState === 'open'; ime.refresh();
    }
    else if (!epHello) return;
    else if (ime.onCtl(m)) return;
    else if (m.t === 'stats') epStats = m;
    else if (m.t === 'cursor') {
      // Only named cursors or the browser's bounded inline PNG format; no URLs.
      var css = typeof m.css === 'string' && m.css.length <= 61000 &&
        (/^[a-z-]+$/.test(m.css) || /^url\(data:image\/png;base64,[A-Za-z0-9+/]+={0,2}\) (?:[0-9]|[1-9][0-9]|1[01][0-9]|12[0-7]) (?:[0-9]|[1-9][0-9]|1[01][0-9]|12[0-7]), auto$/.test(m.css)) ? m.css : 'default';
      stage.style.cursor = video.style.cursor = css;
    }
    else if (m.t === 'frame') epHello = Object.assign({}, epHello, { frame: { w: m.w, h: m.h }, dsf: m.dsf });
  }

  // ---- focus / visibility / resize (06 section 3.1) ----
  function focusChanged() {
    var f = document.hasFocus() && !document.hidden;
    if (!f && ch) input.reset();
    sendCtl({ t: 'focus', focused: f });
  }
  window.addEventListener('blur', focusChanged);
  window.addEventListener('focus', focusChanged);
  document.addEventListener('visibilitychange', focusChanged);
  function sendViewport() {
    var r = video.getBoundingClientRect();
    sendCtl({ t: 'resize', w: Math.round(r.width), h: Math.round(r.height), dpr: devicePixelRatio });
  }
  var rzTimer = null;
  function scheduleViewport() {
    clearTimeout(rzTimer);
    rzTimer = setTimeout(sendViewport, 200);
  }
  window.addEventListener('resize', scheduleViewport);
  // Moving between displays can change DPR without changing CSS dimensions.
  // addListener/removeListener also work on the Chrome 89 viewer baseline.
  var dprMedia = null;
  function watchDpr() {
    if (dprMedia) dprMedia.removeListener(dprChanged);
    dprMedia = matchMedia('(resolution: ' + devicePixelRatio + 'dppx)');
    dprMedia.addListener(dprChanged);
  }
  function dprChanged() { watchDpr(); scheduleViewport(); }
  watchDpr();
  stage.addEventListener('pointerdown', function () { if (video.paused) { var play = video.play(); if (play && play.catch) play.catch(function () {}); } });

  // ---- stats overlay (1 Hz) ----
  var prev = null, statsSamples = [], showStats = q.get('stats') !== '0';
  if (!showStats) overlay.style.display = 'none';
  window.addEventListener('keydown', function (e) { if (e.key === 'F2' && e.ctrlKey) { showStats = !showStats; overlay.style.display = showStats ? 'block' : 'none'; } });
  function fmt(v, d) { return v == null || isNaN(v) ? '-' : (+v).toFixed(d == null ? 1 : d); }
  setInterval(function () {
    if (!pc) { overlay.textContent = 'no session'; return; }
    var s = session, p = pc;
    p.getStats().then(function (r) {
      if (!current(s, p)) return;
      var inb = null, trk = null, pair = null, codec = {};
      r.forEach(function (s) {
        if (s.type === 'inbound-rtp' && (s.kind || s.mediaType) === 'video') inb = s;
        if (s.type === 'candidate-pair' && s.nominated && s.state === 'succeeded') pair = s;
        if (s.type === 'codec') codec[s.id] = s;
        if (s.type === 'track' && s.kind === 'video' && s.remoteSource) trk = s; // Chrome 89: jitter buffer stats live here
      });
      if (!inb) return;
      var jbSrc = inb.jitterBufferEmittedCount != null ? inb : trk || {};
      var now = performance.now(), v = { rtt: pair && pair.currentRoundTripTime != null ? pair.currentRoundTripTime * 1000 : null };
      if (prev) {
        var dt = (now - prev.now) / 1000, df = inb.framesDecoded - prev.fd, de = (jbSrc.jitterBufferEmittedCount || 0) - prev.je;
        v.fps = (inb.framesDecoded - prev.fd) / dt;
        v.kbps = (inb.bytesReceived - prev.b) * 8 / dt / 1000;
        v.jbMs = de > 0 ? ((jbSrc.jitterBufferDelay - prev.jb) / de) * 1000 : null;
        v.decMs = df > 0 && inb.totalDecodeTime != null ? ((inb.totalDecodeTime - prev.td) / df) * 1000 : null;
        v.processingMs = df > 0 && inb.totalProcessingDelay != null ? ((inb.totalProcessingDelay - prev.tp) / df) * 1000 : null;
        v.framesDropped = inb.framesDropped == null ? null : inb.framesDropped - prev.dropped;
        statsSamples.push({ at: now, jbMs: v.jbMs, decMs: v.decMs, processingMs: v.processingMs, framesDropped: v.framesDropped, fps: v.fps });
        if (statsSamples.length > 3600) statsSamples.shift();
        if (statsSamples.length % 5 === 0) {
          var rolling = {};
          ['jbMs', 'decMs', 'processingMs', 'framesDropped', 'fps'].forEach(function (key) {
            rolling[key] = RKMarker.percentiles(statsSamples.slice(-5).map(function (sample) { return sample[key]; }).filter(function (value) { return value != null; }), [50, 95]);
          });
          console.log('[viewer] latency5s interval-p50/p95 ' + JSON.stringify(rolling));
        }
      }
      prev = { now: now, fd: inb.framesDecoded, b: inb.bytesReceived, je: jbSrc.jitterBufferEmittedCount || 0, jb: jbSrc.jitterBufferDelay || 0, td: inb.totalDecodeTime || 0, tp: inb.totalProcessingDelay || 0, dropped: inb.framesDropped || 0 };
      var c = codec[inb.codecId] || {}, lat = measure ? measure.summary() : null;
      var lines = [
        'video  ' + (inb.frameWidth || video.videoWidth) + 'x' + (inb.frameHeight || video.videoHeight) + '  ' + (c.mimeType || '?') + ' ' + (inb.decoderImplementation || ''),
        'fps    ' + fmt(v.fps) + '   ' + fmt(v.kbps / 1000, 2) + ' Mbps',
        'jitbuf ' + fmt(v.jbMs) + ' ms   decode ' + fmt(v.decMs, 2) + ' ms   rtt ' + fmt(v.rtt) + ' ms',
        'lost   ' + (inb.packetsLost || 0) + '  nack ' + (inb.nackCount || 0) + '  pli ' + (inb.pliCount || 0) + '  freeze ' + (inb.freezeCount != null ? inb.freezeCount : '-') + '  drop ' + (inb.framesDropped || 0),
        'hint   ' + hintInfo
      ];
      if (clock) lines.push('clock  offset ' + fmt(clock.offset, 2) + ' ms  minRtt ' + fmt(clock.minRtt, 2) + ' ms');
      if (lat) lines.push('g2g    p50 ' + fmt(lat.lat.p50) + '  p80 ' + fmt(lat.lat.p80) + '  p99 ' + fmt(lat.lat.p99) + ' ms  n=' + lat.lat.n + (lat.precise ? '' : ' (rAF, imprecise)'),
        'i2p    p50 ' + fmt(lat.i2p.p50) + '  p80 ' + fmt(lat.i2p.p80) + '  p99 ' + fmt(lat.i2p.p99) + ' ms  n=' + lat.i2p.n + '  marker ok ' + lat.decoded + '/' + lat.frames + '  fid ' + measure.lastFrameId);
      if (epStats) lines.push('sender ' + fmt(epStats.fps) + ' fps ' + fmt(epStats.kbps / 1000, 2) + ' Mbps enc ' + fmt(epStats.encMs && epStats.encMs.p50, 2) + ' ms qp ' + fmt(epStats.qp) + ' ' + (epStats.backend || '') + (epStats.limit && epStats.limit !== 'none' ? ' limit:' + epStats.limit : ''));
      overlay.textContent = lines.join('\n');
      sendCtl({ t: 'vstats', fps: v.fps, kbps: v.kbps, jbMs: v.jbMs, decMs: v.decMs, rtt: v.rtt, lost: inb.packetsLost, nack: inb.nackCount, pli: inb.pliCount,
        freeze: inb.freezeCount, lat: lat ? { p50: lat.lat.p50, p80: lat.lat.p80, p99: lat.lat.p99 } : null });
      window.rk.last = { inbound: inb, derived: v, codec: c.mimeType, codecDetails: c,
        jitterBuffer: { statsType: jbSrc.type, delay: jbSrc.jitterBufferDelay, emitted: jbSrc.jitterBufferEmittedCount },
        hint: hintInfo, decoder: inb.decoderImplementation };
    }).catch(function (e) { if (current(s, p)) console.warn('stats', e); });
  }, 1000);

  // m3-trace: relay hrtime (== TimeTicks base) minus this page's performance.now(), best of 30 by round trip.
  function syncRelayClock() {
    var r = relayUrl(), m = r && /turn:([^:?]+):(\d+)/.exec(r);
    if (q.get('trace') !== '1' || !m) return Promise.resolve(null);
    var url = 'http://' + m[1] + ':' + (+m[2] + 1) + '/now', best = null, chain = Promise.resolve();
    for (var i = 0; i < 30; i++) chain = chain.then(function () { var t0 = performance.now(); return fetch(url, { cache: 'no-store' }).then(function (x) { return x.text(); }).then(function (txt) {
      var t1 = performance.now(), rtt = t1 - t0, off = parseFloat(txt) - (t0 + t1) / 2; if (!best || rtt < best.rtt) best = { rtt: rtt, off: off }; }); });
    return chain.then(function () { return best; }, function () { return best; });
  }
  // ---- automated report for tools/latency/run.mjs ----
  if (q.get('report')) {
    var secs = +q.get('report'), warm = +(q.get('warmup') || 3);
    var started = null;
    // Synthetic input for input-to-photon: a mouse move every ~97 ms (the mock echoes the applied seq in the marker).
    if (q.get('autoinput') === '1') setInterval(function () { if (input.enabled) input.synthMove(0.2 + 0.6 * Math.random(), 0.6 + 0.3 * Math.random()); }, 97);
    setInterval(function () {
      if ((!field || field.ready) && session && started !== session && measure && clock && clock.offset != null && video.videoWidth) {
        var s = session, p = pc;
        started = s;
        if (field) field.begin(warm);
        setTimeout(function () { if (current(s, p)) { measure.reset(); syncRelayClock().then(function (b) { window.__traceRelay = { start: b }; }); } }, warm * 1000);
        setTimeout(function () {
          if (!current(s, p) || (field && !field.running)) return;
          var snapshot = field ? field.snapshot(measure) : null;
          syncRelayClock().then(function (b) { window.__traceRelay = Object.assign(window.__traceRelay || {}, { end: b }); }).then(function () { return p.getStats(); }).then(function (rs) {
          var pipeline = { relay: relayUrl(), pairs: [], inbound: null, headlessUa: /HeadlessChrome/.test(navigator.userAgent) };
          var cands = {};
          rs.forEach(function (x) { if (x.type === 'local-candidate' || x.type === 'remote-candidate') cands[x.id] = x; });
          rs.forEach(function (x) {
            if (x.type === 'candidate-pair' && x.nominated && x.state === 'succeeded') {
              var l = cands[x.localCandidateId] || {}, r = cands[x.remoteCandidateId] || {};
              pipeline.pairs.push({ local: l.candidateType, localProtocol: l.protocol, relayProtocol: l.relayProtocol, localAddress: l.address, remote: r.candidateType, remoteAddress: r.address,
                currentRoundTripTime: x.currentRoundTripTime, bytesSent: x.bytesSent, bytesReceived: x.bytesReceived });
            }
            if (x.type === 'inbound-rtp' && (x.kind || x.mediaType) === 'video') pipeline.inbound = { decoderImplementation: x.decoderImplementation, powerEfficientDecoder: x.powerEfficientDecoder,
              framesPerSecond: x.framesPerSecond, frameWidth: x.frameWidth, frameHeight: x.frameHeight, framesDecoded: x.framesDecoded, framesDropped: x.framesDropped, packetsLost: x.packetsLost,
              nackCount: x.nackCount, pliCount: x.pliCount, firCount: x.firCount, retransmittedPacketsReceived: x.retransmittedPacketsReceived, jitter: x.jitter, jitterBufferTargetDelay: x.jitterBufferTargetDelay };
          });
          pipeline.display = 'requestVideoFrameCallback expectedDisplayTime estimate' + (pipeline.headlessUa ? ' (headless UA: no real vsync/scanout)' : '');
          if (!current(s, p) || (field && !field.running)) return;
          var body = { trace: snapshot ? snapshot.trace : measure.trace, traceRelay: window.__traceRelay || null, perfNowAtReport: performance.now(), pipeline: pipeline, kind: 'latency', ua: navigator.userAgent, hint: hintInfo, n: snapshot ? snapshot.summary.lat.n : measure.lat.length, summary: snapshot ? snapshot.summary : measure.summary(),
            durationSeconds: secs, query: location.search,
            statsSamples: statsSamples.slice(-secs),
            playoutDelayExtmap: p.localDescription.sdp.split('\n').filter(function (line) { return line.indexOf('a=extmap:') === 0 && line.indexOf('playout-delay') >= 0; }),
            stats: window.rk.last ? { codec: window.rk.last.codec, codecDetails: window.rk.last.codecDetails,
              decoder: window.rk.last.decoder, derived: window.rk.last.derived,
              jitterBufferStatsType: window.rk.last.jitterBuffer.statsType,
              jitterBufferDelay: window.rk.last.jitterBuffer.delay, emitted: window.rk.last.jitterBuffer.emitted,
              totalDecodeTime: window.rk.last.inbound.totalDecodeTime, totalProcessingDelay: window.rk.last.inbound.totalProcessingDelay,
              framesDropped: window.rk.last.inbound.framesDropped,
              frameWidth: window.rk.last.inbound.frameWidth, frameHeight: window.rk.last.inbound.frameHeight } : null,
            sender: epStats, csv: snapshot ? snapshot.csv : measure.csv() };
          if (field) { field.submit(body); return; }
          var x = new XMLHttpRequest(); x.open('POST', '/api/report'); x.setRequestHeader('content-type', 'application/json'); x.send(JSON.stringify(body));
          }).catch(function (e) { if (field) field.error(e); else console.warn('report', e); });
        }, (warm + secs) * 1000);
      }
    }, 500);
  }
  // hwinfo=1: Chrome only exposes inbound-rtp decoderImplementation/powerEfficientDecoder to pages that had media-capture permission when the
  // RTCPeerConnection was created (lab flags --use-fake-ui-for-media-stream --use-fake-device-for-media-stream), so capture first, then connect.
  if (q.get('hwinfo') && navigator.mediaDevices && navigator.mediaDevices.getUserMedia) {
    navigator.mediaDevices.getUserMedia({ video: true }).then(function (st) { st.getTracks().forEach(function (t) { t.stop(); }); console.log('[viewer] hwinfo capture permission ok'); }).catch(function (e) { console.warn('[viewer] hwinfo failed', e && e.name, e && e.message); }).then(connect);
    return;
  }
  connect();
})();
