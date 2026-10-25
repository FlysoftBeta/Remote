// remotekit protocol v1 (design/06-protocol.md): constants and the binary `input` DataChannel codec.
// Plain ES2017 script: works as a <script> in Chrome 89+ (global RK) and via require() in Node.
(function (root, factory) {
  var api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.RK = Object.assign(root.RK || {}, api);
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';
  var V = 1, INPUT_V = 1;
  var KIND = { POINTER: 1, WHEEL: 2, KEY: 3, RESET: 4 };
  var PA = { DOWN: 0, MOVE: 1, UP: 2, CANCEL: 3, LEAVE: 4 };
  var PTYPE = { mouse: 0, touch: 1, pen: 2 };
  var PTYPE_NAME = ['mouse', 'touch', 'pen'];
  var MOD = { SHIFT: 1, CTRL: 2, ALT: 4, META: 8, CAPS: 16, NUM: 32, ALTGR: 64 };
  // DataChannels: negotiated, fixed ids (06 section 2).
  var CHANNELS = [
    { label: 'input', id: 0, opts: { ordered: true } },
    { label: 'ctl', id: 1, opts: { ordered: true } },
    { label: 'probe', id: 2, opts: { ordered: false, maxRetransmits: 0 } }
  ];

  function wallMs() { return performance.timeOrigin + performance.now(); }
  function ts32() { return Math.floor(wallMs() * 10) >>> 0; }
  function clampU16(v) { v = Math.round(v); return v < 0 ? 0 : v > 65535 ? 65535 : v; }
  function clampI16(v) { v = Math.round(v); return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }
  function clampU8(v) { v = Math.round(v); return v < 0 ? 0 : v > 255 ? 255 : v; }

  function header(dv, kind, action, seq, ts) {
    dv.setUint8(0, kind); dv.setUint8(1, action);
    dv.setUint16(2, seq & 0xffff, true); dv.setUint32(4, ts >>> 0, true);
  }

  // p: {action, seq, ts, x, y (0..65535), ptype, pid, buttons, button, mods, pressure (0..1), rx, ry}
  function encodePointer(p) {
    var b = new ArrayBuffer(20), dv = new DataView(b);
    header(dv, KIND.POINTER, p.action, p.seq, p.ts);
    dv.setUint16(8, clampU16(p.x), true); dv.setUint16(10, clampU16(p.y), true);
    dv.setUint8(12, p.ptype | 0); dv.setUint8(13, p.pid & 15);
    dv.setUint8(14, p.buttons & 255); dv.setInt8(15, p.button == null ? -1 : p.button);
    dv.setUint8(16, p.mods | 0); dv.setUint8(17, clampU8((p.pressure || 0) * 255));
    dv.setUint8(18, clampU8(p.rx || 0)); dv.setUint8(19, clampU8(p.ry || 0));
    return b;
  }
  // w: {mode, seq, ts, x, y, dx, dy}  (dx/dy already in frame px for mode 0)
  function encodeWheel(w) {
    var b = new ArrayBuffer(16), dv = new DataView(b);
    header(dv, KIND.WHEEL, w.mode | 0, w.seq, w.ts);
    dv.setUint16(8, clampU16(w.x), true); dv.setUint16(10, clampU16(w.y), true);
    dv.setInt16(12, clampI16(w.dx * 4), true); dv.setInt16(14, clampI16(w.dy * 4), true);
    return b;
  }
  var enc = typeof TextEncoder !== 'undefined' ? new TextEncoder() : null;
  var dec = typeof TextDecoder !== 'undefined' ? new TextDecoder() : null;
  // k: {action, seq, ts, mods, flags, keyCode, code, key}
  function encodeKey(k) {
    var code = enc.encode(k.code || '').subarray(0, 32), key = enc.encode(k.key || '').subarray(0, 32);
    var b = new ArrayBuffer(14 + code.length + key.length), dv = new DataView(b);
    header(dv, KIND.KEY, k.action, k.seq, k.ts);
    dv.setUint8(8, k.mods | 0); dv.setUint8(9, k.flags | 0); dv.setUint16(10, k.keyCode & 0xffff, true);
    dv.setUint8(12, code.length); dv.setUint8(13, key.length);
    var u8 = new Uint8Array(b); u8.set(code, 14); u8.set(key, 14 + code.length);
    return b;
  }
  function encodeReset(seq, ts) {
    var b = new ArrayBuffer(8); header(new DataView(b), KIND.RESET, 0, seq, ts); return b;
  }
  var MIN_LEN = { 1: 20, 2: 16, 3: 14, 4: 8 };
  // Returns a plain object or null (malformed/unknown). Mirrors what the C++ endpoint must do.
  function decodeInput(buf) {
    var u8 = buf instanceof Uint8Array ? buf : new Uint8Array(buf);
    if (u8.length < 8) return null;
    var dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength), kind = dv.getUint8(0);
    if (!MIN_LEN[kind] || u8.length < MIN_LEN[kind]) return null;
    var m = { kind: kind, action: dv.getUint8(1), seq: dv.getUint16(2, true), ts: dv.getUint32(4, true) };
    if (kind === KIND.POINTER) {
      m.x = dv.getUint16(8, true); m.y = dv.getUint16(10, true); m.ptype = dv.getUint8(12); m.pid = dv.getUint8(13);
      m.buttons = dv.getUint8(14); m.button = dv.getInt8(15); m.mods = dv.getUint8(16);
      m.pressure = dv.getUint8(17) / 255; m.rx = dv.getUint8(18); m.ry = dv.getUint8(19);
    } else if (kind === KIND.WHEEL) {
      m.mode = m.action; m.x = dv.getUint16(8, true); m.y = dv.getUint16(10, true);
      m.dx = dv.getInt16(12, true) / 4; m.dy = dv.getInt16(14, true) / 4;
    } else if (kind === KIND.KEY) {
      m.mods = dv.getUint8(8); m.flags = dv.getUint8(9); m.keyCode = dv.getUint16(10, true);
      var cl = dv.getUint8(12), kl = dv.getUint8(13);
      if (u8.length < 14 + cl + kl) return null;
      m.code = dec.decode(u8.subarray(14, 14 + cl)); m.key = dec.decode(u8.subarray(14 + cl, 14 + cl + kl));
    }
    return m;
  }
  function modsOf(e) {
    var m = 0;
    if (e.shiftKey) m |= MOD.SHIFT; if (e.ctrlKey) m |= MOD.CTRL; if (e.altKey) m |= MOD.ALT; if (e.metaKey) m |= MOD.META;
    if (e.getModifierState) {
      if (e.getModifierState('CapsLock')) m |= MOD.CAPS;
      if (e.getModifierState('NumLock')) m |= MOD.NUM;
      if (e.getModifierState('AltGraph')) m |= MOD.ALTGR;
    }
    return m;
  }
  function createChannels(pc) {
    var ch = {};
    CHANNELS.forEach(function (c) {
      var o = Object.assign({ negotiated: true, id: c.id }, c.opts);
      ch[c.label] = pc.createDataChannel(c.label, o);
      ch[c.label].binaryType = 'arraybuffer';
    });
    return ch;
  }
  return {
    V: V, INPUT_V: INPUT_V, KIND: KIND, PA: PA, PTYPE: PTYPE, PTYPE_NAME: PTYPE_NAME, MOD: MOD, CHANNELS: CHANNELS,
    wallMs: wallMs, ts32: ts32, encodePointer: encodePointer, encodeWheel: encodeWheel, encodeKey: encodeKey,
    encodeReset: encodeReset, decodeInput: decodeInput, modsOf: modsOf, createChannels: createChannels
  };
});
