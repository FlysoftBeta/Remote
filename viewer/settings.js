(function () {
  'use strict';
  var form = document.getElementById('settings-form'), screen = document.getElementById('settings');
  var button = document.getElementById('disconnect'), error = document.getElementById('error');
  var params = new URLSearchParams(location.search), saved = {};
  try { saved = JSON.parse(localStorage.getItem('remotekit.viewer.settings') || '{}') || {}; } catch (e) {}
  Array.prototype.forEach.call(form.elements, function (el) {
    if (!el.name) return;
    if (el.name === 'fullscreen') { if (saved.fullscreen != null) el.checked = saved.fullscreen; }
    else el.value = params.has(el.name) ? params.get(el.name) : (saved[el.name] || '');
  });
  var instance = form.elements.instance, picker = document.getElementById('instance-picker');
  var pickerLabel = document.getElementById('instance-picker-label'), initialInstance = instance.value;
  var api = '/api/instances' + (form.elements.token.value ? '?token=' + encodeURIComponent(form.elements.token.value) : '');
  fetch(api).then(function (response) { if (!response.ok) throw new Error('Instances unavailable'); return response.json(); }).then(function (rows) {
    if (!Array.isArray(rows) || !rows.length || screen.hidden || instance.value !== initialInstance) return;
    var running = rows.filter(function (row) { return row && typeof row.id === 'string' && row.state === 'running'; });
    var selected = instance.value || (running.filter(function (row) { return row.id === 'default'; })[0] || running[0] || rows[0] || {}).id;
    if (!selected) return;
    var found = false;
    rows.forEach(function (row) {
      if (!row || typeof row.id !== 'string' || !row.id) return;
      var option = document.createElement('option'); option.value = row.id;
      option.textContent = (row.name || row.id) + ' (' + row.state + ')';
      if (row.id === selected) found = true;
      picker.appendChild(option);
    });
    if (!found) { var option = document.createElement('option'); option.value = selected; option.textContent = selected + ' (unlisted)'; picker.appendChild(option); }
    picker.value = selected; instance.value = selected; pickerLabel.hidden = false;
  }).catch(function () {});
  picker.addEventListener('change', function () { instance.value = picker.value; });
  instance.addEventListener('input', function () { pickerLabel.hidden = true; });
  try { var message = sessionStorage.getItem('remotekit.viewer.error'); sessionStorage.removeItem('remotekit.viewer.error'); if (message) { error.textContent = message; error.hidden = false; } } catch (e) {}
  function finish(message) {
    if (window.rk && window.rk.disconnect) window.rk.disconnect();
    try { if (message) sessionStorage.setItem('remotekit.viewer.error', message); } catch (e) {}
    location.reload();
  }
  window.viewerFinish = finish;
  button.addEventListener('click', function (e) { e.stopPropagation(); finish(''); });
  form.addEventListener('submit', function (e) {
    e.preventDefault();
    var values = {};
    Array.prototype.forEach.call(form.elements, function (el) {
      if (!el.name) return;
      if (el.name === 'fullscreen') values.fullscreen = el.checked;
      else { values[el.name] = el.value.trim(); if (values[el.name]) params.set(el.name, values[el.name]); else params.delete(el.name); }
    });
    try { localStorage.setItem('remotekit.viewer.settings', JSON.stringify(values)); } catch (err) {}
    history.replaceState(null, '', location.pathname + (params.toString() ? '?' + params.toString() : '') + location.hash);
    if (values.fullscreen && document.documentElement.requestFullscreen) {
      try { var request = document.documentElement.requestFullscreen(); if (request && request.then) request.then(function () { if (window.screen.orientation && window.screen.orientation.lock) window.screen.orientation.lock('landscape').catch(function () {}); }).catch(function () {}); } catch (err) {}
    }
    screen.hidden = true; button.hidden = false;
    var scripts = ['presenter.js', 'protocol.js', '/tools/latency/marker.js', '/tools/latency/clock.js', '/tools/latency/measure.js', 'input.js', 'ime.js', '/tools/field-test/viewer-field.js', 'viewer.js'];
    function next() {
      if (!scripts.length) return;
      var script = document.createElement('script'); script.src = scripts.shift(); script.onload = next;
      script.onerror = function () { finish('Unable to load ' + script.src); };
      document.body.appendChild(script);
    }
    next();
  });
})();
