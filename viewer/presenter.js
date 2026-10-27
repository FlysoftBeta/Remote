// Opt-in presentation experiments. Defaults and input coordinates stay unchanged.
(function () {
  'use strict';
  var q = new URLSearchParams(location.search), mode = q.get('presenter');
  if (mode !== 'clean' && mode !== 'canvas') return;
  var video = document.getElementById('video');
  var info = { mode: mode, draws: 0, desynchronized: null };
  window.RKPresenter = { info: info, draw: function () {}, source: null };
  function size() {
    var dpr = window.devicePixelRatio || 1;
    // The endpoint rounds physical dimensions to even pixels for H.264.
    video.style.width = Math.max(2, Math.floor(innerWidth * dpr / 2) * 2) / dpr + 'px';
    video.style.height = Math.max(2, Math.floor(innerHeight * dpr / 2) * 2) / dpr + 'px';
  }
  size(); window.addEventListener('resize', size);
  if (mode !== 'canvas') return;
  var canvas = document.createElement('canvas');
  canvas.id = 'presenter-canvas';
  canvas.style.cssText = 'position:absolute;left:0;top:0;pointer-events:none;object-fit:contain;background:#000';
  document.getElementById('stage').appendChild(canvas);
  var ctx = canvas.getContext('2d', { desynchronized: true });
  var attrs = ctx && ctx.getContextAttributes ? ctx.getContextAttributes() : null;
  info.desynchronized = attrs ? attrs.desynchronized : null;
  info.contextAttributes = attrs;
  info.pixelSource = 'visible canvas after drawImage(video)';
  info.displayTiming = 'source-video rVFC estimate; canvas present/scanout unobserved';
  window.RKPresenter.source = canvas;
  window.RKPresenter.draw = function () {
    if (!ctx || !video.videoWidth) return;
    if (canvas.width !== video.videoWidth || canvas.height !== video.videoHeight) {
      canvas.width = video.videoWidth; canvas.height = video.videoHeight;
    }
    canvas.style.width = video.style.width; canvas.style.height = video.style.height;
    ctx.drawImage(video, 0, 0); info.draws++;
  };
  // Measurement draws in its own rVFC before reading either marker or colour.
  // Outside the harness, retain an independent presentation loop.
  if (!q.has('measure') && 'requestVideoFrameCallback' in video) {
    var cb = function () { window.RKPresenter.draw(); video.requestVideoFrameCallback(cb); };
    video.requestVideoFrameCallback(cb);
  }
})();
