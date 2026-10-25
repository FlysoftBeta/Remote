// Viewer (D13). Must run on Chrome 89+: no newer APIs without feature detection. M0: signalling hello only.
(function () {
  var log = document.getElementById('log');
  var ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/signal');
  ws.onmessage = function (e) { log.textContent += e.data + '\n'; };
  ws.onclose = function () { log.textContent += 'signalling closed\n'; };
})();
