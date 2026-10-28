// In-session chrome: auto-hiding toolbar, connection status chip, stats toggle. Chrome 79+.
// Never takes keyboard focus from the stage on pointer use, so remote input keeps flowing.
(function () {
  'use strict';
  var chrome = document.getElementById('chrome'), handle = document.getElementById('toolbar-handle');
  var toolbar = document.getElementById('toolbar'), statusEl = document.getElementById('status');
  var tbStatus = document.getElementById('tb-status'), tbInstance = document.getElementById('tb-instance');
  var statsBtn = document.getElementById('tb-stats'), fsBtn = document.getElementById('tb-fullscreen');
  var kbdBtn = document.getElementById('tb-keyboard'), toast = document.getElementById('toast');
  var stage = document.getElementById('stage'), video = document.getElementById('video');
  var hideTimer = null, toastTimer = null, current = 'idle', started = false;
  var isTouch = (navigator.maxTouchPoints || 0) > 0 && matchMedia('(pointer: coarse)').matches;
  var fsSupported = !!(document.documentElement.requestFullscreen && document.exitFullscreen);

  function open(ms) {
    chrome.classList.add('open'); handle.setAttribute('aria-expanded', 'true');
    clearTimeout(hideTimer); hideTimer = setTimeout(close, ms || 3000);
  }
  function close() {
    clearTimeout(hideTimer);
    if (toolbar.contains(document.activeElement)) return; // keyboard users keep it until they leave
    chrome.classList.remove('open'); handle.setAttribute('aria-expanded', 'false');
  }
  function refocusStage() { if (document.activeElement && toolbar.contains(document.activeElement)) stage.focus({ preventScroll: true }); }
  function say(text) {
    toast.textContent = text; toast.hidden = false;
    clearTimeout(toastTimer); toastTimer = setTimeout(function () { toast.hidden = true; }, 2500);
  }

  // Mouse/pen presses on the chrome must not move focus off the stage (input.js forwards window key events).
  Array.prototype.forEach.call(chrome.querySelectorAll('button'), function (b) {
    b.addEventListener('pointerdown', function (e) { e.preventDefault(); e.stopPropagation(); });
  });
  handle.addEventListener('click', function () { open(4000); });
  toolbar.addEventListener('pointerenter', function () { clearTimeout(hideTimer); });
  toolbar.addEventListener('pointerleave', function (e) { if (e.pointerType === 'mouse') { clearTimeout(hideTimer); hideTimer = setTimeout(close, 1200); } });
  toolbar.addEventListener('focusout', function () { setTimeout(function () { if (!toolbar.contains(document.activeElement)) { hideTimer = setTimeout(close, 1500); } }, 0); });
  // Reveal when a mouse rests near the top edge; passive observer only, input.js still gets every event.
  document.addEventListener('pointermove', function (e) {
    if (!started || e.pointerType !== 'mouse' || e.buttons) return;
    if (e.clientY < 28 && Math.abs(e.clientX - innerWidth / 2) < 260) open(2500);
  }, { passive: true });
  stage.addEventListener('pointerdown', function () { if (chrome.classList.contains('open')) close(); }, { passive: true });

  function setStatsPressed(on) { statsBtn.setAttribute('aria-pressed', on ? 'true' : 'false'); }
  statsBtn.addEventListener('click', function () {
    var rk = window.rk;
    if (rk && rk.setStats) setStatsPressed(rk.setStats(!rk.statsVisible));
    open(); refocusStage();
  });

  if (!fsSupported) fsBtn.hidden = true;
  function fsChanged() { fsBtn.setAttribute('aria-pressed', document.fullscreenElement ? 'true' : 'false'); fsBtn.title = document.fullscreenElement ? 'Exit fullscreen' : 'Fullscreen'; }
  document.addEventListener('fullscreenchange', fsChanged);
  fsBtn.addEventListener('click', function () {
    try {
      var p = document.fullscreenElement ? document.exitFullscreen() : document.documentElement.requestFullscreen();
      if (p && p.catch) p.catch(function () { say('Fullscreen is not available here'); });
    } catch (e) { say('Fullscreen is not available here'); }
    refocusStage();
  });

  // Touch only: re-open the soft keyboard for a focused remote text field (same path as the ime.js button).
  if (isTouch) kbdBtn.hidden = false;
  kbdBtn.addEventListener('click', function () {
    var ime = window.rk && window.rk.ime;
    if (!ime || !ime.state || !ime.state.focused) { say('Tap a text field in the remote page first'); return; }
    var b = document.getElementById('kbd');
    try { b.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, cancelable: true, pointerType: 'touch' })); } catch (e) { /* old browser */ }
    close();
  });

  var LABEL = { connecting: 'Connecting…', waiting: 'Waiting for the remote browser…', negotiating: 'Starting video…',
    reconnecting: 'Connection lost — reconnecting…', action: 'Tap to start video' };
  function render() {
    var playing = video.readyState >= 2 && !video.paused;
    var showChip = current !== 'connected' || !playing;
    var text = current === 'connected' ? LABEL.negotiating : (statusEl.dataset.text || LABEL[current] || '');
    statusEl.className = current === 'action' ? 'action' : current === 'error' ? 'error' : '';
    statusEl.textContent = text;
    statusEl.style.display = started && showChip && text ? '' : 'none';
    tbStatus.className = 'tb-status ' + (current === 'connected' && playing ? 'connected' : current === 'error' ? 'error' : 'busy');
    tbStatus.title = current === 'connected' && playing ? 'Connected' : text;
  }
  statusEl.addEventListener('click', function () { if (current === 'action') { var p = video.play(); if (p && p.catch) p.catch(function () {}); } });
  ['playing', 'pause', 'emptied', 'loadeddata'].forEach(function (t) { video.addEventListener(t, render); });

  window.viewerUi = {
    start: function (instance) {
      started = true; chrome.hidden = false; tbInstance.textContent = instance || 'default';
      this.state('connecting'); open(3500);
    },
    // kind: connecting | waiting | negotiating | connected | reconnecting | action | error; text overrides the default label.
    state: function (kind, text) {
      current = kind; if (text) statusEl.dataset.text = text; else delete statusEl.dataset.text;
      render();
    },
    statsChanged: setStatsPressed
  };
})();
