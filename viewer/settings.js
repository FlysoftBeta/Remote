// Settings / connect screen. Every setting is a URL query parameter (docs/viewer.md); the form only edits them.
(function () {
  'use strict';
  var form = document.getElementById('settings-form'), screen = document.getElementById('settings');
  var button = document.getElementById('disconnect'), error = document.getElementById('error');
  var params = new URLSearchParams(location.search), saved = {};
  try { saved = JSON.parse(localStorage.getItem('remotekit.viewer.settings') || '{}') || {}; } catch (e) {}
  var els = form.elements;
  var TEXT = ['instance', 'token', 'cp', 'relay', 'scenario', 'report', 'warmup', 'my'];
  var FLAGS = ['stats', 'autoinput', 'field', 'hwinfo', 'trace'];
  function initial(name) { return params.has(name) ? params.get(name) : (saved[name] == null ? '' : String(saved[name])); }
  function radio(name, value) {
    Array.prototype.forEach.call(form.querySelectorAll('input[name="' + name + '"]'), function (r) { r.checked = r.value === value; });
  }
  function radioValue(name) { var r = form.querySelector('input[name="' + name + '"]:checked'); return r ? r.value : ''; }
  function $(id) { return document.getElementById(id); }

  // ---- load values into controls ----
  TEXT.forEach(function (name) { els[name].value = initial(name); });
  FLAGS.forEach(function (name) { var v = initial(name); els[name].checked = !!v && v !== '0'; });
  if (saved.fullscreen != null) els.fullscreen.checked = !!saved.fullscreen;
  var presenter = initial('presenter'); radio('presenter', presenter === 'clean' || presenter === 'canvas' ? presenter : '');
  var delay = initial('delay'), delayMs = $('f-delay-ms');
  if (delay === 'off') radio('delay-mode', 'off');
  else if (delay !== '' && isFinite(+delay) && +delay > 0) { radio('delay-mode', 'custom'); delayMs.value = +delay; }
  else radio('delay-mode', 'low');
  var measure = initial('measure'), measureS = $('f-measure-s');
  if (measure === '1') radio('measure-mode', 'marker');
  else if (+measure > 1) { radio('measure-mode', 'field'); measureS.value = +measure; }
  else radio('measure-mode', 'off');
  function syncModes() {
    $('delay-custom').hidden = radioValue('delay-mode') !== 'custom';
    $('measure-custom').hidden = radioValue('measure-mode') !== 'field';
  }
  form.addEventListener('change', function (e) {
    syncModes();
    if (e.target.name === 'delay-mode' && e.target.value === 'custom' && !delayMs.value) { delayMs.value = 50; delayMs.focus(); }
    if (e.target.name === 'measure-mode' && e.target.value === 'field' && !measureS.value) { measureS.value = 60; measureS.focus(); }
  });
  syncModes();
  // Open the optional sections when the URL or saved settings already use them.
  if (els.stats.checked || radioValue('presenter') || radioValue('delay-mode') !== 'low' || els.cp.value) $('display-panel').open = true;
  if (radioValue('measure-mode') !== 'off' || ['relay', 'scenario', 'report', 'warmup', 'my'].some(function (n) { return els[n].value; }) ||
    ['autoinput', 'field', 'hwinfo', 'trace'].some(function (n) { return els[n].checked; })) $('dev-panel').open = true;

  // Collect the URL value of each parameter; '' means "remove from the URL".
  function collect() {
    var v = {};
    TEXT.forEach(function (name) { v[name] = els[name].value.trim(); });
    FLAGS.forEach(function (name) { v[name] = els[name].checked ? '1' : ''; });
    v.presenter = radioValue('presenter');
    var dm = radioValue('delay-mode'), ms = Math.round(+delayMs.value);
    v.delay = dm === 'off' ? 'off' : dm === 'custom' && delayMs.value !== '' && ms >= 0 ? String(ms) : '';
    var mm = radioValue('measure-mode'), s = Math.round(+measureS.value);
    v.measure = mm === 'marker' ? '1' : mm === 'field' ? String(s >= 5 && s <= 3600 ? s : 60) : '';
    return v;
  }

  // ---- token reveal ----
  var reveal = $('token-reveal');
  reveal.addEventListener('click', function () {
    var show = els.token.type === 'password';
    els.token.type = show ? 'text' : 'password';
    reveal.setAttribute('aria-pressed', show ? 'true' : 'false');
    reveal.setAttribute('aria-label', show ? 'Hide token' : 'Show token'); reveal.title = reveal.getAttribute('aria-label');
  });

  // ---- instance picker (read-only GET /api/instances) ----
  var list = $('instance-list'), note = $('instance-note'), refresh = $('instances-refresh'), instance = els.instance;
  var rows = [], loading = 0;
  function apiBase() {
    var cp = els.cp.value.trim();
    return cp && cp !== location.host ? location.protocol + '//' + cp : '';
  }
  function updateTarget() { $('connect-target').textContent = instance.value.trim() || 'default'; }
  function mark() {
    var id = instance.value.trim() || 'default';
    Array.prototype.forEach.call(list.children, function (b) {
      var on = b.getAttribute('data-id') === id;
      b.setAttribute('aria-checked', on ? 'true' : 'false'); b.tabIndex = on ? 0 : -1;
    });
    if (list.children.length && !list.querySelector('[tabindex="0"]')) list.children[0].tabIndex = 0;
    updateTarget();
  }
  function row(item, unlisted) {
    var b = document.createElement('button'), r = document.createElement('span'), meta = document.createElement('span');
    var name = document.createElement('span'), sub = document.createElement('span');
    b.type = 'button'; b.className = 'instance'; b.setAttribute('role', 'radio'); b.setAttribute('data-id', item.id);
    r.className = 'radio'; meta.className = 'meta'; name.className = 'name'; sub.className = 'sub';
    name.textContent = unlisted ? item.id : (item.name || item.id);
    sub.textContent = unlisted ? 'Not in the server list' : item.id + (item.profile && item.profile !== item.id ? ' · profile ' + item.profile : '');
    meta.appendChild(name); meta.appendChild(sub); b.appendChild(r); b.appendChild(meta);
    if (item.viewerConnected) { var u = document.createElement('span'); u.className = 'in-use'; u.textContent = 'In use'; u.title = 'Another viewer is connected; connecting takes over'; b.appendChild(u); }
    if (!unlisted) { var badge = document.createElement('span'); badge.className = 'badge ' + String(item.state || '').replace(/[^a-z]/g, ''); badge.textContent = item.state || 'unknown'; b.appendChild(badge); }
    b.addEventListener('click', function () { instance.value = item.id; mark(); });
    return b;
  }
  function render() {
    list.textContent = '';
    var id = instance.value.trim() || 'default', found = false;
    rows.forEach(function (item) { if (item.id === id) found = true; list.appendChild(row(item)); });
    if (!found && instance.value.trim()) list.insertBefore(row({ id: id }, true), list.firstChild);
    list.hidden = !list.children.length; mark();
  }
  function load() {
    var seq = ++loading, headers = {}, token = els.token.value.trim();
    if (token) headers.Authorization = 'Bearer ' + token;
    refresh.classList.add('spinning'); note.textContent = rows.length ? '' : 'Loading instances…';
    fetch(apiBase() + '/api/instances', { headers: headers, cache: 'no-store' }).then(function (response) {
      if (response.status === 401) throw new Error(token ? 'The access token was not accepted. Check it to see instances.' : 'Enter the access token to see available instances.');
      if (!response.ok) throw new Error('The instance list is unavailable (HTTP ' + response.status + '). You can still type an ID.');
      return response.json();
    }).then(function (data) {
      if (seq !== loading) return;
      rows = (Array.isArray(data) ? data : []).filter(function (r) { return r && typeof r.id === 'string' && r.id; });
      if (!instance.value.trim() && rows.length) {
        var running = rows.filter(function (r) { return r.state === 'running'; });
        var pick = running.filter(function (r) { return r.id === 'default'; })[0] || running[0] || rows[0];
        if (pick.id !== 'default') instance.value = pick.id;
      }
      note.textContent = rows.length ? '' : 'No instances are configured on this server.';
      render();
    }).catch(function (e) {
      if (seq !== loading) return;
      rows = []; render();
      note.textContent = e instanceof TypeError ? 'Could not reach the control plane to list instances. You can still type an ID.' : e.message;
    }).then(function () { if (seq === loading) refresh.classList.remove('spinning'); });
  }
  list.addEventListener('keydown', function (e) {
    var items = Array.prototype.slice.call(list.children), i = items.indexOf(document.activeElement), d = 0;
    if (e.key === 'ArrowDown' || e.key === 'ArrowRight') d = 1; else if (e.key === 'ArrowUp' || e.key === 'ArrowLeft') d = -1; else return;
    e.preventDefault(); var next = items[(i + d + items.length) % items.length]; next.focus(); next.click();
  });
  instance.addEventListener('input', mark);
  instance.addEventListener('change', render);
  refresh.addEventListener('click', load);
  els.token.addEventListener('change', load);
  els.cp.addEventListener('change', load);
  updateTarget();
  load();

  // ---- errors from a previous session ----
  var retry = $('error-retry');
  try {
    var message = sessionStorage.getItem('remotekit.viewer.error'); sessionStorage.removeItem('remotekit.viewer.error');
    if (message) { $('error-text').textContent = message; error.hidden = false; }
  } catch (e) {}
  retry.addEventListener('click', function () { error.hidden = true; $('connect').click(); });
  $('error-dismiss').addEventListener('click', function () { error.hidden = true; });

  function finish(message) {
    if (window.rk && window.rk.disconnect) window.rk.disconnect();
    try { if (message) sessionStorage.setItem('remotekit.viewer.error', message); } catch (e) {}
    // Drop connect=1 so a disconnect or fatal error returns to the settings screen instead of looping.
    if (params.has('connect')) { params.delete('connect'); location.replace(location.pathname + (params.toString() ? '?' + params.toString() : '') + location.hash); }
    else location.reload();
  }
  window.viewerFinish = finish;
  button.addEventListener('click', function (e) { e.stopPropagation(); finish(''); });

  form.addEventListener('submit', function (e) {
    e.preventDefault();
    var values = collect();
    Object.keys(values).forEach(function (name) { if (values[name]) params.set(name, values[name]); else params.delete(name); });
    values.fullscreen = els.fullscreen.checked;
    try { localStorage.setItem('remotekit.viewer.settings', JSON.stringify(values)); } catch (err) {}
    history.replaceState(null, '', location.pathname + (params.toString() ? '?' + params.toString() : '') + location.hash);
    if (values.fullscreen && document.documentElement.requestFullscreen) {
      try { var request = document.documentElement.requestFullscreen(); if (request && request.then) request.then(function () { if (window.screen.orientation && window.screen.orientation.lock) window.screen.orientation.lock('landscape').catch(function () {}); }).catch(function () {}); } catch (err) {}
    }
    screen.hidden = true; loading++;
    if (window.viewerUi) window.viewerUi.start(values.instance);
    document.getElementById('stage').focus({ preventScroll: true });
    var scripts = ['presenter.js', 'protocol.js', '/tools/latency/marker.js', '/tools/latency/clock.js', '/tools/latency/measure.js', 'input.js', 'ime.js', '/tools/field-test/viewer-field.js', 'viewer.js'];
    function next() {
      if (!scripts.length) return;
      var script = document.createElement('script'); script.src = scripts.shift(); script.onload = next;
      script.onerror = function () { finish('Unable to load ' + script.src); };
      document.body.appendChild(script);
    }
    next();
  });
  // connect=1 (used by tools/smoke, tools/latency, tools/soak) skips the settings screen.
  if (params.get('connect') === '1') { if (form.requestSubmit) form.requestSubmit(); else form.dispatchEvent(new Event('submit', { cancelable: true })); }
})();
