// Frame marker encoded in pixels (design/06-protocol.md section 4.2, design/03 section 5).
// One row at the top-left of the frame: 2 sync cells (white, black) + 72 data cells, cell = frameWidth/160 px.
// Data (big-endian bits): frameId u16 | t u32 (sender wall-clock ms, low 32 bits) | inputSeq u16 | crc8(poly 0x07).
// Plain script: global RKMarker in browsers, module.exports in Node.
(function (root, factory) {
  var api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.RKMarker = api;
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';
  var SYNC = 2, BITS = 72, CELLS = SYNC + BITS, TWO32 = 4294967296;
  function cellFor(frameWidth) { return frameWidth / 160; }
  function crc8(bytes, n) {
    var c = 0;
    for (var i = 0; i < n; i++) {
      c ^= bytes[i];
      for (var k = 0; k < 8; k++) c = c & 0x80 ? ((c << 1) ^ 0x07) & 0xff : (c << 1) & 0xff;
    }
    return c;
  }
  function pack(m) {
    var b = new Uint8Array(9), t = m.t >>> 0;
    b[0] = (m.frameId >> 8) & 255; b[1] = m.frameId & 255;
    b[2] = t >>> 24; b[3] = (t >>> 16) & 255; b[4] = (t >>> 8) & 255; b[5] = t & 255;
    b[6] = (m.inputSeq >> 8) & 255; b[7] = m.inputSeq & 255;
    b[8] = crc8(b, 8);
    return b;
  }
  // Draw onto a 2D context. m = {frameId, t (ms, any magnitude; low 32 bits kept), inputSeq}.
  function draw(ctx, frameWidth, m) {
    var cell = Math.max(4, Math.round(cellFor(frameWidth))), b = pack({ frameId: m.frameId & 0xffff, t: m.t % TWO32, inputSeq: m.inputSeq & 0xffff });
    ctx.fillStyle = '#000'; ctx.fillRect(0, 0, CELLS * cell + cell, cell * 2);
    for (var i = 0; i < CELLS; i++) {
      var bit = i === 0 ? 1 : i === 1 ? 0 : (b[(i - SYNC) >> 3] >> (7 - ((i - SYNC) & 7))) & 1;
      ctx.fillStyle = bit ? '#fff' : '#000';
      ctx.fillRect(i * cell, 0, cell, cell);
    }
    return cell;
  }
  // Decode from RGBA pixels covering at least [0, CELLS*cell) x [0, cell) of the frame.
  // data: Uint8ClampedArray, stride: width of that pixel buffer, cell: float cell size in those pixels.
  function decode(data, stride, cell) {
    function luma(i) {
      var cx = Math.floor((i + 0.5) * cell), cy = Math.floor(cell / 2), r = Math.max(1, Math.floor(cell / 4)), s = 0, n = 0;
      for (var y = cy - r; y <= cy + r; y++) for (var x = cx - r; x <= cx + r; x++) {
        var o = (y * stride + x) * 4; s += 0.299 * data[o] + 0.587 * data[o + 1] + 0.114 * data[o + 2]; n++;
      }
      return s / n;
    }
    var white = luma(0), black = luma(1);
    if (white - black < 60) return null;
    var thr = (white + black) / 2, b = new Uint8Array(9);
    for (var i = 0; i < BITS; i++) if (luma(i + SYNC) > thr) b[i >> 3] |= 1 << (7 - (i & 7));
    if (crc8(b, 8) !== b[8]) return null;
    return { frameId: (b[0] << 8) | b[1], t: ((b[2] << 24) | (b[3] << 16) | (b[4] << 8) | b[5]) >>> 0, inputSeq: (b[6] << 8) | b[7] };
  }
  // Unwrap a 32-bit ms value to the full value nearest to `ref` (ms).
  function unwrap(t32, ref) {
    var base = ref - (ref % TWO32), v = base + t32;
    if (v - ref > TWO32 / 2) v -= TWO32; else if (ref - v > TWO32 / 2) v += TWO32;
    return v;
  }
  function percentiles(xs, ps) {
    var a = xs.slice().sort(function (x, y) { return x - y; }), out = {};
    (ps || [50, 80, 95, 99]).forEach(function (p) {
      out['p' + p] = a.length ? a[Math.min(a.length - 1, Math.max(0, Math.ceil(p / 100 * a.length) - 1))] : null;
    });
    out.max = a.length ? a[a.length - 1] : null; out.n = a.length;
    return out;
  }
  return { CELLS: CELLS, cellFor: cellFor, crc8: crc8, pack: pack, draw: draw, decode: decode, unwrap: unwrap, percentiles: percentiles };
});
