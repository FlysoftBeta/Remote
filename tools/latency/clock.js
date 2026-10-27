// Clock-offset estimation over the `probe` DataChannel (docs/protocol.md section 4.1).
// Viewer side: new RKClock(channel) ; endpoint side: RKClock.answer(channel, msg).
// offset = endpointClock - viewerClock, taken from the minimum-RTT sample of the last 32.
(function (root) {
  'use strict';
  function wall() { return performance.timeOrigin + performance.now(); }
  function RKClock(ch) {
    this.ch = ch; this.samples = []; this.offset = null; this.rtt = null; this.next = 1; this.sent = 0;
    var self = this, start = performance.now();
    function tick() {
      if (self.closed) return;
      if (ch.readyState === 'open') { ch.send(JSON.stringify({ t: 'ping', id: self.next++, t0: wall() })); self.sent++; }
      setTimeout(tick, performance.now() - start < 2000 ? 50 : 250);
    }
    tick();
  }
  RKClock.prototype.onMessage = function (m) {
    if (m.t !== 'pong') return false;
    var t3 = wall(), rtt = (t3 - m.t0) - (m.t2 - m.t1);
    if (rtt < 0 || rtt > 5000) return true;
    this.samples.push({ rtt: rtt, off: ((m.t1 - m.t0) + (m.t2 - t3)) / 2 });
    if (this.samples.length > 32) this.samples.shift();
    var best = this.samples[0];
    for (var i = 1; i < this.samples.length; i++) if (this.samples[i].rtt < best.rtt) best = this.samples[i];
    this.offset = best.off; this.rtt = rtt; this.minRtt = best.rtt;
    return true;
  };
  // Convert an endpoint-clock time to viewer clock.
  RKClock.prototype.toLocal = function (tRemote) { return tRemote - (this.offset || 0); };
  RKClock.prototype.close = function () { this.closed = true; };
  RKClock.answer = function (ch, m) {
    if (m.t !== 'ping') return false;
    var t1 = wall();
    if (ch.readyState === 'open') ch.send(JSON.stringify({ t: 'pong', id: m.id, t0: m.t0, t1: t1, t2: wall() }));
    return true;
  };
  RKClock.wall = wall;
  root.RKClock = RKClock;
})(typeof self !== 'undefined' ? self : this);
