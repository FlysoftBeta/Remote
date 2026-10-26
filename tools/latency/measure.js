// Glass-to-glass measurement on the viewer (design/03 section 5, 06 section 4.2).
// Decodes the pixel marker from every displayed frame (requestVideoFrameCallback), maps the sender timestamp
// into the viewer clock with RKClock, and records capture->display and input->photon samples.
// Needs marker.js (RKMarker) and clock.js (RKClock).
(function (root) {
  'use strict';
  function RKMeasure(video, clock) {
    this.video = video; this.clock = clock; this.senderWidth = null; // Unknown sender width defaults to videoWidth.
    this.lat = []; this.rx = []; this.i2p = []; this.rows = []; this.decoded = 0; this.failed = 0; this.frames = 0;
    this.stages = {};
    this.displayGaps = []; this.lastDisplayPerf = null; this.freezeCount = 0;
    this.measurementStartWall = Date.now();
    this.validateFrame = new URLSearchParams(location.search).get('validateFrame') === '1';
    this.inputSent = {}; this.lastInputSeq = -1; this.lastFrameId = -1; this.dupes = 0; this.skipped = 0;
    // my is a marker origin in decoded source pixels; absent/invalid means auto-detect.
    var my = new URLSearchParams(location.search).get('my');
    this.markerOverride = my != null && my.trim() !== '' && Number.isFinite(+my) && +my >= 0 ? Math.floor(+my) : null;
    this.markerY = this.markerOverride; this.markerFailures = 0; this.markerScans = 0;
    this.markerWidth = 0; this.markerHeight = 0; this.markerCell = 0;
    this.colorInput = new URLSearchParams(location.search).get('i2p') === 'colour';
    this.colorState = null; this.colorPending = null;
    this.colorAttempts = 0; this.colorTimeouts = 0; this.colorRows = [];
    if (this.colorInput) {
      this.colorCanvas = document.createElement('canvas');
      this.colorCanvas.width = this.colorCanvas.height = 1;
      this.colorCtx = this.colorCanvas.getContext('2d', { willReadFrequently: true });
    }
    this.canvas = document.createElement('canvas');
    // Chrome 89 ignores this newer dictionary hint; ordinary 2D readback remains the fallback.
    this.ctx = this.canvas.getContext('2d', { willReadFrequently: true });
    this.precise = 'requestVideoFrameCallback' in HTMLVideoElement.prototype;
    var self = this;
    if (this.colorInput) setInterval(function () {
      var input = root.rk && root.rk.input;
      if (!input || !input.enabled || self.colorState == null || self.colorPending != null) return;
      // sendKey uses the viewer's normal binary input DataChannel path. One
      // outstanding key avoids attributing multiple inputs to one decoded flip.
      self.colorPending = performance.now(); self.colorAttempts++;
      input.sendKey({ key: 'i', code: 'KeyI', keyCode: 73, location: 0 }, 0);
      input.sendKey({ key: 'i', code: 'KeyI', keyCode: 73, location: 0 }, 1);
    }, 500);
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
    if (this.colorInput) return;
    var now = RKClock.wall(); this.expireInputs(now); this.inputSent[seq & 0xffff] = now;
  };
  RKMeasure.prototype.onFrame = function (displayPerf, meta) {
    if (this.lastDisplayPerf != null) {
      var displayGap = displayPerf - this.lastDisplayPerf;
      if (displayGap > 200) this.freezeCount++;
      this.displayGaps.push(displayGap);
      if (this.displayGaps.length > 20000) this.displayGaps.shift();
    }
    this.lastDisplayPerf = displayPerf;
    var self = this;
    function stage(name, value) {
      if (!Number.isFinite(value)) return;
      var values = self.stages[name] || (self.stages[name] = []);
      values.push(value);
      if (values.length > 20000) values.shift();
    }
    if (meta) {
      // Optional harness diagnostic: verify that pixel sampling and rVFC refer
      // to the same media timestamp. VideoFrame wraps the current video frame.
      if (this.validateFrame && typeof VideoFrame !== 'undefined' && this.frames % 30 === 0) {
        var sampled = new VideoFrame(this.video);
        stage('sampledFrameMinusMetadata', sampled.timestamp / 1000 - meta.mediaTime * 1000);
        sampled.close();
      }
      if (meta.receiveTime != null && meta.presentationTime != null) stage('receiveToPresentation', meta.presentationTime - meta.receiveTime);
      if (meta.presentationTime != null) stage('presentationToDisplay', displayPerf - meta.presentationTime);
      if (meta.receiveTime != null) stage('receiveToDisplay', displayPerf - meta.receiveTime);
      if (meta.processingDuration != null) stage('processingDuration', meta.processingDuration * 1000);
    }
    this.expireInputs(RKClock.wall());
    var v = this.video, w = v.videoWidth;
    if (!w || this.clock.offset == null) return;
    if (this.colorInput) {
      // This point is inside the fixture's right-hand colour block in tab mode,
      // including its browser UI inset. Read decoded video, never the source DOM.
      this.colorCtx.drawImage(v, Math.floor(w * 0.75), Math.floor(v.videoHeight * 0.5), 1, 1, 0, 0, 1, 1);
      var pixel = this.colorCtx.getImageData(0, 0, 1, 1).data;
      var state = pixel[1] - pixel[0] > 60 ? 0 : pixel[0] - pixel[1] > 60 ? 1 : null;
      var detected = performance.now();
      if (state != null && this.colorState != null && state !== this.colorState && this.colorPending != null) {
        var elapsed = detected - this.colorPending;
        this.i2p.push(elapsed);
        this.colorRows.push({ sent: this.colorPending, detected: detected, latency: elapsed, state: state });
        this.colorPending = null;
      }
      if (state != null) this.colorState = state;
      if (this.colorPending != null && detected - this.colorPending > 5000) {
        this.colorTimeouts++; this.colorPending = null;
      }
    }
    this.frames++;
    var senderWidth = this.senderWidth == null ? w : this.senderWidth;
    if (!Number.isFinite(senderWidth) || senderWidth <= 0) return;
    var cell = RKMarker.cellFor(senderWidth) * w / senderWidth, cw = Math.ceil(RKMarker.CELLS * cell) + 2, ch = Math.ceil(cell) + 1;
    if (cw > w || ch > v.videoHeight) { this.failed++; return; }
    if (w !== this.markerWidth || v.videoHeight !== this.markerHeight || cell !== this.markerCell) {
      this.markerWidth = w; this.markerHeight = v.videoHeight; this.markerCell = cell;
      this.markerY = this.markerOverride; this.markerFailures = 0;
    }
    var m = null;
    if (this.markerY == null) {
      // Only search while unlocked. decode checks x < 2 cells for sync before reading data/CRC.
      var maxY = Math.floor(Math.min(200, v.videoHeight / 4, v.videoHeight - ch));
      var scanHeight = maxY + ch;
      if (this.canvas.width !== cw || this.canvas.height !== scanHeight) { this.canvas.width = cw; this.canvas.height = scanHeight; }
      this.ctx.drawImage(v, 0, 0, cw, scanHeight, 0, 0, cw, scanHeight);
      var found = RKMarker.findRow(this.ctx.getImageData(0, 0, cw, scanHeight).data, cw, cell, maxY);
      this.markerScans++;
      if (found) { this.markerY = found.y; m = found.marker; }
    } else if (this.markerY + ch <= v.videoHeight) {
      if (this.canvas.width !== cw || this.canvas.height !== ch) { this.canvas.width = cw; this.canvas.height = ch; }
      this.ctx.drawImage(v, 0, this.markerY, cw, ch, 0, 0, cw, ch);
      m = RKMarker.decode(this.ctx.getImageData(0, 0, cw, ch).data, cw, cell);
    }
    if (!m) {
      this.failed++;
      // Five consecutive invalid decodes drop the automatic lock; an explicit override stays fixed.
      if (++this.markerFailures >= 5 && this.markerOverride == null) this.markerY = null;
      return;
    }
    this.markerFailures = 0;
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
    var row = { fid: m.frameId, lat: +lat.toFixed(2), rtt: this.clock.minRtt != null ? +this.clock.minRtt.toFixed(2) : '' ,
      i2p: this.colorInput && elapsed != null ? +elapsed.toFixed(2) : '' };
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
    var stages = {}, self = this;
    Object.keys(this.stages).forEach(function (key) { stages[key] = RKMarker.percentiles(self.stages[key], [50, 80, 95, 99]); });
    return {
      stages: stages,
      measurementStartWall: this.measurementStartWall,
      freezes: { thresholdMs: 200, count: this.freezeCount,
        gaps: RKMarker.percentiles(this.displayGaps, [50, 80, 95, 99]) },
      precise: this.precise, frames: this.frames, decoded: this.decoded, failed: this.failed, dupes: this.dupes, skipped: this.skipped,
      markerY: this.markerY, markerScans: this.markerScans,
      inputMethod: this.colorInput ? 'DataChannel keydown to decoded colour flip' : 'marker sequence',
      inputAttempts: this.colorAttempts, inputTimeouts: this.colorTimeouts, inputSamples: this.colorRows,
      offset: this.clock.offset, minRtt: this.clock.minRtt,
      lat: RKMarker.percentiles(this.lat, [50, 80, 95, 99]), rx: RKMarker.percentiles(this.rx, [50, 80, 95, 99]), i2p: RKMarker.percentiles(this.i2p, [50, 80, 95, 99])
    };
  };
  RKMeasure.prototype.reset = function () {
    this.stages = {};
    this.displayGaps = []; this.lastDisplayPerf = null; this.freezeCount = 0;
    this.measurementStartWall = Date.now();
    this.lat = []; this.rx = []; this.i2p = []; this.rows = []; this.inputSent = {};
    this.lastInputSeq = this.lastFrameId = -1;
    this.decoded = this.failed = this.frames = this.dupes = this.skipped = 0;
    this.colorPending = null; this.colorAttempts = this.colorTimeouts = 0; this.colorRows = [];
  };
  RKMeasure.prototype.csv = function () {
    return 'fid,lat_ms,min_rtt_ms,i2p_ms\n' + this.rows.map(function (r) { return [r.fid, r.lat, r.rtt, r.i2p].join(','); }).join('\n') + '\n';
  };
  root.RKMeasure = RKMeasure;
})(typeof self !== 'undefined' ? self : this);
