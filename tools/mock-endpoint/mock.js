// Mock endpoint (docs/protocol.md): a browser page that plays the endpoint role.
// canvas.captureStream -> H.264 (preferred) -> viewer, with the latency pixel marker, input echo and a fake
// text field that drives the IME/OSK messages. Params: ?instance=default&w=1920&h=1080&fps=30&kbps=12000&token=&hint=
(function () {
  'use strict';
  var q = new URLSearchParams(location.search);
  var W = +q.get('w') || 1920, H = +q.get('h') || 1080, FPS = +q.get('fps') || 30, KBPS = +q.get('kbps') || 12000;
  var canvas = document.getElementById('c'), ctx = canvas.getContext('2d');
  canvas.width = W; canvas.height = H;
  var stream = canvas.captureStream(0), track = stream.getVideoTracks()[0];
  if (q.get('hint')) track.contentHint = q.get('hint');
  var logEl = document.getElementById('log');
  function log() {
    var s = Array.prototype.join.call(arguments, ' ');
    console.log('[mock] ' + s);
    logEl.textContent = (new Date().toISOString().slice(11, 23) + ' ' + s + '\n' + logEl.textContent).slice(0, 4000);
  }

  // ---- remote "browser" state ----
  var S = {
    frameId: 0, inputSeq: 0, pointers: {}, mouse: null, lastKey: '', counts: { pointer: 0, wheel: 0, key: 0, reset: 0, ime: 0, ctl: 0 },
    field: { x: 0.3, y: 0.42, w: 0.4, h: 0.07, focused: false, text: '', comp: '' }, scroll: 0, imeSeq: 0, cursor: 'default'
  };
  window.mockState = S;
  window.mockStream = stream;

  function inField(nx, ny) { var f = S.field; return nx >= f.x && nx <= f.x + f.w && ny >= f.y && ny <= f.y + f.h; }

  function draw() {
    var t = performance.now() / 1000;
    ctx.fillStyle = '#1b2733'; ctx.fillRect(0, 0, W, H);
    // "scrolling page": text-like bars moving with time + wheel offset (stresses the encoder like S-scroll)
    var lineH = Math.round(H / 36), off = (t * 120 + S.scroll) % (lineH * 2);
    for (var y = -lineH * 2 + lineH * 3; y < H; y += lineH) {
      var yy = y - off, seed = Math.floor((y + S.scroll + t * 120) / lineH);
      ctx.fillStyle = (seed & 3) ? '#9fb3c8' : '#e2b04a';
      var len = (0.35 + 0.5 * Math.abs(Math.sin(seed * 12.9898))) * W * 0.55;
      ctx.fillRect(W * 0.05, yy, len, lineH * 0.45);
    }
    // moving box (motion, like S-game)
    var bx = (Math.sin(t * 1.3) * 0.4 + 0.5) * (W - 200), by = (Math.cos(t * 0.9) * 0.35 + 0.55) * (H - 200);
    ctx.fillStyle = '#c0392b'; ctx.fillRect(bx, by, 160, 160);
    // fake text field
    var f = S.field, fx = f.x * W, fy = f.y * H, fw = f.w * W, fh = f.h * H;
    ctx.fillStyle = f.focused ? '#fff' : '#ddd'; ctx.fillRect(fx, fy, fw, fh);
    ctx.lineWidth = 4; ctx.strokeStyle = f.focused ? '#2e86de' : '#777'; ctx.strokeRect(fx, fy, fw, fh);
    ctx.fillStyle = '#000'; ctx.font = Math.round(fh * 0.55) + 'px sans-serif'; ctx.textBaseline = 'middle';
    var shown = f.text + f.comp;
    ctx.fillText(shown || (f.focused ? '' : 'tap to type'), fx + 12, fy + fh / 2);
    if (f.comp) { var cw = ctx.measureText(f.text).width, ww = ctx.measureText(f.comp).width; ctx.fillRect(fx + 12 + cw, fy + fh * 0.8, ww, 3); }
    if (f.focused && (t * 2 | 0) % 2) ctx.fillRect(fx + 14 + ctx.measureText(shown).width, fy + fh * 0.2, 3, fh * 0.6);
    // pointers
    Object.keys(S.pointers).forEach(function (k) {
      var p = S.pointers[k];
      ctx.beginPath(); ctx.arc(p.x * W, p.y * H, Math.max(20, p.rx), 0, 6.283);
      ctx.fillStyle = p.ptype === 1 ? 'rgba(46,204,113,0.7)' : 'rgba(241,196,15,0.7)'; ctx.fill();
      ctx.fillStyle = '#000'; ctx.font = '24px sans-serif'; ctx.fillText(String(k), p.x * W - 7, p.y * H);
    });
    if (S.mouse) { ctx.strokeStyle = '#fff'; ctx.lineWidth = 2; var mx = S.mouse.x * W, my = S.mouse.y * H;
      ctx.beginPath(); ctx.moveTo(mx - 15, my); ctx.lineTo(mx + 15, my); ctx.moveTo(mx, my - 15); ctx.lineTo(mx, my + 15); ctx.stroke(); }
    ctx.fillStyle = '#fff'; ctx.font = '28px monospace'; ctx.textBaseline = 'alphabetic';
    var c = S.counts;
    ctx.fillText('mock endpoint ' + W + 'x' + H + '@' + FPS + '  frame ' + S.frameId + '  seq ' + S.inputSeq, 20, H - 60);
    ctx.fillText('ptr ' + c.pointer + ' wheel ' + c.wheel + ' key ' + c.key + ' ime ' + c.ime + ' last key: ' + S.lastKey, 20, H - 20);
    // marker last, on top
    RKMarker.draw(ctx, W, { frameId: S.frameId, t: RKClock.wall(), inputSeq: S.inputSeq });
    S.frameId = (S.frameId + 1) & 0xffff;
    if (track.requestFrame) track.requestFrame(); else if (stream.requestFrame) stream.requestFrame();
  }
  // Fixed-rate drawing; setTimeout drift correction.
  var next = performance.now();
  (function loop() { draw(); next += 1000 / FPS; var d = next - performance.now(); if (d < -100) { next = performance.now(); d = 0; } setTimeout(loop, Math.max(0, d)); })();

  // ---- signalling ----
  var pc = null, ch = null, sid = null, ws = null;
  var session = null, generation = 0;
  function current(s, p) { return session === s && generation === s.generation && pc === p; }
  function sendSig(m) { if (ws && ws.readyState === 1) ws.send(JSON.stringify(m)); }
  function connect() {
    var u = (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/signal?role=endpoint&instance=' +
      encodeURIComponent(q.get('instance') || 'default') + (q.get('token') ? '&token=' + encodeURIComponent(q.get('token')) : '');
    ws = new WebSocket(u);
    ws.onopen = function () { sendSig({ t: 'hello', v: RK.V, role: 'endpoint', instance: q.get('instance') || 'default', name: 'mock-endpoint', caps: { codecs: ['H264'], maxFps: FPS } }); log('signalling connected'); };
    ws.onmessage = function (e) { try { onSignal(JSON.parse(e.data)); } catch (err) { log('signalling: ' + err.message); } };
    ws.onerror = function () { ws.close(); };
    ws.onclose = function () { log('signalling closed; retry in 2 s'); closePc(); setTimeout(connect, 2000); };
  }
  function closePc() {
    generation++; session = null; sid = null;
    if (ch) { ch.input.close(); ch.ctl.close(); ch.probe.close(); ch = null; }
    if (pc) { var old = pc; pc = null; old.close(); }
    if (statsTimer) { clearInterval(statsTimer); statsTimer = null; }
    S.field.focused = false; S.field.comp = ''; S.inputSeq = 0; S.imeSeq = 0;
    S.pointers = {}; S.mouse = null; S.lastKey = ''; S.cursor = 'default'; S.vstats = null;
    S.counts = { pointer: 0, wheel: 0, key: 0, reset: 0, ime: 0, ctl: 0 }; prev = null;
  }

  function preferH264(transceiver) {
    if (!transceiver.setCodecPreferences || !RTCRtpSender.getCapabilities) return false;
    var codecs = RTCRtpSender.getCapabilities('video').codecs;
    function rank(c) {
      if (c.mimeType.toLowerCase() !== 'video/h264') return /rtx|red|ulpfec/i.test(c.mimeType) ? 5 : 7;
      var f = c.sdpFmtpLine || '';
      return (f.indexOf('packetization-mode=1') >= 0 ? 0 : 2) + (f.indexOf('42e01f') >= 0 ? 0 : 1);
    }
    codecs = codecs.slice().sort(function (a, b) { return rank(a) - rank(b); }); // H.264 first, others kept as fallback (e.g. Chromium builds without H.264)
    transceiver.setCodecPreferences(codecs);
    return true;
  }

  function onSignal(m) {
    if (m.t === 'session.open') {
      closePc(); sid = m.sid; log('session.open ' + sid.slice(0, 8));
      var s = session = { sid: sid, generation: generation, handshaken: false, candidates: [], outgoing: [], offerSent: false,
        remoteReady: false, negotiation: Promise.resolve(), ice: Promise.resolve() };
      pc = new RTCPeerConnection({ iceServers: m.iceServers || [] });
      var p = pc;
      var tr = pc.addTransceiver(track, { direction: 'sendonly', streams: [stream] });
      log('H.264 preferred: ' + preferH264(tr));
      ch = RK.createChannels(pc);
      var channels = ch;
      ch.input.onmessage = function (e) { if (current(s, p) && s.handshaken) onInput(e.data); };
      ch.ctl.onmessage = function (e) { if (current(s, p)) { try { onCtl(JSON.parse(e.data)); } catch (err) { log('ctl: ' + err.message); } } };
      ch.probe.onmessage = function (e) { if (current(s, p)) { try { RKClock.answer(channels.probe, JSON.parse(e.data)); } catch (err) { log('probe: ' + err.message); } } };
      ch.ctl.onopen = function () { if (current(s, p)) log('datachannels open'); };
      pc.onicecandidate = function (e) {
        if (!current(s, p)) return;
        var ice = { t: 'ice', sid: s.sid, candidate: e.candidate ? e.candidate.toJSON() : null };
        if (s.offerSent) sendSig(ice); else s.outgoing.push(ice);
      };
      pc.onconnectionstatechange = function () { if (current(s, p)) log('pc ' + p.connectionState); };
      makeOffer(s, p, tr.sender, false);
      statsTimer = setInterval(function () { if (current(s, p)) sendStats(); }, 1000);
    } else if (!session || !pc || m.sid !== sid) return;
    else if (m.t === 'answer') {
      var active = session, peer = pc;
      active.negotiation = active.negotiation.then(async function () {
        if (!current(active, peer)) return;
        active.remoteReady = false;
        await peer.setRemoteDescription({ type: 'answer', sdp: m.sdp });
        if (!current(active, peer)) return;
        active.remoteReady = true; await drainIce(active, peer);
        if (!current(active, peer)) return;
        var h = /a=rtpmap:\d+ (\w+)\/90000/.exec(peer.remoteDescription.sdp); log('answer applied; first codec ' + (h && h[1]));
      }).catch(function (e) { if (current(active, peer)) log('answer: ' + e.message); });
    } else if (m.t === 'ice') {
      session.candidates.push(m.candidate); drainIce(session, pc);
    } else if (m.t === 'restart') makeOffer(session, pc, null, true);
    else if (m.t === 'bye') { log('bye ' + m.reason); closePc(); }
  }
  async function setSenderParameters(s, p, sender) {
    if (!current(s, p)) return false;
    try {
      var params = sender.getParameters();
      if (!params.encodings || !params.encodings.length) params.encodings = [{}];
      params.encodings[0].maxBitrate = KBPS * 1000; params.encodings[0].maxFramerate = FPS;
      params.degradationPreference = 'maintain-resolution';
      await sender.setParameters(params); return true;
    } catch (e) { if (current(s, p)) log('setParameters: ' + e.message); return false; }
  }
  function makeOffer(s, p, sender, restart) {
    s.negotiation = s.negotiation.then(async function () {
      if (!current(s, p)) return;
      s.offerSent = false; s.remoteReady = false;
      var configured = sender ? await setSenderParameters(s, p, sender) : true;
      if (!current(s, p)) return;
      var offer = await p.createOffer(restart ? { iceRestart: true } : {});
      if (!current(s, p)) return;
      await p.setLocalDescription(offer);
      if (!current(s, p)) return;
      // Some older senders expose encodings only after local SDP; ICE stays queued through the fallback.
      if (!configured) await setSenderParameters(s, p, sender);
      if (!current(s, p)) return;
      sendSig({ t: 'offer', sid: s.sid, sdp: p.localDescription.sdp });
      s.offerSent = true;
      s.outgoing.splice(0).forEach(sendSig);
    }).catch(function (e) { if (current(s, p)) log('offer: ' + e.message); });
  }
  function drainIce(s, p) {
    if (!current(s, p) || !s.remoteReady) return s.ice;
    s.ice = s.ice.then(async function () {
      while (current(s, p) && s.remoteReady && s.candidates.length) {
        var c = s.candidates.shift();
        try { await p.addIceCandidate(c === null ? { candidate: '' } : c); }
        catch (e) { if (c !== null && current(s, p)) log('ice: ' + e.message); }
      }
    }).catch(function (e) { if (current(s, p)) log('ice: ' + e.message); });
    return s.ice;
  }

  // ---- input (binary) ----
  function onInput(buf) {
    var m = RK.decodeInput(buf);
    if (!m) { log('bad input message'); return; }
    S.inputSeq = m.seq;
    var nx = m.x / 65535, ny = m.y / 65535;
    if (m.kind === RK.KIND.POINTER) {
      S.counts.pointer++;
      if (m.ptype === RK.PTYPE.mouse) S.mouse = m.action === RK.PA.LEAVE ? null : { x: nx, y: ny };
      var setCursor = m.ptype === 0 && inField(nx, ny) ? 'text' : 'default';
      if (setCursor !== S.cursor) { S.cursor = setCursor; sendCtl({ t: 'cursor', css: setCursor }); }
      if (m.action === RK.PA.DOWN) {
        S.pointers[m.pid] = { x: nx, y: ny, ptype: m.ptype, rx: m.rx };
        focusField(inField(nx, ny), m.ptype);
        log('down pid=' + m.pid + ' ' + RK.PTYPE_NAME[m.ptype] + ' btn=' + m.button + ' at ' + nx.toFixed(3) + ',' + ny.toFixed(3));
      } else if (m.action === RK.PA.MOVE) {
        if (S.pointers[m.pid]) { S.pointers[m.pid].x = nx; S.pointers[m.pid].y = ny; }
      } else if (m.action === RK.PA.UP || m.action === RK.PA.CANCEL) {
        delete S.pointers[m.pid];
        log((m.action === RK.PA.UP ? 'up' : 'cancel') + ' pid=' + m.pid);
      }
    } else if (m.kind === RK.KIND.WHEEL) {
      S.counts.wheel++; S.scroll += m.dy * (m.mode === 1 ? 40 : m.mode === 2 ? H : 1);
    } else if (m.kind === RK.KIND.KEY) {
      S.counts.key++;
      if (m.action === 0) {
        S.lastKey = (m.mods & RK.MOD.CTRL ? 'Ctrl+' : '') + m.code + '/' + m.key;
        if (S.field.focused && !(m.mods & (RK.MOD.CTRL | RK.MOD.META))) {
          if (m.key === 'Backspace') S.field.text = S.field.text.slice(0, -1);
          else if (m.key.length === 1) S.field.text += m.key;
        }
        log('key ' + S.lastKey + ' mods=' + m.mods);
      }
    } else if (m.kind === RK.KIND.RESET) {
      S.counts.reset++; S.pointers = {}; log('reset');
    }
  }
  function focusField(focus, ptype) {
    var f = S.field;
    if (focus === f.focused) { if (focus && ptype === 1) sendCtl({ t: 'ime.vk', show: true, seq: ++S.imeSeq }); return; }
    f.focused = focus; f.comp = '';
    sendCtl({ t: 'ime.state', seq: ++S.imeSeq, focused: focus, type: focus ? 'text' : 'none', mode: 'default', flags: 0,
      rect: { x: Math.round(f.x * 65535), y: Math.round(f.y * 65535), w: Math.round(f.w * 65535), h: Math.round(f.h * 65535) },
      reason: ['mouse', 'touch', 'pen'][ptype] || 'other', composing: false });
    // Windows policy (VirtualKeyboardControllerWin): show the keyboard on touch focus only; hide on blur.
    if (focus && ptype === 1) sendCtl({ t: 'ime.vk', show: true, seq: ++S.imeSeq });
    if (!focus) sendCtl({ t: 'ime.vk', show: false, seq: ++S.imeSeq });
  }
  function sendCtl(m) { if (ch && ch.ctl.readyState === 'open') ch.ctl.send(JSON.stringify(m)); }
  function onCtl(m) {
    if (m.t === 'hello') {
      if (m.v !== 1 || m.input !== 1) { sendSig({ t: 'bye', sid: sid, reason: 'version' }); closePc(); return; }
      session.handshaken = true;
    } else if (!session.handshaken) return;
    S.counts.ctl++;
    var f = S.field;
    switch (m.t) {
      case 'hello': log('viewer hello ' + JSON.stringify(m.caps) + ' view ' + JSON.stringify(m.view));
        sendCtl({ t: 'hello', v: RK.V, input: RK.INPUT_V, frame: { w: W, h: H }, dsf: 1, name: 'mock-endpoint' }); break;
      case 'resize': log('viewer resize ' + m.w + 'x' + m.h + '@' + m.dpr); break;
      case 'focus': log('viewer focus ' + m.focused); if (!m.focused) S.pointers = {}; break;
      case 'ime.commit': S.counts.ime++; if (f.focused) { f.text += m.text; f.comp = ''; } log('ime.commit ' + JSON.stringify(m.text)); break;
      case 'ime.compose': S.counts.ime++; if (f.focused) f.comp = m.text; break;
      case 'ime.delete': S.counts.ime++; if (f.focused) f.text = f.text.slice(0, Math.max(0, f.text.length - (m.before | 0))); log('ime.delete ' + m.before); break;
      case 'ime.key': S.counts.ime++; if (f.focused && m.key === 'Backspace') f.text = f.text.slice(0, -1); log('ime.key ' + m.key); break;
      case 'ime.hidden': log('viewer keyboard hidden'); break;
      case 'keyframe': log('keyframe requested (not controllable from a canvas track)'); break;
      case 'vstats': S.vstats = m; break;
    }
  }

  // ---- stats ----
  var statsTimer = null, prev = null;
  function sendStats() {
    if (!pc) return;
    var s = session, p = pc;
    p.getStats().then(function (r) {
      if (!current(s, p)) return;
      var o = null;
      r.forEach(function (s) { if (s.type === 'outbound-rtp' && s.kind === 'video') o = s; });
      if (!o) return;
      var now = performance.now();
      if (prev) {
        var dt = (now - prev.now) / 1000, df = o.framesEncoded - prev.f;
        sendCtl({ t: 'stats', fps: +(df / dt).toFixed(1), kbps: Math.round((o.bytesSent - prev.b) * 8 / dt / 1000),
          encMs: { p50: df ? +((o.totalEncodeTime - prev.e) / df * 1000).toFixed(2) : null, p99: null },
          qp: df && o.qpSum != null ? +((o.qpSum - prev.qp) / df).toFixed(1) : null, idr: o.keyFramesEncoded,
          backend: o.encoderImplementation || '?', queue: 0, bweKbps: null, frame: { w: o.frameWidth, h: o.frameHeight },
          limit: o.qualityLimitationReason, inputs: S.counts });
      }
      prev = { now: now, f: o.framesEncoded, b: o.bytesSent, e: o.totalEncodeTime || 0, qp: o.qpSum || 0 };
    }).catch(function (e) { if (current(s, p)) log('stats: ' + e.message); });
  }
  connect();
})();

// Calibration (?loop=1): display the captured canvas stream locally (no encoder, no network) and measure
// draw -> captureStream -> local display with the same marker decoder. Subtract from harness numbers to isolate encode+transport+decode.
(function () {
  if (new URLSearchParams(location.search).get('loop') !== '1' || !window.RKMeasure) return;
  var v = document.createElement('video'); v.muted = true; v.autoplay = true; v.playsInline = true; v.style.width = '320px';
  document.body.appendChild(v);
  v.srcObject = window.mockStream;
  setTimeout(function () {
    var m = new RKMeasure(v, { offset: 0, minRtt: 0, toLocal: function (t) { return t; } });
    setInterval(function () { var s = m.summary().lat; console.log('[mock] loop calibration P50 ' + (s.p50 || 0).toFixed(1) + ' P99 ' + (s.p99 || 0).toFixed(1) + ' n=' + s.n); }, 5000);
  }, 1000);
})();
