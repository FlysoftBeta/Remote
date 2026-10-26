// remotekit viewer (D13, design/06-protocol.md). Must run on Chrome 89+: newer APIs only behind feature detection.
// URL params: instance, token, measure=1 (pixel-marker latency), report=<s> (POST summary to /api/report after s seconds),
//             my=<source px> (fixed marker row; default auto-detect),
//             delay=<ms>|off (receiver jitter-buffer hint, default 0), stats=0 (hide overlay), cp=<host:port>.
(function () {
  'use strict';
  var q = new URLSearchParams(location.search);
  var video = document.getElementById('video'), stage = document.getElementById('stage');
  var overlay = document.getElementById('overlay'), statusEl = document.getElementById('status');
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
      sendSig({ t: 'hello', v: RK.V, role: 'viewer', ua: navigator.userAgent,
        caps: { touch: navigator.maxTouchPoints || 0, dpr: devicePixelRatio, osk: isTouch, codecs: h264Caps().map(function (c) { return c.sdpFmtpLine || ''; }) } });
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
    ch = null; epHello = null; epStats = null; prev = null; window.rk.last = null; video.srcObject = null; stage.style.cursor = 'default';
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
    pc = new RTCPeerConnection({ iceServers: s.iceServers, bundlePolicy: 'max-bundle' });
    var p = pc;
    ch = RK.createChannels(pc);
    var channels = ch;
    ch.ctl.onopen = function () {
      if (!current(s, p)) return;
      sendCtl({ t: 'hello', v: RK.V, input: RK.INPUT_V, caps: { touch: navigator.maxTouchPoints || 0, osk: isTouch, dpr: devicePixelRatio },
        view: { w: innerWidth, h: innerHeight, dpr: devicePixelRatio } });
      sendCtl({ t: 'focus', focused: document.hasFocus() && !document.hidden });
    };
    ch.ctl.onmessage = function (e) { if (current(s, p)) { try { onCtl(JSON.parse(e.data)); } catch (err) { console.warn('ctl', err); } } };
    ch.probe.onopen = function () { if (!current(s, p)) return; clock = new RKClock(channels.probe); if (q.get('measure') === '1' && !measure) measure = new RKMeasure(video, clock); else if (measure) measure.clock = clock; };
    ch.probe.onmessage = function (e) { if (current(s, p) && clock) { try { clock.onMessage(JSON.parse(e.data)); } catch (err) { console.warn('probe', err); } } };
    ch.input.onopen = function () { if (current(s, p)) input.enabled = !!epHello; };
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
      epHello = m; input.enabled = ch.input.readyState === 'open';
    }
    else if (!epHello) return;
    else if (ime.onCtl(m)) return;
    else if (m.t === 'stats') epStats = m;
    else if (m.t === 'cursor') stage.style.cursor = /^[a-z-]+$/.test(m.css) ? m.css : 'default';
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
  var rzTimer = null;
  window.addEventListener('resize', function () {
    clearTimeout(rzTimer);
    rzTimer = setTimeout(function () { sendCtl({ t: 'resize', w: innerWidth, h: innerHeight, dpr: devicePixelRatio }); }, 200);
  });
  stage.addEventListener('pointerdown', function () { if (video.paused) { var play = video.play(); if (play && play.catch) play.catch(function () {}); } });

  // ---- stats overlay (1 Hz) ----
  var prev = null, showStats = q.get('stats') !== '0';
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
      }
      prev = { now: now, fd: inb.framesDecoded, b: inb.bytesReceived, je: jbSrc.jitterBufferEmittedCount || 0, jb: jbSrc.jitterBufferDelay || 0, td: inb.totalDecodeTime || 0 };
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
      window.rk.last = { inbound: inb, derived: v, codec: c.mimeType, hint: hintInfo, decoder: inb.decoderImplementation };
    }).catch(function (e) { if (current(s, p)) console.warn('stats', e); });
  }, 1000);

  // ---- automated report for tools/latency/run.mjs ----
  if (q.get('report')) {
    var secs = +q.get('report'), warm = +(q.get('warmup') || 3);
    var started = null;
    // Synthetic input for input-to-photon: a mouse move every ~97 ms (the mock echoes the applied seq in the marker).
    if (q.get('autoinput') === '1') setInterval(function () { if (input.enabled) input.synthMove(0.2 + 0.6 * Math.random(), 0.6 + 0.3 * Math.random()); }, 97);
    setInterval(function () {
      if (session && started !== session && measure && clock && clock.offset != null && video.videoWidth) {
        var s = session, p = pc;
        started = s;
        setTimeout(function () { if (current(s, p)) measure.reset(); }, warm * 1000);
        setTimeout(function () {
          if (!current(s, p)) return;
          var body = { kind: 'latency', ua: navigator.userAgent, hint: hintInfo, n: measure.lat.length, summary: measure.summary(),
            stats: window.rk.last ? { codec: window.rk.last.codec, decoder: window.rk.last.decoder, derived: window.rk.last.derived,
              jitterBufferDelay: window.rk.last.inbound.jitterBufferDelay, emitted: window.rk.last.inbound.jitterBufferEmittedCount,
              frameWidth: window.rk.last.inbound.frameWidth, frameHeight: window.rk.last.inbound.frameHeight } : null,
            sender: epStats, csv: measure.csv() };
          var x = new XMLHttpRequest(); x.open('POST', '/api/report'); x.setRequestHeader('content-type', 'application/json'); x.send(JSON.stringify(body));
        }, (warm + secs) * 1000);
      }
    }, 500);
  }
  connect();
})();
