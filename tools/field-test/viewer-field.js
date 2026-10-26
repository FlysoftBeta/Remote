// Optional no-CDP field UI. Plain script, Chrome 89 baseline; loaded before viewer.js.
(function () {
  'use strict';
  var q = new URLSearchParams(location.search);
  if (q.get('field') !== '1' && !(+q.get('measure') > 1)) return;
  var secs = q.get('field') === '1' ? +(q.get('report') || 60) : +q.get('measure');
  if (!Number.isFinite(secs) || secs < 5 || secs > 3600) secs = 60;
  q.set('field', '1'); q.set('measure', '1'); q.set('report', String(secs));
  q.set('i2p', 'colour'); q.set('autoinput', '0');
  if (!q.has('delay')) q.set('delay', '0');
  if (!q.has('stats')) q.set('stats', '0');
  history.replaceState(null, '', location.pathname + '?' + q.toString() + location.hash);

  var panel = document.createElement('div');
  panel.style.cssText = 'position:fixed;right:6px;top:6px;z-index:10;max-width:95vw;padding:10px;background:rgba(0,0,0,.85);color:white;font:12px monospace;white-space:pre-wrap';
  var text = document.createElement('div'), button = document.createElement('button');
  button.textContent = 'Start ' + secs + ' s test'; button.disabled = true;
  button.style.cssText = 'margin-top:8px;padding:10px;font:16px sans-serif';
  var download = document.createElement('a');
  download.textContent = 'Download report'; download.style.cssText = 'display:none;color:#8df;margin:8px';
  panel.appendChild(text); panel.appendChild(button); panel.appendChild(download); document.body.appendChild(panel);
  var config = null, configError = null, savedBody = null, finalSummary = null, endWall = null;
  var message = 'Checking server configuration...', uploadBusy = false;
  var field = window.RKField = { ready: false, running: false };
  fetch('/field-config', { cache: 'no-store' }).then(function (r) {
    if (!r.ok) throw new Error('Start the server with tools/field-test/start-server.ps1');
    return r.json();
  }).then(function (c) {
    config = c;
    if (q.get('scenario') && q.get('scenario') !== c.scenario) {
      configError = 'Scenario mismatch. Server runs ' + c.scenario + '; use the printed URL or restart with -Scenario.';
    }
    message = 'Tap Start when video is moving. Keep this tab visible.';
  }).catch(function (e) { configError = e.message; });
  function summaryLine(label, metric) {
    function f(v) { return v == null ? '-' : v.toFixed(1); }
    metric = metric || {};
    return label + ': ' + f(metric.p50) + ' / ' + f(metric.p80) + ' / ' + f(metric.p99) + '  n=' + (metric.n || 0);
  }
  function show() {
    var rk = window.rk, m = rk && rk.measure;
    var s = finalSummary || (m && m.summary());
    var lines = ['Field test: ' + (config ? config.scenario : '?') + '  ' + secs + ' s', message];
    if (endWall && field.running) lines.push('Remaining: ' + Math.max(0, Math.ceil((endWall - Date.now()) / 1000)) + ' s');
    if (s) {
      lines.push('ms: P50 / P80 / P99', summaryLine('capture->decoded', s.latDecoded), summaryLine('input->decoded', s.i2decoded),
        summaryLine('capture->display*', s.lat), summaryLine('input->display*', s.i2display), '* glass estimate (expectedDisplayTime)',
        'Marker OK ' + s.decoded + '/' + s.frames + '; clock min RTT ' + (s.minRtt == null ? '-' : s.minRtt.toFixed(1)) + ' ms');
      if (!s.precise) lines.push('rVFC unavailable: decoded metric unavailable. Display uses rAF.');
    }
    if (configError) lines.push(configError);
    text.textContent = lines.join('\n');
    if (!savedBody) button.disabled = !!configError || !config || field.ready || !rk || !rk.input.enabled || !m ||
      !rk.clock || rk.clock.offset == null || m.colorState == null || document.hidden;
  }
  button.addEventListener('click', function () {
    if (savedBody) { upload(); return; }
    var play = document.getElementById('video').play();
    if (play && play.catch) play.catch(function () {});
    field.ready = true; button.disabled = true; message = 'Warming up...'; show();
  });
  field.begin = function (warm) {
    field.running = true; endWall = Date.now() + (warm + secs) * 1000;
    message = 'Measuring after ' + warm + ' s warm-up; automatic key-to-colour probes every 500 ms.';
  };
  field.disconnect = function () {
    if (field.running) message = 'Disconnected: run discarded. Reconnect, then tap Start again.';
    field.ready = false; field.running = false; endWall = null;
  };
  document.addEventListener('visibilitychange', function () {
    if (document.hidden && field.running) {
      configError = 'Tab was hidden during measurement. Reload and keep this tab visible for the entire test.';
      field.ready = false; field.running = false;
    }
  });
  field.snapshot = function (measure) {
    return { summary: measure.summary(), csv: measure.csv(), trace: measure.trace ? measure.trace.slice() : null };
  };
  field.submit = function (body) {
    field.running = false; finalSummary = body.summary;
    // Stop automatic input after the report boundary; reload to repeat a test.
    if (window.rk) window.rk.input.enabled = false;
    body.kind = 'field-latency'; body.preset = 'm3-combined'; body.netem = null;
    body.field = { scenario: config.scenario, navigator: { userAgent: navigator.userAgent, platform: navigator.platform,
      hardwareConcurrency: navigator.hardwareConcurrency || null },
      screen: { width: screen.width, height: screen.height, availWidth: screen.availWidth, availHeight: screen.availHeight,
        devicePixelRatio: devicePixelRatio, viewportWidth: innerWidth, viewportHeight: innerHeight },
      connection: null, decoderImplementation: body.stats && body.stats.decoder || null,
      note: 'No CDP. Decoder name may be permission-gated. Capture metric starts at source marker draw, as in run.mjs; clock assumes symmetric minimum-RTT path.' };
    var connection = navigator.connection || navigator.mozConnection || navigator.webkitConnection;
    if (connection) body.field.connection = { type: connection.type || null, effectiveType: connection.effectiveType || null,
      downlink: connection.downlink, rtt: connection.rtt, saveData: connection.saveData };
    body.pipeline.serverRequested = config;
    body.pipeline.render = { actual: null, note: 'No CDP; requested settings are in serverRun/run.json' };
    savedBody = JSON.stringify(body, null, 2);
    download.href = URL.createObjectURL(new Blob([savedBody], { type: 'application/json' }));
    download.download = 'field-' + new Date().toISOString().replace(/[:.]/g, '-') + '.json';
    download.style.display = 'inline-block';
    upload();
  };
  field.error = function (e) { message = 'Report failed: ' + e.message + '. Reload to repeat.'; field.running = false; show(); };
  function upload() {
    if (uploadBusy) return;
    uploadBusy = true; button.disabled = true; message = 'Saving report...'; show();
    fetch('/field-report', { method: 'POST', headers: { 'content-type': 'application/json' }, body: savedBody }).then(function (r) {
      if (!r.ok) throw new Error('HTTP ' + r.status);
      return r.json();
    }).then(function (r) {
      message = 'Saved: ' + r.file + '\nReload the printed URL to repeat.';
      if (!finalSummary.latDecoded.n || !finalSummary.i2decoded.n)
        message += '\nINCOMPLETE: decoded samples missing; no acceptance verdict.';
      button.style.display = 'none';
    }).catch(function (e) {
      message = 'Upload failed: ' + e.message + '. Retry or download the report.';
      button.textContent = 'Retry upload'; button.disabled = false;
    }).then(function () { uploadBusy = false; show(); });
  }
  setInterval(show, 500); show();
})();
