// Glass-to-glass measurement on the viewer (design/03 section 5, 06 section 4.2).
// Decodes the pixel marker from every displayed frame (requestVideoFrameCallback), maps the sender timestamp
// into the viewer clock with RKClock, and records capture->display and input->photon samples.
// Needs marker.js (RKMarker) and clock.js (RKClock).
(function (root) {
  'use strict';
  function RKMeasure(video, clock) {
    this.video = video; this.clock = clock; this.senderWidth = null; // Unknown sender width defaults to videoWidth.
    this.lat = []; this.rx = []; this.i2p = []; this.rows = []; this.decoded = 0; this.failed = 0; this.frames = 0;
    this.inputSent = {}; this.lastInputSeq = -1; this.lastFrameId = -1; this.dupes = 0; this.skipped = 0;
    this.canvas = document.createElement('canvas');
    // Chrome 89 ignores this newer dictionary hint; ordinary 2D readback remains the fallback.
    this.ctx = this.canvas.getContext('2d', { willReadFrequently: true });
    this.precise = 'requestVideoFrameCallback' in HTMLVideoElement.prototype;
    var self = this;
    if (this.precise) {
      var cb = function (now, meta) { self.onFrame(meta.expectedDisplayTime || now, meta); video.requestVideoFrameCallback(cb); };
      video.requestVideoFrameCallback(cb);
    } else {
      var raf = function (now) { self.onFrame(now, null); requestAnimationFrame(raf); };
      requestAnimationFrame(raf);
    }
  }
  // Called by the viewer for every input message it sends.
  RKMeasure.prototype.expireInputs = function (now) {
    var pending = this.inputSent;
    Object.keys(pending).forEach(function (seq) { if (now - pending[seq] > 5000) delete pending[seq]; });
  };
  RKMeasure.prototype.noteInput = function (seq) {
    var now = RKClock.wall(); this.expireInputs(now); this.inputSent[seq & 0xffff] = now;
  };
  RKMeasure.prototype.onFrame = function (displayPerf, meta) {
    this.expireInputs(RKClock.wall());
    var v = this.video, w = v.videoWidth;
    if (!w || this.clock.offset == null) return;
    this.frames++;
    var senderWidth = this.senderWidth == null ? w : this.senderWidth;
    if (!Number.isFinite(senderWidth) || senderWidth <= 0) return;
    var cell = RKMarker.cellFor(senderWidth) * w / senderWidth, cw = Math.ceil(RKMarker.CELLS * cell) + 2, ch = Math.ceil(cell) + 1;
    if (cw > w || ch > v.videoHeight) { this.failed++; return; }
    if (this.canvas.width !== cw || this.canvas.height !== ch) { this.canvas.width = cw; this.canvas.height = ch; }
    this.ctx.drawImage(v, 0, 0, cw, ch, 0, 0, cw, ch);
    var m = RKMarker.decode(this.ctx.getImageData(0, 0, cw, ch).data, cw, cell);
    if (!m) { this.failed++; return; }
    this.decoded++;
    if (m.frameId === this.lastFrameId) { this.dupes++; return; }
    if (this.lastFrameId >= 0) { var gap = (m.frameId - this.lastFrameId) & 0xffff; if (gap > 1 && gap < 1000) this.skipped += gap - 1; }
    this.lastFrameId = m.frameId;
    var display = performance.timeOrigin + displayPerf;
    var sentRemote = RKMarker.unwrap(m.t, display + this.clock.offset);
    var lat = display - this.clock.toLocal(sentRemote);
    this.lat.push(lat);
    // rVFC receiveTime (WebRTC sources): sender draw -> last packet received, i.e. without decode/render/display.
    if (meta && meta.receiveTime) this.rx.push(performance.timeOrigin + meta.receiveTime - this.clock.toLocal(sentRemote));
    var row = { fid: m.frameId, lat: +lat.toFixed(2), rtt: this.clock.minRtt != null ? +this.clock.minRtt.toFixed(2) : '' , i2p: '' };
    var self = this;
    Object.keys(this.inputSent).forEach(function (seq) {
      // Half-range comparison orders 16-bit seqs across wrap; pending entries expire after five seconds.
      if (((m.inputSeq - (+seq)) & 0xffff) >= 0x8000) return;
      var sample = display - self.inputSent[seq];
      self.i2p.push(sample); row.i2p = +sample.toFixed(2); delete self.inputSent[seq];
    });
    this.lastInputSeq = m.inputSeq;
    if (this.rows.length < 100000) this.rows.push(row);
    if (this.lat.length > 20000) this.lat.shift();
  };
  RKMeasure.prototype.summary = function () {
    return {
      precise: this.precise, frames: this.frames, decoded: this.decoded, failed: this.failed, dupes: this.dupes, skipped: this.skipped,
      offset: this.clock.offset, minRtt: this.clock.minRtt,
      lat: RKMarker.percentiles(this.lat, [50, 80, 95, 99]), rx: RKMarker.percentiles(this.rx, [50, 80, 95, 99]), i2p: RKMarker.percentiles(this.i2p, [50, 80, 95, 99])
    };
  };
  RKMeasure.prototype.reset = function () {
    this.lat = []; this.rx = []; this.i2p = []; this.rows = []; this.inputSent = {};
    this.lastInputSeq = this.lastFrameId = -1;
    this.decoded = this.failed = this.frames = this.dupes = this.skipped = 0;
  };
  RKMeasure.prototype.csv = function () {
    return 'fid,lat_ms,min_rtt_ms,i2p_ms\n' + this.rows.map(function (r) { return [r.fid, r.lat, r.rtt, r.i2p].join(','); }).join('\n') + '\n';
  };
  root.RKMeasure = RKMeasure;
})(typeof self !== 'undefined' ? self : this);
