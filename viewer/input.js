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
    var slots = {}, used = {}, pending = {}, buttons = {}, captures = {}, positions = {};
    function nextSeq() { self.seq = (self.seq + 1) & 0xffff; return self.seq; }
    function sendPointer(m) { m.seq = nextSeq(); m.ts = RK.ts32(); sendBinary(RK.encodePointer(m), m.seq); }
    function discrete(m) { self.flush(); sendPointer(m); }
    function slotFor(e) {
      if (e.pointerType === 'mouse') return 0;
      if (slots[e.pointerId] != null) return slots[e.pointerId];
      for (var i = 1; i < 16; i++) if (!used[i]) { used[i] = true; slots[e.pointerId] = i; return i; }
      return -1;
    }
    function freeSlot(e) { var s = slots[e.pointerId]; if (s != null) { delete used[s]; delete slots[e.pointerId]; } }
    function pointerMsg(e, action, p, pid) {
      var c = p.c, scale = c ? c.vw / c.width : 1;
      return { action: action, x: p.x, y: p.y, ptype: RK.PTYPE[e.pointerType] || 0, pid: pid,
        buttons: e.buttons, button: action === RK.PA.MOVE ? -1 : e.button, mods: RK.modsOf(e), pressure: e.pressure,
        rx: (e.width || 0) / 2 * scale, ry: (e.height || 0) / 2 * scale };
    }
    // Coalescing (06 section 2.4): moves are sent immediately unless the channel is backed up; only the newest is kept.
    this.flush = function () {
      if (!self.enabled) return;
      Object.keys(pending).forEach(function (pid) { var m = pending[pid]; delete pending[pid]; sendPointer(m); });
    };
    this.backedUp = function () { return false; };

    function onDown(e) {
      if (!self.enabled) return;
      var p = mapPoint(video, e.clientX, e.clientY);
      if (!p || !p.inside) return;
      var pid = slotFor(e); if (pid < 0) return;
      e.preventDefault();
      captures[e.pointerId] = { pid: pid, type: e.pointerType };
      try { stage.setPointerCapture(e.pointerId); } catch (err) { /* ignore */ }
      if (e.pointerType === 'mouse' && document.activeElement !== self.keyTarget) stage.focus({ preventScroll: true });
      buttons[pid] = e.buttons; positions[pid] = p;
      discrete(pointerMsg(e, RK.PA.DOWN, p, pid));
      if (self.onPointerDown) self.onPointerDown(e);
    }
    function onMove(e) {
      if (!self.enabled) return;
      var pid = e.pointerType === 'mouse' ? 0 : slots[e.pointerId];
      if (pid == null) return; // touch/pen not down on the stage
      var p = mapPoint(video, e.clientX, e.clientY); if (!p) return;
      positions[pid] = p;
      var previous = buttons[pid] || 0, changed = previous ^ e.buttons;
      // Additional chord presses/releases arrive as pointermove. Keep each transition discrete.
      [1, 2, 4, 8, 16].forEach(function (bit, i) {
        if (!(changed & bit)) return;
        previous = (previous & ~bit) | (e.buttons & bit);
        var transition = pointerMsg(e, e.buttons & bit ? RK.PA.DOWN : RK.PA.UP, p, pid);
        transition.buttons = previous; transition.button = [0, 2, 1, 3, 4][i];
        discrete(transition);
      });
      buttons[pid] = e.buttons;
      if (e.pointerType === 'mouse' && !p.inside && !e.buttons) { if (!self.mouseOut) { self.mouseOut = true; discrete(pointerMsg(e, RK.PA.LEAVE, p, 0)); } return; }
      self.mouseOut = false;
      var m = pointerMsg(e, RK.PA.MOVE, p, pid);
      if (self.backedUp()) pending[pid] = m; else { delete pending[pid]; sendPointer(m); }
    }
    function onUp(e, cancel) {
      if (!self.enabled) return;
      var pid = e.pointerType === 'mouse' ? 0 : slots[e.pointerId];
      if (pid == null) return;
      var p = mapPoint(video, e.clientX, e.clientY) || positions[pid] || { x: 0, y: 0 };
      delete captures[e.pointerId];
      buttons[pid] = cancel ? 0 : e.buttons;
      discrete(pointerMsg(e, cancel ? RK.PA.CANCEL : RK.PA.UP, p, pid));
      if (e.pointerType !== 'mouse') freeSlot(e);
      e.preventDefault();
    }
    stage.addEventListener('pointerdown', onDown);
    stage.addEventListener('pointermove', onMove);
    stage.addEventListener('pointerup', function (e) { onUp(e, false); });
    stage.addEventListener('pointercancel', function (e) { onUp(e, true); });
    stage.addEventListener('lostpointercapture', function (e) {
      var capture = captures[e.pointerId];
      delete captures[e.pointerId];
      if (!self.enabled || !capture) return;
      if (capture.type === 'mouse') { if (buttons[0]) self.reset(); }
      else if (slots[e.pointerId] != null) {
        var m = { action: RK.PA.CANCEL, x: 0, y: 0, ptype: RK.PTYPE[capture.type], pid: capture.pid, buttons: 0, button: -1 };
        var p = positions[capture.pid]; if (p) { m.x = p.x; m.y = p.y; }
        discrete(m); freeSlot(e); delete buttons[capture.pid]; delete positions[capture.pid];
      }
    });
    stage.addEventListener('contextmenu', function (e) { e.preventDefault(); });
    // No browser gestures: touch-action:none on the stage, plus cancel legacy touch/gesture events (iOS/old Android).
    ['touchstart', 'touchmove', 'gesturestart'].forEach(function (t) { stage.addEventListener(t, function (e) { e.preventDefault(); }, { passive: false }); });
    stage.addEventListener('wheel', function (e) {
      if (!self.enabled) return;
      e.preventDefault();
      var p = mapPoint(video, e.clientX, e.clientY); if (!p || !p.inside) return;
      self.flush();
      var k = e.deltaMode === 0 ? p.c.vw / p.c.width : 1, seq = nextSeq();
      sendBinary(RK.encodeWheel({ mode: e.deltaMode, seq: seq, ts: RK.ts32(), x: p.x, y: p.y, dx: e.deltaX * k, dy: e.deltaY * k }), seq);
    }, { passive: false });

    // Keyboard: physical keys. Composition / keyCode 229 belong to the IME path (ime.js).
    this.sendKey = function (e, action) {
      if (!self.enabled) return;
      self.flush();
      var seq = nextSeq(), flags = (e.repeat ? 1 : 0) | (e.location === 2 ? 2 : 0) | (e.location === 3 ? 4 : 0);
      sendBinary(RK.encodeKey({ action: action, seq: seq, ts: RK.ts32(), mods: RK.modsOf(e), flags: flags, keyCode: e.keyCode, code: e.code, key: e.key }), seq);
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

    this.synthMove = function (nx, ny) {
      if (!self.enabled) return;
      discrete({ action: RK.PA.MOVE, x: nx * 65535, y: ny * 65535, ptype: 0, pid: 0, buttons: 0, button: -1, mods: 0 });
    };
    this.clearSession = function () {
      var oldCaptures = captures;
      pending = {}; slots = {}; used = {}; buttons = {}; captures = {}; positions = {}; self.mouseOut = false;
      Object.keys(oldCaptures).forEach(function (id) { try { stage.releasePointerCapture(+id); } catch (err) { /* already released */ } });
    };
    this.reset = function () {
      self.flush(); self.clearSession();
      var seq = nextSeq(); sendBinary(RK.encodeReset(seq, RK.ts32()), seq);
    };
  }
  RKInput.contentRect = contentRect;
  RKInput.mapPoint = mapPoint;
  root.RKInput = RKInput;
})(typeof self !== 'undefined' ? self : this);
