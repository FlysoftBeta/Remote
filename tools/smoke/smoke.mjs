// Real CDP input -> viewer -> WebRTC -> mock endpoint; Node 24, no added packages.
// node tools/smoke/smoke.mjs [--port 8210] [--viewer-browser <exe>] [--old-headless]
import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repo = fileURLToPath(new URL('../../', import.meta.url));
const edge = 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
const options = { port: 8210, viewerBrowser: edge, oldHeadless: false };
const children = [], clients = [];
let passed = 0, failed = 0, cleaned = false;
const tail = [];
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const compact = value => JSON.stringify(value);
function check(name, ok, detail = '') {
  if (ok) passed++; else failed++;
  console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${detail ? ': ' + detail : ''}`);
  return ok;
}
function cleanup() {
  if (cleaned) return;
  cleaned = true;
  for (const client of clients) client.close();
  let ok = true;
  for (const child of children.toReversed()) {
    if (!child.pid || child.exitCode !== null || child.signalCode !== null) continue;
    const result = spawnSync('taskkill', ['/PID', String(child.pid), '/T', '/F'], { stdio: 'pipe', windowsHide: true });
    if (result.status !== 0) {
      // A child can exit between the check and taskkill. Verify before calling it a cleanup failure.
      try { process.kill(child.pid, 0); ok = false; } catch {}
    }
  }
  check('child process trees cleaned up', ok, 'taskkill /T /F');
}
process.on('exit', cleanup);
for (const [sig, code] of [['SIGINT', 130], ['SIGTERM', 143]]) process.on(sig, () => process.exit(code));
const watchdog = setTimeout(() => { check('overall deadline', false, '150 seconds'); process.exit(1); }, 150000);

async function poll(read, ready, timeout = 6000) {
  const end = Date.now() + timeout;
  let last, error;
  do {
    try { last = await read(); error = null; if (ready(last)) return last; } catch (e) { error = e; }
    await delay(50);
  } while (Date.now() < end);
  throw new Error(`timeout; last=${compact(last)}${error ? '; ' + error.message : ''}`);
}
async function freePort(port = 0) {
  const server = net.createServer();
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen({ host: '127.0.0.1', port, exclusive: true }, resolve); });
  const selected = server.address().port;
  await new Promise((resolve, reject) => server.close(e => e ? reject(e) : resolve()));
  return selected;
}
const getJSON = async url => {
  const response = await fetch(url, { signal: AbortSignal.timeout(1500) });
  if (!response.ok) throw new Error(`HTTP ${response.status} ${url}`);
  return response.json();
};
function child(exe, args, name, env = process.env) {
  const p = spawn(exe, args, { cwd: repo, env, stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true });
  p.on('error', error => { p.smokeError = error; tail.push(`${name}: ${error.message}`); });
  for (const stream of [p.stdout, p.stderr]) stream.on('data', data => {
    tail.push(`${name}: ${data.toString().trim()}`); if (tail.length > 40) tail.shift();
  });
  children.push(p);
  return p;
}
class CDP {
  constructor(socket) {
    this.socket = socket; this.next = 0; this.pending = new Map();
    socket.addEventListener('message', event => {
      const message = JSON.parse(event.data), pending = this.pending.get(message.id);
      if (!pending) return;
      this.pending.delete(message.id); clearTimeout(pending.timer);
      if (message.error) {
        const error = new Error(`${pending.method}: ${message.error.message}`);
        error.code = message.error.code; pending.reject(error);
      } else pending.resolve(message.result);
    });
    socket.addEventListener('close', () => this.rejectAll());
    socket.addEventListener('error', () => this.rejectAll());
  }
  rejectAll() {
    for (const pending of this.pending.values()) { clearTimeout(pending.timer); pending.reject(new Error('CDP disconnected')); }
    this.pending.clear();
  }
  call(method, params = {}) {
    return new Promise((resolve, reject) => {
      const id = ++this.next;
      const timer = setTimeout(() => { this.pending.delete(id); reject(new Error(`${method}: timeout`)); }, 8000);
      this.pending.set(id, { resolve, reject, timer, method });
      try { this.socket.send(JSON.stringify({ id, method, params })); }
      catch (e) { clearTimeout(timer); this.pending.delete(id); reject(e); }
    });
  }
  async evaluate(expression) {
    const result = await this.call('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
    if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
    return result.result.value;
  }
  close() { this.rejectAll(); this.socket.close(); }
}

// Passive receive-only observation: preserve all real channels and application handlers.
// mockState retains only lastKey (no key-up/code/modifier history), so capture decoded wire messages too.
const observer = `(() => {
  window.__smoke = { channels: [], input: [], ctl: [], trusted: 0, untrusted: [], hardwareTrusted: 0, hardwareUntrusted: 0 };
  const state = window.__smoke;
  const original = RTCPeerConnection.prototype.createDataChannel;
  RTCPeerConnection.prototype.createDataChannel = function () {
    const channel = original.apply(this, arguments);
    state.channels.push(channel);
    channel.addEventListener('message', function (event) {
      if (channel.label === 'input') state.input.push(window.RK.decodeInput(event.data));
      if (channel.label === 'ctl') { try { state.ctl.push(JSON.parse(event.data)); } catch (_) {} }
    });
    return channel;
  };
  ['pointerdown','pointermove','pointerup','pointercancel','wheel','keydown','keyup','beforeinput','compositionstart','compositionupdate','compositionend'].forEach(type => {
    window.addEventListener(type, event => {
      if (event.isTrusted) state.trusted++; else state.untrusted.push(type);
      if (/^(pointer|wheel|key)/.test(type)) state[event.isTrusted ? 'hardwareTrusted' : 'hardwareUntrusted']++;
    }, true);
  });
})()`;
const snapshot = `({state: window.mockState, input: window.__smoke.input, ctl: window.__smoke.ctl,
  channels: window.__smoke.channels.map(c => ({label:c.label,id:c.id,state:c.readyState}))})`;
async function browser(exe, name, url, old, tmp) {
  const dp = await freePort();
  const args = [old ? '--headless' : '--headless=new', `--remote-debugging-port=${dp}`, '--remote-debugging-address=127.0.0.1',
    `--user-data-dir=${path.join(tmp, name)}`, '--no-first-run', '--no-default-browser-check',
    '--autoplay-policy=no-user-gesture-required', '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
    '--disable-backgrounding-occluded-windows', '--disable-features=WebRtcHideLocalIpsWithMdns,CalculateNativeWinOcclusion',
    '--enable-logging=stderr', '--v=0', '--window-size=1280,900',
    ...(old ? ['--no-sandbox', '--enable-features=NetworkServiceInProcess'] : []), 'about:blank'];
  const p = child(exe, args, name);
  const target = await poll(async () => {
    if (p.smokeError) throw p.smokeError;
    const pages = await getJSON(`http://127.0.0.1:${dp}/json`);
    return pages.find(page => page.type === 'page' && page.url === 'about:blank');
  }, value => !!value, 20000);
  const socket = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => { socket.close(); reject(new Error('CDP connect timeout')); }, 6000);
    socket.addEventListener('open', () => { clearTimeout(timer); resolve(); }, { once: true });
    socket.addEventListener('error', () => { clearTimeout(timer); reject(new Error('CDP connect error')); }, { once: true });
  });
  const cdp = new CDP(socket); clients.push(cdp);
  await cdp.call('Page.enable');
  await cdp.call('Page.addScriptToEvaluateOnNewDocument', { source: observer });
  // A deliberately different aspect ratio exercises real letterboxing in both headless implementations.
  await cdp.call('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
  await cdp.call('Page.navigate', { url });
  return cdp;
}

async function main() {
  for (let i = 2; i < process.argv.length; i++) {
    const flag = process.argv[i];
    if (flag === '--old-headless') options.oldHeadless = true;
    else if (flag === '--port' || flag === '--viewer-browser') {
      const value = process.argv[++i];
      if (!value || value.startsWith('--')) throw new Error(`missing value for ${flag}`);
      if (flag === '--port') options.port = Number(value); else options.viewerBrowser = value;
    } else throw new Error(`unknown option ${flag}`);
  }
  if (!Number.isInteger(options.port) || options.port < 1 || options.port > 65535) throw new Error('port must be 1..65535');
  for (const exe of [edge, options.viewerBrowser]) if (!fs.existsSync(exe)) throw new Error(`browser missing: ${exe}`);
  // Keep every generated artifact within this task directory, including browser TEMP files.
  const taskTmp = 'C:/src/tmp/pt-smoke';
  fs.mkdirSync(taskTmp, { recursive: true });
  const runDir = fs.mkdtempSync(path.join(taskTmp, 'run-'));
  console.log(`RUN ${new Date().toISOString()} port=${options.port} viewer=${options.viewerBrowser} oldHeadless=${options.oldHeadless}`);
  console.log(`INFO profiles=${runDir}`);
  await freePort(options.port); // Refuse to reuse or kill another server.
  const base = `http://127.0.0.1:${options.port}`;
  const env = { ...process.env, CP_TOKEN: '', ENDPOINT_PATH: '', REPORT_FILE: '', TEMP: runDir, TMP: runDir };
  const cp = child(process.execPath, [path.join(repo, 'control-plane/index.js'), '--port', String(options.port)], 'control-plane', env);
  await poll(async () => {
    if (cp.smokeError || cp.exitCode !== null) throw cp.smokeError || new Error('control-plane exited');
    return getJSON(base + '/api/health');
  }, health => health?.ok, 10000);
  check('control-plane started', true);
  // Browser child() also inherits only task-local temporary directories.
  const oldTemp = process.env.TEMP, oldTmp = process.env.TMP;
  process.env.TEMP = runDir; process.env.TMP = runDir;
  let mock, viewer;
  try {
    mock = await browser(edge, 'mock', base + '/mock?w=1920&h=1080&fps=15', false, runDir);
    viewer = await browser(options.viewerBrowser, 'viewer', base + '/?stats=1', options.oldHeadless, runDir);
  } finally {
    if (oldTemp === undefined) delete process.env.TEMP; else process.env.TEMP = oldTemp;
    if (oldTmp === undefined) delete process.env.TMP; else process.env.TMP = oldTmp;
  }
  console.log('INFO mock=' + (await mock.call('Browser.getVersion')).product + ' viewer=' + (await viewer.call('Browser.getVersion')).product);
  const ready = await poll(() => viewer.evaluate(`({width:document.querySelector('video')?.videoWidth,
    enabled:!!window.rk?.input.enabled, channels:window.__smoke.channels.map(c=>({id:c.id,label:c.label,state:c.readyState}))})`),
  value => value.width === 1920 && value.enabled && value.channels.length === 3 && value.channels.every(c => c.state === 'open'), 30000);
  check('viewer video 1920 and all data channels open', true, compact(ready));
  const start = await poll(() => mock.evaluate(snapshot), value => value.channels.length === 3 && value.channels.every(c => c.state === 'open'));
  check('mock data channels open and handshake received', start.state.counts.ctl > 0, compact(start.channels));
  await viewer.call('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 2 });
  await viewer.call('Page.bringToFront');
  const rect = await viewer.evaluate(`(() => { const v=document.querySelector('video'),r=v.getBoundingClientRect();
    const s=Math.min(r.width/v.videoWidth,r.height/v.videoHeight),w=v.videoWidth*s,h=v.videoHeight*s;
    return {left:r.left+(r.width-w)/2,top:r.top+(r.height-h)/2,width:w,height:h,scale:s}; })()`);
  check('letterbox coordinate mapping exercised', rect.top > 0 || rect.left > 0, compact(rect));
  const point = (x, y) => ({ x: rect.left + x * rect.width, y: rect.top + y * rect.height });
  const near = (m, x, y) => m && Math.abs(m.x / 65535 - x) < 0.01 && Math.abs(m.y / 65535 - y) < 0.01;
  const stateNear = (p, x, y) => p && Math.abs(p.x - x) < 0.01 && Math.abs(p.y - y) < 0.01;
  let current = start;
  // Each step snapshots the mock before and after delivery, then validates real received packets and state.
  async function step(name, count, increment, drive, validate) {
    const before = await mock.evaluate(snapshot);
    await drive();
    try {
      current = await poll(() => mock.evaluate(snapshot), value => value.state.counts[count] >= before.state.counts[count] + increment);
    } catch (e) {
      current = await mock.evaluate(snapshot);
      check(name, false, e.message); return current;
    }
    const packets = current.input.slice(before.input.length), ctl = current.ctl.slice(before.ctl.length);
    const delta = current.state.counts[count] - before.state.counts[count];
    check(name, delta >= increment && validate(current.state, packets, ctl), `count +${delta}; seq=${current.state.inputSeq}`);
    return current;
  }
  const mouse = (type, x, y, extra = {}) => viewer.call('Input.dispatchMouseEvent', { type, ...point(x, y), ...extra });
  await step('mouse move coordinates', 'pointer', 1, () => mouse('mouseMoved', .2, .25), (s, p) => stateNear(s.mouse, .2, .25) && p.some(m => m.kind === 1 && m.action === 1 && m.pid === 0 && near(m, .2, .25)));
  for (const [button, mask, number, x] of [['left', 1, 0, .22], ['right', 2, 2, .24]]) {
    await step(`${button} mouse down`, 'pointer', 1, () => mouse('mousePressed', x, .28, { button, buttons: mask, clickCount: 1 }),
      (s, p) => stateNear(s.pointers[0], x, .28) && p.some(m => m.kind === 1 && m.action === 0 && m.pid === 0 && m.button === number && m.buttons === mask && near(m, x, .28)));
    await step(`${button} mouse up, no stuck pointer`, 'pointer', 1, () => mouse('mouseReleased', x, .28, { button, buttons: 0, clickCount: 1 }),
      (s, p) => !s.pointers[0] && p.some(m => m.kind === 1 && m.action === 2 && m.button === number && m.buttons === 0));
  }
  await step('wheel coordinates and scaled deltas', 'wheel', 1, () => mouse('mouseWheel', .25, .3, { deltaX: 12, deltaY: 48 }),
    (s, p) => p.some(m => m.kind === 2 && m.mode === 0 && near(m, .25, .3) && Math.abs(m.dx - 12 / rect.scale) < .26 && Math.abs(m.dy - 48 / rect.scale) < .26));

  // CDP modifier bits differ from protocol bits: CDP Shift=8, Ctrl=2; protocol Shift=1, Ctrl=2.
  async function key(name, code, key, vk, modifiers = 0, protocolMods = 0) {
    for (const [type, action] of [['keyDown', 0], ['keyUp', 1]]) {
      await step(`${name} ${type}: code/key/VK/modifiers`, 'key', 1,
        () => viewer.call('Input.dispatchKeyEvent', { type, code, key, windowsVirtualKeyCode: vk, nativeVirtualKeyCode: vk, modifiers }),
        (s, p) => p.some(m => m.kind === 3 && m.action === action && m.code === code && m.key === key && m.keyCode === vk && m.mods === protocolMods));
    }
  }
  await key('a', 'KeyA', 'a', 65);
  await key('Shift+A', 'KeyA', 'A', 65, 8, 1);
  await key('Enter', 'Enter', 'Enter', 13);
  await key('Backspace', 'Backspace', 'Backspace', 8);
  await key('ArrowLeft', 'ArrowLeft', 'ArrowLeft', 37);
  await key('Ctrl+c', 'KeyC', 'c', 67, 2, 2);

  const touch = (id, x, y) => ({ id, ...point(x, y), radiusX: 4, radiusY: 4, force: .5 });
  const dispatchTouch = (type, touchPoints) => viewer.call('Input.dispatchTouchEvent', { type, touchPoints });
  await step('two simultaneous touch downs and normalized positions', 'pointer', 2,
    () => dispatchTouch('touchStart', [touch(10, .2, .65), touch(20, .75, .7)]),
    (s, p) => p.filter(m => m.kind === 1 && m.ptype === 1 && m.action === 0).length === 2 && Object.keys(s.pointers).length === 2 &&
      p.some(m => m.ptype === 1 && near(m, .2, .65)) && p.some(m => m.ptype === 1 && near(m, .75, .7)));
  const downs = current.input.filter(m => m.kind === 1 && m.ptype === 1 && m.action === 0).slice(-2);
  const slots = downs.map(m => m.pid).sort((a, b) => a - b);
  check('two-touch pid slots 1 and 2 (mouse=0, touch=1..15 per 06)', compact(slots) === '[1,2]', `observed=${compact(slots)}`);
  check('documented two-touch pid slots 1 and 2', compact(slots) === '[1,2]', compact(slots));
  const [first, second] = downs.map(m => m.pid);
  await step('both touch moves preserve slots and positions', 'pointer', 2,
    () => dispatchTouch('touchMove', [touch(10, .25, .68), touch(20, .7, .73)]),
    (s, p) => stateNear(s.pointers[first], .25, .68) && stateNear(s.pointers[second], .7, .73) &&
      p.some(m => m.action === 1 && m.ptype === 1 && m.pid === first && near(m, .25, .68)) && p.some(m => m.action === 1 && m.ptype === 1 && m.pid === second && near(m, .7, .73)));
  // CDP touchEnd's points are the contacts to release, not the contacts that remain active.
  await step('touchEnd one leaves the other active', 'pointer', 1, () => dispatchTouch('touchEnd', [touch(10, .25, .68)]),
    (s, p) => !s.pointers[first] && !!s.pointers[second] && Object.keys(s.pointers).length === 1 && p.some(m => m.ptype === 1 && m.action === 2 && m.pid === first));
  await step('touchCancel remaining contact', 'pointer', 1, () => dispatchTouch('touchCancel', []),
    (s, p) => Object.keys(s.pointers).length === 0 && p.some(m => m.ptype === 1 && m.action === 3 && m.pid === second));
  await step('tap text field down', 'pointer', 1, () => dispatchTouch('touchStart', [touch(30, .5, .455)]),
    (s, p) => s.field.focused && p.some(m => m.ptype === 1 && m.action === 0 && near(m, .5, .455)));
  await step('tap text field up', 'pointer', 1, () => dispatchTouch('touchEnd', []),
    (s, p) => Object.keys(s.pointers).length === 0 && p.some(m => m.ptype === 1 && m.action === 2));
  try {
    const ime = await poll(() => viewer.evaluate(`({state:window.rk.ime.state,active:document.activeElement.id,
      ctl:window.__smoke.ctl.filter(m=>m.t==='ime.state'||m.t==='ime.vk')})`),
    v => v.state.focused && v.active === 'ime' && v.ctl.some(m => m.t === 'ime.state' && m.focused) && v.ctl.some(m => m.t === 'ime.vk' && m.show));
    check('viewer received ime.state and ime.vk, editor focused', true, `focused=${ime.state.focused}; active=${ime.active}`);
  } catch (e) { check('viewer received ime.state and ime.vk, editor focused', false, e.message); }
  const committed = 'héllo 世界';
  await step('insertText Unicode committed exactly once', 'ime', 1, () => viewer.call('Input.insertText', { text: committed }),
    (s, p, ctl) => s.field.text === committed && s.field.comp === '' && ctl.filter(m => m.t === 'ime.commit' && m.text === committed).length === 1);
  let compositionSupported = true;
  const composition = '你好';
  try {
    await step('IME composition reaches mock without committing', 'ime', 1,
      () => viewer.call('Input.imeSetComposition', { text: composition, selectionStart: composition.length, selectionEnd: composition.length }),
      (s, p, ctl) => s.field.text === committed && s.field.comp === composition && ctl.some(m => m.t === 'ime.compose' && m.text === composition && m.cursor === composition.length));
  } catch (e) {
    if (e.code !== -32601) throw e;
    compositionSupported = false;
    console.log('INFO IME composition unavailable: CDP Input.imeSetComposition method not found');
    check('optional IME composition capability handled', true, 'unsupported; composition checks skipped');
  }
  if (compositionSupported) {
    await step('IME composition commit exactly once, composition cleared', 'ime', 1,
      () => viewer.call('Input.insertText', { text: composition }),
      (s, p, ctl) => s.field.text === committed + composition && s.field.comp === '' && ctl.filter(m => m.t === 'ime.commit' && m.text === composition).length === 1);
  }
  // Let trailing composition/input events and delayed channel messages settle before final assertions.
  await delay(200);
  const final = await mock.evaluate(snapshot);
  check('final committed field text', final.state.field.text === committed + (compositionSupported ? composition : '') && final.state.field.comp === '', compact(final.state.field.text));
  check('final pointers empty', Object.keys(final.state.pointers).length === 0, compact(final.state.pointers));
  const packets = final.input;
  check('binary inputSeq delivered in order without gaps', packets.length > 0 && packets.every((m, i) => m && (!i || m.seq === ((packets[i - 1].seq + 1) & 65535))) && final.state.inputSeq === packets.at(-1).seq, `packets=${packets.length}; inputSeq=${final.state.inputSeq}`);
  const trust = await viewer.evaluate('({trusted:window.__smoke.trusted,untrusted:window.__smoke.untrusted,hardwareTrusted:window.__smoke.hardwareTrusted,hardwareUntrusted:window.__smoke.hardwareUntrusted})');
  check('viewer pointer/wheel/key events are browser trusted', trust.hardwareTrusted > 0 && trust.hardwareUntrusted === 0, compact(trust));
  console.log('INFO final counts=' + compact(final.state.counts));
}

try { await main(); }
catch (error) {
  check('harness completed', false, error.stack || error.message);
  for (const line of tail.slice(-8)) console.log('DIAG ' + line);
} finally {
  clearTimeout(watchdog);
  cleanup();
  console.log(`RESULT ${passed} passed, ${failed} failed`);
  // Do not leave WebSocket close-handshake timers alive after their browser processes were killed.
  process.exit(failed ? 1 : 0);
}
