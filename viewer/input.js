// Input capture and coordinate mapping (design/06-protocol.md section 2). Chrome 89+, no newer APIs without fallback.
// RKInput(stage, video, send) ; send(arrayBuffer, seq, immediate) is provided by the viewer.
(function (root) {
  'use strict';
  var RK = root.RK;

  // Visible video content rect inside the element (object-fit: contain => letterbox/pillarbox).
  function contentRect(video) {
    var r = video.getBoundingClientRect(), vw = video.videoWidth, vh = video.videoHeight;
    if (!vw || !vh) return null;
    var s = Math.min(r.width / vw, r.height / vh), cw = vw * s, ch = vh * s;
    return { left: r.left + (r.width - cw) / 2, top: r.top + (r.height - ch) / 2, width: cw, height: ch, scale: s, vw: vw, vh: vh };
  }
  // CSS client coords -> normalized 0..65535 (clamped) + inside flag. DPR does not matter: both rects are CSS px.
  function mapPoint(video, cx, cy) {
    var c = contentRect(video);
    if (!c) return null;
    var nx = (cx - c.left) / c.width, ny = (cy - c.top) / c.height;
    return { x: nx * 65535, y: ny * 65535, inside: nx >= 0 && nx <= 1 && ny >= 0 && ny <= 1, c: c };
  }

  function RKInput(stage, video, sendBinary) {
    var self = this;
    this.seq = 0; this.enabled = false; this.keyTarget = null; // keyTarget: element whose key events we skip (IME textarea handles its own)
    var slots = {}, used = {}, pending = {}, mods = 0, buttons = 0;
    function nextSeq() { self.seq = (self.seq + 1) & 0xffff; return self.seq; }
    function send(buf, seq) { sendBinary(buf, seq); }
    function slotFor(e) {
      if (e.pointerType === 'mouse') return 0;
      if (slots[e.pointerId] != null) return slots[e.pointerId];
      for (var i = 0; i < 16; i++) if (!used[i]) { used[i] = true; slots[e.pointerId] = i; return i; }
      return -1;
    }
    function freeSlot(e) { var s = slots[e.pointerId]; if (s != null) { delete used[s]; delete slots[e.pointerId]; } }
    function pointerMsg(e, action, p, pid) {
      var seq = nextSeq(), c = p.c, scale = c ? c.vw / c.width : 1;
      return { buf: RK.encodePointer({ action: action, seq: seq, ts: RK.ts32(), x: p.x, y: p.y, ptype: RK.PTYPE[e.pointerType] || 0, pid: pid,
        buttons: e.buttons, button: action === RK.PA.MOVE ? -1 : e.button, mods: RK.modsOf(e), pressure: e.pressure,
        rx: (e.width || 0) / 2 * scale, ry: (e.height || 0) / 2 * scale }), seq: seq };
    }
    // Coalescing (06 section 2.4): moves are sent immediately unless the channel is backed up; only the newest is kept.
    this.flush = function () {
      Object.keys(pending).forEach(function (pid) { var m = pending[pid]; delete pending[pid]; send(m.buf, m.seq); });
    };
    function flushPid(pid) { var m = pending[pid]; if (m) { delete pending[pid]; send(m.buf, m.seq); } }
    this.backedUp = function () { return false; };

    function onDown(e) {
      if (!self.enabled) return;
      var p = mapPoint(video, e.clientX, e.clientY);
      if (!p || !p.inside) return;
      var pid = slotFor(e); if (pid < 0) return;
      e.preventDefault();
      try { stage.setPointerCapture(e.pointerId); } catch (err) { /* ignore */ }
      if (e.pointerType === 'mouse' && document.activeElement !== self.keyTarget) stage.focus({ preventScroll: true });
      flushPid(pid);
      var m = pointerMsg(e, RK.PA.DOWN, p, pid); send(m.buf, m.seq);
      if (self.onPointerDown) self.onPointerDown(e);
    }
    function onMove(e) {
      if (!self.enabled) return;
      var pid = e.pointerType === 'mouse' ? 0 : slots[e.pointerId];
      if (pid == null) return; // touch/pen not down on the stage
      var p = mapPoint(video, e.clientX, e.clientY); if (!p) return;
      if (e.pointerType === 'mouse' && !p.inside && !e.buttons) { if (!self.mouseOut) { self.mouseOut = true; var l = pointerMsg(e, RK.PA.LEAVE, p, 0); send(l.buf, l.seq); } return; }
      self.mouseOut = false;
      var m = pointerMsg(e, RK.PA.MOVE, p, pid);
      if (self.backedUp()) pending[pid] = m; else { delete pending[pid]; send(m.buf, m.seq); }
    }
    function onUp(e, cancel) {
      if (!self.enabled) return;
      var pid = e.pointerType === 'mouse' ? 0 : slots[e.pointerId];
      if (pid == null) return;
      var p = mapPoint(video, e.clientX, e.clientY) || { x: 0, y: 0 };
      flushPid(pid);
      var m = pointerMsg(e, cancel ? RK.PA.CANCEL : RK.PA.UP, p, pid); send(m.buf, m.seq);
      if (e.pointerType !== 'mouse') freeSlot(e);
      e.preventDefault();
    }
    stage.addEventListener('pointerdown', onDown);
    stage.addEventListener('pointermove', onMove);
    stage.addEventListener('pointerup', function (e) { onUp(e, false); });
    stage.addEventListener('pointercancel', function (e) { onUp(e, true); });
    stage.addEventListener('lostpointercapture', function (e) { if (e.pointerType !== 'mouse' && slots[e.pointerId] != null) onUp(e, true); });
    stage.addEventListener('contextmenu', function (e) { e.preventDefault(); });
    // No browser gestures: touch-action:none on the stage, plus cancel legacy touch/gesture events (iOS/old Android).
    ['touchstart', 'touchmove', 'gesturestart'].forEach(function (t) { stage.addEventListener(t, function (e) { e.preventDefault(); }, { passive: false }); });
    stage.addEventListener('wheel', function (e) {
      if (!self.enabled) return;
      e.preventDefault();
      var p = mapPoint(video, e.clientX, e.clientY); if (!p || !p.inside) return;
      var k = e.deltaMode === 0 ? p.c.vw / p.c.width : 1, seq = nextSeq();
      send(RK.encodeWheel({ mode: e.deltaMode, seq: seq, ts: RK.ts32(), x: p.x, y: p.y, dx: e.deltaX * k, dy: e.deltaY * k }), seq);
    }, { passive: false });

    // Keyboard: physical keys. Composition / keyCode 229 belong to the IME path (ime.js).
    this.sendKey = function (e, action) {
      var seq = nextSeq(), flags = (e.repeat ? 1 : 0) | (e.location === 2 ? 2 : 0) | (e.location === 3 ? 4 : 0);
      send(RK.encodeKey({ action: action, seq: seq, ts: RK.ts32(), mods: RK.modsOf(e), flags: flags, keyCode: e.keyCode, code: e.code, key: e.key }), seq);
    };
    function onKey(e, action) {
      if (!self.enabled || e.isComposing || e.keyCode === 229) return;
      if (e.target === self.keyTarget) return; // ime.js forwards these itself
      if (e.key === 'F11' || e.key === 'F12') return;
      e.preventDefault();
      self.sendKey(e, action);
    }
    window.addEventListener('keydown', function (e) { onKey(e, 0); });
    window.addEventListener('keyup', function (e) { onKey(e, 1); });

    this.reset = function () {
      pending = {}; slots = {}; used = {};
      var seq = nextSeq(); send(RK.encodeReset(seq, RK.ts32()), seq);
    };
  }
  RKInput.contentRect = contentRect;
  RKInput.mapPoint = mapPoint;
  root.RKInput = RKInput;
})(typeof self !== 'undefined' ? self : this);
