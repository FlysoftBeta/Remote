// Real endpoint + Edge regression: a single static paint starting at 300 kbps must
// continue decoding delta frames and converge toward the source pixels.
// node tools/quality/static-recovery.mjs [--encoder openh264|vpl] [--endpoint <exe>]
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import net from 'node:net';
import { spawn, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { CDP, sleep } from '../soak/support.mjs';
import { testSession } from '../auth.mjs';

const repo = fileURLToPath(new URL('../../', import.meta.url));
const arg = (key, fallback) => { const i = process.argv.indexOf('--' + key); return i < 0 ? fallback : process.argv[i + 1]; };
const endpoint = path.resolve(arg('endpoint', 'C:/src/chromium/src/out/dev/re.exe'));
const encoder = arg('encoder', 'openh264');
const output = path.resolve(arg('out', `C:/src/tmp/static-recovery-${encoder}-${Date.now()}`));
fs.mkdirSync(output, { recursive: true });
const children = [], clients = [];
const report = { encoder, endpoint, samples: [] };
async function port() {
  const s = net.createServer();
  return new Promise((resolve, reject) => { s.once('error', reject); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => resolve(p)); }); });
}
async function poll(fn, ms = 45000) {
  const end = Date.now() + ms; let error;
  while (Date.now() < end) { try { const r = await fn(); if (r) return r; } catch (e) { error = e; } await sleep(100); }
  throw new Error(`poll timeout: ${error || 'condition not met'}`);
}
function launch(exe, args, name, env = process.env) {
  const fd = fs.openSync(path.join(output, name + '.log'), 'a');
  try {
    const p = spawn(exe, args, { cwd: repo, env, windowsHide: true, stdio: ['ignore', fd, fd] });
    p.on('error', e => { report.launchError = String(e); }); children.push(p); return p;
  } finally { fs.closeSync(fd); }
}
async function attach(debugPort, match) {
  const target = await poll(async () => (await (await fetch(`http://127.0.0.1:${debugPort}/json/list`)).json()).find(p => p.type === 'page' && p.url.includes(match)));
  const c = await CDP.connect(target.webSocketDebuggerUrl); clients.push(c); return c;
}
try {
  const [cpPort, remotePort, viewerPort] = await Promise.all([port(), port(), port()]);
  const base = `http://127.0.0.1:${cpPort}`;
  launch(process.execPath, [path.join(repo, 'control-plane/index.js'), '--port', String(cpPort)], 'cp', {
    ...process.env, HOST: '127.0.0.1', CP_TOKEN: 'static-test', CP_DATA_DIR: path.join(output, 'data'),
    TLS_CERT: '', TLS_KEY: '', TLS_CA: '', TLS_PFX: '', HTTP_REDIRECT_PORT: '', ENDPOINT_PATH: endpoint,
    ENDPOINT_ARGS_JSON: JSON.stringify([
      `--chrome=${path.join(path.dirname(endpoint), 'chrome.exe')}`, '--isolation=job', '--job-ui-limits=none',
      `--encoder=${encoder}`, '--fps=15', '--min-kbps=300', '--start-kbps=300',
      `--max-kbps=${arg('max-kbps', '8000')}`,
      '--cr-window-size=800,600', `--cr-remote-debugging-port=${remotePort}`, `--url=${base}/tools/quality/static.html`
    ])
  });
  await poll(async () => (await fetch(base + '/api/health')).ok);
  const token = await testSession(base, { serverCredential: 'static-test' });
  const remote = await attach(remotePort, '/tools/quality/static.html');
  await poll(() => remote.evaluate('document.readyState === "complete" && !!document.querySelector("canvas")'));
  // Source screenshot includes the real text rasterization and color conversion.
  const reference = (await remote.call('Page.captureScreenshot', {
    format: 'png', clip: { x: 0, y: 0, width: 800, height: 600, scale: 1 }
  })).data;
  const toolbarHeight = await remote.evaluate('outerHeight - innerHeight');
  fs.writeFileSync(path.join(output, 'source.png'), Buffer.from(reference, 'base64'));
  launch(arg('viewer', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'), [
    '--headless=new', `--remote-debugging-port=${viewerPort}`, `--user-data-dir=${path.join(output, 'viewer')}`,
    '--no-first-run', '--no-default-browser-check', '--autoplay-policy=no-user-gesture-required',
    '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
    `${base}/tools/multi/receiver.html?token=${encodeURIComponent(token)}`
  ], 'viewer');
  const viewer = await attach(viewerPort, '/tools/multi/receiver.html');
  await poll(() => viewer.evaluate('document.readyState === "complete" && !!window.rk'));
  await viewer.evaluate(`(async () => {
    const im = new Image(); im.src = 'data:image/png;base64,${reference}'; await im.decode();
    const c = document.createElement('canvas'); c.width = 800; c.height = 600;
    const x = c.getContext('2d', {willReadFrequently:true}); x.drawImage(im, 0, 0, 800, 600);
    const ref = x.getImageData(0, 0, 800, 600).data;
    window.qualitySample = async () => {
      const v = document.getElementById('video');
      if (!v.videoWidth || !window.rk?.pc) return null;
      x.drawImage(v, 0, 0, 800, 600); const p = x.getImageData(0, 0, 800, 600).data;
      const offset = ${toolbarHeight};
      let mse = 0; for (let y = offset; y < 600; y++) for (let col = 0; col < 800; col++)
        for (let k = 0; k < 3; k++) mse += (p[(y*800+col)*4+k] - ref[((y-offset)*800+col)*4+k]) ** 2;
      let stats; (await rk.pc.getStats()).forEach(s => { if (s.type === 'inbound-rtp' && s.kind === 'video') stats = s; });
      return { at: Date.now(), mse: mse / (800 * (600-offset) * 3), frames: stats?.framesDecoded || 0,
        keys: stats?.keyFramesDecoded || 0, bytes: stats?.bytesReceived || 0, width: v.videoWidth, height: v.videoHeight };
    };
  })()`);
  const first = await poll(async () => { const s = await viewer.evaluate('qualitySample()'); return s?.frames > 0 && s; });
  async function saveFrame(name) {
    const data = await viewer.evaluate(`(() => { const c = document.createElement('canvas'); c.width=800; c.height=600; c.getContext('2d').drawImage(document.getElementById('video'),0,0,800,600); return c.toDataURL().split(',')[1]; })()`);
    fs.writeFileSync(path.join(output, name + '.png'), Buffer.from(data, 'base64'));
  }
  await saveFrame('first');
  report.samples.push(first);
  for (let i = 0; i < 15; i++) {
    await sleep(1000); report.samples.push(await viewer.evaluate('qualitySample()'));
  }
  const last = report.samples.at(-1);
  await saveFrame('last');
  console.log(JSON.stringify({ first, last }));
  assert.equal(last.width, 800); assert.equal(last.height, 600);
  assert.ok(last.frames - first.frames >= 8, 'static content must continue decoding refreshes');
  assert.ok(last.frames - first.frames <= 130, 'refresh must respect the 15 fps source ceiling');
  assert.ok(last.keys - first.keys <= 1, 'idle refresh must not cause periodic IDRs');
  assert.ok(last.mse < first.mse * 0.8 || (first.mse < 30 && last.mse < 30),
    'static image must improve by at least 20% or remain already clear (MSE < 30)');
  // Real content must immediately regain its ordinary frame budget.
  await remote.evaluate(`window.qualityAnimation = setInterval(() => {
    const x = document.querySelector('canvas').getContext('2d');
    x.fillStyle = Math.random() > .5 ? '#000' : '#fff'; x.fillRect(0, 0, 150, 100);
  }, 1000 / 15)`);
  const movingStart = await viewer.evaluate('qualitySample()');
  await sleep(3000);
  const movingEnd = await viewer.evaluate('qualitySample()');
  report.movingFrameDelta = movingEnd.frames - movingStart.frames;
  assert.ok(report.movingFrameDelta >= 20, 'real motion must regain the frame budget');
  await remote.evaluate('clearInterval(window.qualityAnimation)');
  // Closing the viewer must stop the refresh loop and source delivery.
  await viewer.call('Page.navigate', { url: 'about:blank' });
  await sleep(3000);
  const log = fs.readFileSync(path.join(output, 'cp.log'), 'utf8');
  assert.match(log.split('\n').filter(l => l.includes('D3D source fps=')).at(-1),
    /D3D source fps=0 native=0 i420=0/);
  if (encoder === 'vpl') assert.match(log, /PRTS encoder active=vpl/);
  await viewer.call('Page.navigate', {
    url: `${base}/tools/multi/receiver.html?token=${encodeURIComponent(token)}`
  });
  await poll(() => viewer.evaluate(`(async () => {
    if (!window.rk?.pc) return false;
    let frames = 0; (await rk.pc.getStats()).forEach(s => {
      if (s.type === 'inbound-rtp' && s.kind === 'video') frames = s.framesDecoded || 0;
    }); return frames >= 3;
  })()`));
  report.reconnected = true;
  report.passed = true;
} catch (e) { report.passed = false; report.error = e.stack; console.error(e.stack); process.exitCode = 1; }
finally {
  for (const c of clients) c.close();
  for (const p of children.toReversed()) if (p.exitCode === null && p.signalCode === null)
    spawnSync('taskkill.exe', ['/PID', String(p.pid), '/T', '/F'], { windowsHide: true, stdio: 'ignore' });
  fs.writeFileSync(path.join(output, 'result.json'), JSON.stringify(report, null, 2));
  console.log('evidence: ' + output);
}
