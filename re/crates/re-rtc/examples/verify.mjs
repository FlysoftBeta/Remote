// Local acceptance fixture: exact viewer + stdio probe + two-socket UDP bridge.
// Counts clear RTP header extensions on SRTP; does not decrypt media.
// node re/crates/re-rtc/examples/verify.mjs --out=C:/src/tmp/m6/rtc-native/edge
// Add --chrome89 for the pinned compatibility browser. Build the probe first.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import http from 'node:http';
import dgram from 'node:dgram';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { spawn, spawnSync } from 'node:child_process';

const repo = fileURLToPath(new URL('../../../../', import.meta.url));
const require = createRequire(path.join(repo, 'control-plane/package.json'));
const { WebSocketServer } = require('ws');
const { Decoder, Writer } = await import(new URL('../../../../control-plane/framing.js', import.meta.url));
const arg = (name, fallback) => process.argv.find(v => v.startsWith(`--${name}=`))?.slice(name.length + 3) ?? fallback;
const old = process.argv.includes('--chrome89');
const noDelay = process.argv.includes('--no-playout-delay');
const dropFrame = process.argv.includes('--drop-frame');
const out = path.resolve(arg('out', `C:/src/tmp/m6/rtc-native/${old ? 'chrome89' : 'edge'}`));
fs.mkdirSync(out, { recursive: true });
const exe = arg('browser', old ? 'C:/src/tmp/chrome89-google/app/chrome.exe' : 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe');
const probe = path.join(repo, 're/target/release/examples/re-rtc-probe.exe');
const dll = process.env.RE_NATIVE_DLL || 'C:/src/chromium/src/out/dev/re_native.dll';
const host = Object.values(os.networkInterfaces()).flat().find(v => v.family === 'IPv4' && !v.internal)?.address;
if (!host) throw new Error('need a local IPv4 interface for the UDP acceptance bridge');
const ep = dgram.createSocket('udp4'), vp = dgram.createSocket('udp4');
await Promise.all([ep, vp].map(s => new Promise(r => s.bind(0, host, r))));
let endpointAddr, viewerAddr, viewer, ready = false, started = false;
let child, browser, cdp, server, wsServer;
let endpointLog = '';
let extensions = new Map();
const wire = { rtpPackets: 0, playoutPackets: 0, twccPackets: 0, malformedPlayout: 0,
  droppedPackets: 0, pliPackets: 0, firPackets: 0 };
let firstDroppedTimestamp;
let mediaStartedAt;
const sid = 're-rtc-native-probe';

function inspect(b) {
  if (b.length < 12 || b[0] >> 6 !== 2 || (b[1] >= 192 && b[1] <= 223)) return;
  wire.rtpPackets++;
  if (!(b[0] & 16)) return;
  let off = 12 + (b[0] & 15) * 4;
  if (off + 4 > b.length) return;
  const profile = b.readUInt16BE(off), end = off + 4 + b.readUInt16BE(off + 2) * 4;
  if (end > b.length) return;
  off += 4;
  while (off < end) {
    const h = b[off++];
    if (!h) continue;
    let id, n;
    if (profile === 0xbede) { id = h >> 4; n = (h & 15) + 1; if (id === 15) break; }
    else if ((profile & 0xfff0) === 0x1000) { id = h; if (off >= end) break; n = b[off++]; }
    else break;
    if (off + n > end) break;
    const uri = extensions.get(id) || '';
    if (uri.includes('playout-delay')) {
      wire.playoutPackets++;
      if (n !== 3 || b[off] !== 0 || b[off + 1] !== 0 || b[off + 2] !== 0) wire.malformedPlayout++;
    }
    if (uri.includes('transport-wide-cc')) wire.twccPackets++;
    off += n;
  }
}
ep.on('message', (b, r) => {
  endpointAddr = r;
  inspect(b);
  if (dropFrame && b.length >= 12 && b[0] >> 6 === 2 && !(b[1] >= 192 && b[1] <= 223)) {
    const ts = b.readUInt32BE(4);
    if (!(b[0] & 32)) mediaStartedAt ??= Date.now();
    // Drop a complete steady-state reference picture, including its RTX,
    // instead of early padding or the initial IDR duplicated by ctl hello.
    if (firstDroppedTimestamp === undefined && !(b[0] & 32) && Date.now() - mediaStartedAt >= 3000)
      firstDroppedTimestamp = ts;
    if (ts === firstDroppedTimestamp) { wire.droppedPackets++; return; }
  }
  if (viewerAddr) vp.send(b, viewerAddr.port, viewerAddr.address);
});
vp.on('message', (b, r) => {
  viewerAddr = r;
  if (b.length >= 8 && b[0] >> 6 === 2 && b[1] === 206) {
    if ((b[0] & 31) === 1) wire.pliPackets++;
    if ((b[0] & 31) === 4) wire.firPackets++;
  }
  if (endpointAddr) ep.send(b, endpointAddr.port, endpointAddr.address);
});
function candidate(text, side) {
  const a = text.trim().split(/\s+/);
  if (a[2]?.toLowerCase() !== 'udp' || !/^\d+\.\d+\.\d+\.\d+$/.test(a[4])) return null;
  const real = { address: a[4], port: Number(a[5]) };
  if (side === 'endpoint') {
    if (!endpointAddr || real.address === host) endpointAddr = real;
    a[5] = String(vp.address().port);
  } else {
    if (!viewerAddr || real.address === host) viewerAddr = real;
    a[5] = String(ep.address().port);
  }
  a[4] = host;
  return a.join(' ');
}
function rewrite(message, side) {
  if (message.sdp) {
    fs.writeFileSync(path.join(out, `${side}.sdp`), message.sdp);
    if (side === 'endpoint') {
      extensions = new Map([...message.sdp.matchAll(/^a=extmap:(\d+)(?:\/\S+)? (\S+)/gm)].map(m => [Number(m[1]), m[2]]));
    }
    message.sdp = message.sdp.split(/\r?\n/).flatMap(line => {
      if (!line.startsWith('a=candidate:')) return [line];
      const c = candidate(line.slice(2), side); return c ? ['a=' + c] : [];
    }).join('\r\n');
  }
  if (message.t === 'ice' && message.candidate?.candidate) {
    const c = candidate(message.candidate.candidate, side);
    if (!c) return null;
    message.candidate.candidate = c;
  }
  return message;
}
function terminate(p) {
  // Only processes spawned by this fixture. Node retains exit status, so an
  // already exited PID is never passed to taskkill.
  if (p?.pid && p.exitCode === null && p.signalCode === null)
    spawnSync('taskkill', ['/PID', String(p.pid), '/T', '/F'], { stdio: 'ignore', windowsHide: true });
}
const pause = ms => new Promise(r => setTimeout(r, ms));
try {
  const endpointFd = fs.openSync(path.join(out, 'endpoint.log'), 'w');
  const probeArgs = [`--width=${arg('width', '1280')}`, `--height=${arg('height', '720')}`, `--fps=${arg('fps', '30')}`];
  if (noDelay) probeArgs.push('--no-playout-delay');
  child = spawn(probe, probeArgs, {
    env: { ...process.env, RE_NATIVE_DLL: dll, RE_RTC_PROBE_FIRST_AU: path.join(out, 'first-frame.h264'),
      RB_TRACE: '1', PATH: `${path.dirname(dll)};${process.env.PATH}` },
    stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true,
  });
  child.on('error', e => { endpointLog += e.message; });
  child.stderr.on('data', b => { endpointLog += b; fs.writeSync(endpointFd, b); });
  const writer = new Writer(child.stdin);
  child.stdin.on('error', () => {});
  function start() {
    if (!ready || !viewer || started) return;
    started = true;
    viewer.send(JSON.stringify({ t: 'welcome', v: 1, sid, instance: 'default', iceServers: [] }));
    writer.write({ t: 'session.open', sid, iceServers: [] }).catch(e => { endpointLog += e.message; });
  }
  const decoder = new Decoder(m => {
    if (m.t === 'hello') { ready = true; start(); return; }
    const message = rewrite(m, 'endpoint');
    if (message && viewer?.readyState === 1) viewer.send(JSON.stringify(message));
  });
  child.stdout.on('data', b => decoder.push(b));
  server = http.createServer(async (req, res) => {
    const url = new URL(req.url, 'http://localhost');
    if (url.pathname === '/field-config') { res.writeHead(404); res.end('{}'); return; }
    const name = url.pathname === '/' ? '/viewer/index.html' : url.pathname.startsWith('/tools/') ? url.pathname : '/viewer' + url.pathname;
    const file = path.resolve(repo, '.' + name);
    if (!file.startsWith(path.resolve(repo) + path.sep)) { res.writeHead(403); res.end(); return; }
    try {
      const data = await fs.promises.readFile(file);
      res.setHeader('content-type', { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css' }[path.extname(file)] || 'application/octet-stream');
      res.end(data);
    } catch { res.writeHead(404); res.end(); }
  });
  wsServer = new WebSocketServer({ server, path: '/signal' });
  wsServer.on('connection', w => {
    viewer = w;
    w.on('message', b => {
      const m = JSON.parse(b);
      if (m.t === 'hello') { start(); return; }
      const message = rewrite(m, 'viewer'); if (message) writer.write(message).catch(e => { endpointLog += e.message; });
    });
  });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const profile = path.join(out, `profile-${Date.now()}`);
  const args = [`--user-data-dir=${profile}`, '--no-first-run', '--no-default-browser-check',
    '--autoplay-policy=no-user-gesture-required', '--disable-background-networking', '--disable-component-update',
    '--disable-features=CalculateNativeWinOcclusion,WebRtcHideLocalIpsWithMdns', '--remote-debugging-port=0'];
  if (old) args.push('--no-sandbox', '--enable-features=NetworkServiceInProcess');
  else args.push('--headless=new');
  args.push(`http://127.0.0.1:${server.address().port}/`);
  const browserFd = fs.openSync(path.join(out, 'browser.log'), 'w');
  browser = spawn(exe, args, { stdio: ['ignore', browserFd, browserFd], windowsHide: true });
  browser.on('error', e => { endpointLog += e.message; });
  let tab;
  for (let i = 0; i < 300; i++) {
    if (child.exitCode !== null || child.signalCode !== null) throw new Error(`probe exited: ${child.exitCode}; ${endpointLog.slice(-4000)}`);
    try {
      const port = Number(fs.readFileSync(path.join(profile, 'DevToolsActivePort'), 'utf8').split('\n')[0]);
      const tabs = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
      tab = tabs.find(t => t.type === 'page' && t.url.includes(`:${server.address().port}/`));
      if (tab) break;
    } catch {}
    await pause(200);
  }
  if (!tab) throw new Error('browser CDP startup timeout');
  cdp = new WebSocket(tab.webSocketDebuggerUrl);
  await new Promise((r, j) => { cdp.onopen = r; cdp.onerror = j; });
  let id = 0; const pending = new Map();
  cdp.onmessage = e => { const m = JSON.parse(e.data); if (m.id) { pending.get(m.id)?.(m); pending.delete(m.id); } };
  const call = (method, params) => new Promise(r => { const i = ++id; pending.set(i, r); cdp.send(JSON.stringify({ id: i, method, params })); });
  let pageReady = false;
  for (let i = 0; i < 150; i++) {
    const status = await call('Runtime.evaluate', { expression: "document.readyState==='complete' && !!document.querySelector('video') && !!window.rk", returnByValue: true });
    if (status.result?.result?.value) { pageReady = true; break; }
    await pause(200);
  }
  if (!pageReady) {
    const html = await call('Runtime.evaluate', { expression: 'document.documentElement.outerHTML', returnByValue: true });
    fs.writeFileSync(path.join(out, 'page-error.json'), JSON.stringify(html, null, 2));
    throw new Error('viewer page did not initialize (see page-error.json)');
  }
  const evaluated = await call('Runtime.evaluate', { awaitPromise: true, returnByValue: true, expression: `new Promise(resolve => {
    let frames = 0, echo = false, echoRttMs = null, keyframeSent = false;
    const video = document.querySelector('video');
    function count() { frames++; video.requestVideoFrameCallback(count); }
    video.requestVideoFrameCallback(count);
    const deadline = setTimeout(async () => {
      clearInterval(poll);
      const pc = window.rk && window.rk.pc;
      const stats = pc ? Array.from((await pc.getStats()).values()).filter(s => s.type === 'inbound-rtp' || s.type === 'codec') : [];
      resolve({ frames, echo, echoRttMs, keyframeSent, width:video.videoWidth, height:video.videoHeight,
        state:pc && pc.connectionState, stats, ua:navigator.userAgent });
    }, ${Number(arg('seconds', '15')) * 1000});
    const poll = setInterval(() => {
      const dc = window.rk && window.rk.clock && window.rk.clock.ch;
      if (!dc || dc.readyState !== 'open' || dc.echoSent) return;
      dc.echoSent = true;
      const nonce = 're-native-' + Date.now(), begin = performance.now();
      dc.addEventListener('message', e => { let m; try { m=JSON.parse(e.data); } catch { return; }
        if (m.t === 'echo' && m.nonce === nonce) { echo=true; echoRttMs=performance.now()-begin; } });
      dc.send(JSON.stringify({t:'echo',nonce}));
      const receiver = window.rk.pc.getReceivers().find(r => r.track && r.track.kind === 'video');
      if (receiver && typeof receiver.requestKeyFrame === 'function') { receiver.requestKeyFrame(); keyframeSent=true; }
    }, 100);
  })` });
  if (evaluated.result?.exceptionDetails) throw new Error(JSON.stringify(evaluated.result.exceptionDetails));
  const result = evaluated.result?.result?.value;
  const evidence = { ...result, wire, extensions: Object.fromEntries(extensions),
    bweCallbacks: (endpointLog.match(/BWE bitrate_bps=/g) || []).length,
    keyframeCallbacks: (endpointLog.match(/keyframe requested/g) || []).length,
    browserArgs: args, nativeDll: dll };
  fs.writeFileSync(path.join(out, 'result.json'), JSON.stringify(evidence, null, 2));
  console.log(JSON.stringify(evidence, null, 2));
  if (!result || result.frames < 30 || !result.echo || (noDelay ? wire.playoutPackets !== 0 : wire.playoutPackets < 1) || wire.malformedPlayout ||
      wire.twccPackets < 1 || evidence.bweCallbacks < 1) throw new Error('RTC acceptance failed (see result.json)');
} finally {
  cdp?.close();
  if (browser && browser.exitCode === null) terminate(browser);
  terminate(child);
  wsServer?.clients.forEach(w => w.terminate());
  wsServer?.close(); server?.close(); ep.close(); vp.close();
}
