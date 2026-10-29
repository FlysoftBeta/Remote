// Measure the viewer's animation clock independently of received video fps.
(function (root) {
  'use strict';
  function estimate(intervals) {
    var sorted = intervals.filter(function (dt) { return dt >= 4 && dt <= 100; }).sort(function (a, b) { return a - b; });
    if (sorted.length < 30) return null;
    // Missed callbacks are multiples of the display period. Use a fast
    // percentile so workload stalls do not masquerade as a monitor change.
    var hz = 1000 / sorted[Math.floor(sorted.length * 0.2)];
    var common = [24, 25, 30, 48, 50, 60, 72, 75, 90, 100, 120];
    for (var i = 0; i < common.length; i++) {
      if (Math.abs(hz - common[i]) < common[i] * 0.04) return common[i];
    }
    return Math.max(1, Math.min(120, Math.round(hz)));
  }
  function monitor(changed) {
    var last = null, intervals = [], target = 60, candidate = null, windows = 0;
    function tick(at) {
      if (document.hidden) { last = null; intervals = []; candidate = null; windows = 0; }
      else {
        if (last != null) intervals.push(at - last);
        last = at;
        if (intervals.length >= 60) {
          var next = estimate(intervals); intervals = [];
          if (next === candidate) windows++;
          else { candidate = next; windows = 1; }
          // Two windows prevent a brief main-thread stall changing capture.
          if (next && next !== target && windows >= 2) { target = next; changed(target); }
        }
      }
      requestAnimationFrame(tick);
    }
    requestAnimationFrame(tick);
    return { get: function () { return target; } };
  }
  // Congestion control owns bitrate. This slower loop bounds the work offered
  // to it and to the decoder; low source fps alone is never congestion evidence.
  function controller(send) {
    var display = 60, maximum = 60, budget = 120, target = 0;
    var bad = 0, healthy = 0, changedAt = -Infinity, reason = 'capabilities';
    function apply(why) {
      var next = Math.max(1, Math.min(display, maximum, budget));
      if (next !== target) { target = next; reason = why; send(next, why); }
    }
    function capabilities(hz, max) {
      if (Number.isFinite(hz) && hz >= 1 && hz <= 120) display = hz;
      if (Number.isFinite(max) && max >= 1 && max <= 120) maximum = max;
      apply('capabilities');
    }
    function sample(v, now) {
      if (v.hidden) { bad = healthy = 0; return; }
      var active = v.decoded > 0 || v.lost > 0 || v.sourceFps > 5;
      if (!active) { bad = healthy = 0; return; }
      var decode = v.decoded >= 10 && v.decMs * target > 700;
      var encode = v.senderFps > 5 && v.encMs * target > 750;
      var loss = v.lost >= 5 && v.lossRatio > 0.05;
      var queue = v.decoded >= 10 && v.jbMs > 120;
      var drops = v.decoded >= 10 && v.dropped > Math.max(2, v.decoded * 0.1);
      // Fresh raw frames distinguish sender frame-dropping (CPU/GCC) from
      // an intentionally static or low-frame-rate source.
      var sender = v.sourceFps >= target * 0.8 && v.senderFps < v.sourceFps * 0.8;
      var pressure = decode || encode || loss || queue || drops || sender;
      bad = pressure ? bad + 1 : 0;
      healthy = pressure ? 0 : healthy + 1;
      var ceiling = Math.min(display, maximum), levels = [10, 15, 20, 30, 60, 120];
      if (bad >= 3 && now - changedAt >= 5000 && target > Math.min(10, ceiling)) {
        var lower = levels.filter(function (f) { return f < target; });
        budget = lower.length ? lower[lower.length - 1] : Math.max(1, Math.floor(target / 2));
        changedAt = now; bad = healthy = 0;
        apply(decode ? 'decode' : encode ? 'encode' : loss ? 'loss' : queue ? 'queue' : drops ? 'drops' : 'sender');
      } else if (healthy >= 10 && now - changedAt >= 10000 && target < ceiling) {
        budget = Math.min(ceiling, levels.filter(function (f) { return f > target; })[0] || ceiling);
        changedAt = now; bad = healthy = 0; apply('probe');
      }
    }
    function reset() { budget = 120; bad = healthy = 0; target = 0; changedAt = -Infinity; apply('capabilities'); }
    return { capabilities: capabilities, sample: sample, reset: reset,
      get: function () { return { displayFps: display, maxFps: maximum, targetFps: target, reason: reason }; } };
  }
  root.RKFrameRate = { estimate: estimate, monitor: monitor, controller: controller };
})(typeof window === 'undefined' ? globalThis : window);
