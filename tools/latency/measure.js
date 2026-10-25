// Glass-to-glass measurement on the viewer (design/03 section 5, 06 section 4.2).
// Decodes the pixel marker from every displayed frame (requestVideoFrameCallback), maps the sender timestamp
// into the viewer clock with RKClock, and records capture->display and input->photon samples.
// Needs marker.js (RKMarker) and clock.js (RKClock).
(function (root) {
  'use strict';
  function RKMeasure(video, clock) {
    this.video = video; this.clock = clock;
    this.lat = []; this.i2p = []; this.rows = []; this.decoded = 0; this.failed = 0; this.frames = 0;
    this.inputSent = {}; this.lastInputSeq = -1; this.lastFrameId = -1; this.dupes = 0; this.skipped = 0;
    this.canvas = document.createElement('canvas');
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
  RKMeasure.prototype.noteInput = function (seq) { this.inputSent[seq & 0xffff] = RKClock.wall(); };
  RKMeasure.prototype.onFrame = function (displayPerf, meta) {
    var v = this.video, w = v.videoWidth;
    if (!w || this.clock.offset == null) return;
    this.frames++;
    var cell = RKMarker.cellFor(w), cw = Math.ceil(RKMarker.CELLS * cell) + 2, ch = Math.ceil(cell) + 1;
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
    var row = { fid: m.frameId, lat: +lat.toFixed(2), rtt: this.clock.minRtt != null ? +this.clock.minRtt.toFixed(2) : '' , i2p: '' };
    if (m.inputSeq !== this.lastInputSeq) {
      var sent = this.inputSent[m.inputSeq];
      if (sent != null) { row.i2p = +(display - sent).toFixed(2); this.i2p.push(display - sent); delete this.inputSent[m.inputSeq]; }
      this.lastInputSeq = m.inputSeq;
    }
    if (this.rows.length < 100000) this.rows.push(row);
    if (this.lat.length > 20000) this.lat.shift();
  };
  RKMeasure.prototype.summary = function () {
    return {
      precise: this.precise, frames: this.frames, decoded: this.decoded, failed: this.failed, dupes: this.dupes, skipped: this.skipped,
      offset: this.clock.offset, minRtt: this.clock.minRtt,
      lat: RKMarker.percentiles(this.lat, [50, 80, 95, 99]), i2p: RKMarker.percentiles(this.i2p, [50, 80, 95, 99])
    };
  };
  RKMeasure.prototype.reset = function () { this.lat = []; this.i2p = []; this.rows = []; this.decoded = this.failed = this.frames = this.dupes = this.skipped = 0; };
  RKMeasure.prototype.csv = function () {
    return 'fid,lat_ms,min_rtt_ms,i2p_ms\n' + this.rows.map(function (r) { return [r.fid, r.lat, r.rtt, r.i2p].join(','); }).join('\n') + '\n';
  };
  root.RKMeasure = RKMeasure;
})(typeof self !== 'undefined' ? self : this);
