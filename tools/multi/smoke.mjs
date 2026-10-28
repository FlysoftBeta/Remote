// Short functional acceptance: one CP, three real dev endpoints and headless viewers. No latency measurement.
import fs from 'node:fs';
import path from 'node:path';
import net from 'node:net';
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { CDP, sleep, processes, killOwned } from '../soak/support.mjs';
import { testSession } from '../auth.mjs';

const repo = fileURLToPath(new URL('../../', import.meta.url));
const argv = process.argv.slice(2);
const arg = (key, fallback) => { const i = argv.indexOf('--' + key); return i < 0 ? fallback : argv[i + 1]; };
const output = path.resolve(arg('out', `C:/src/tmp/m8/multi/run-${Date.now()}`));
const endpoint = path.resolve(arg('endpoint', 'C:/src/chromium/src/out/dev/re.exe'));
const chrome = path.join(path.dirname(endpoint), 'chrome.exe');
const edge = arg('viewer', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe');
fs.mkdirSync(output, { recursive: true });
if (fs.existsSync(path.join(output, 'result.json'))) throw new Error('use a new output directory');
for (const exe of [endpoint, chrome, edge]) assert.ok(fs.existsSync(exe), `missing ${exe}`);
const report = { started: new Date().toISOString(), endpoint, endpointBuild: fs.statSync(endpoint).mtime.toISOString(), checks: [], frames: {}, limits: { memoryMB: 4096, cpuRate: 10000, processCount: 64 } };
const owned = new Map(), children = [], viewers = new Map();
const ids = ['default', 'alpha', 'beta'];
let cp, base, token;
const probe = fs.readFileSync(path.join(repo, 'tools/soak/viewer-probe.js'), 'utf8');
const log = (name, detail) => { report.checks.push({ name, detail, at: new Date().toISOString() }); console.log('[V] ' + name); };
async function poll(fn, timeout = 45000) {
  const end = Date.now() + timeout; let error;
  while (Date.now() < end) { try { const result = await fn(); if (result) return result; } catch (e) { error = e; } await sleep(150); }
  throw new Error('poll timed out' + (error ? ': ' + error.message : ''));
}
async function freePort() {
  const s = net.createServer();
  return new Promise((resolve, reject) => { s.once('error', reject); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => resolve(p)); }); });
}
async function api(route, method = 'GET', body, expected = 200) {
  const r = await fetch(base + '/api/' + route, { method, headers: { Authorization: 'Bearer ' + token, 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body), signal: AbortSignal.timeout(15000) });
  const text = await r.text(); assert.equal(r.status, expected, route + ': ' + text); return text ? JSON.parse(text) : null;
}
function launch(exe, args, name, env = process.env) {
  const fd = fs.openSync(path.join(output, name + '.log'), 'a');
  try {
    const child = spawn(exe, args, { cwd: repo, env: { ...env, TEMP: output, TMP: output }, stdio: ['ignore', fd, fd], windowsHide: true });
    children.push(child); child.on('error', e => { report.launchError = e.message; }); return child;
  } finally { fs.closeSync(fd); }
}
function track() {
  const rows = processes(), roots = new Set(children.filter(p => p.exitCode === null && p.signalCode === null).map(p => p.pid));
  for (const row of rows) if (owned.get(row.ProcessId)?.Created === row.Created) roots.add(row.ProcessId);
  for (let i = 0; i < rows.length; i++) for (const row of rows) if (roots.has(row.ParentProcessId)) roots.add(row.ProcessId);
  for (const row of rows) if (roots.has(row.ProcessId)) owned.set(row.ProcessId, row);
  return rows.filter(row => roots.has(row.ProcessId));
}
async function startCP() {
  if (!base) base = 'http://127.0.0.1:' + await freePort();
  const port = Number(new URL(base).port);
  const reArgs = [`--chrome=${chrome}`, '--isolation=job', '--job-ui-limits=none', '--job-memory-mb=4096', '--job-cpu-rate=10000', '--job-process-count=64',
    '--fps=15', '--encoder=openh264', `--url=${base}/tools/latency/remote-page.html?scenario=marker`, '--cr-window-size=800,600'];
  cp = launch(process.execPath, [path.join(repo, 'control-plane/index.js'), '--port', String(port)], 'cp',
    { ...process.env, HOST: '127.0.0.1', CP_TOKEN: 'multi-test', CP_DATA_DIR: path.join(output, 'data'), ENDPOINT_PATH: endpoint,
      ENDPOINT_ARGS_JSON: JSON.stringify(reArgs), TLS_CERT: '', TLS_KEY: '', TLS_CA: '', TLS_PFX: '', HTTP_REDIRECT_PORT: '' });
  await poll(async () => { if (cp.exitCode !== null || cp.signalCode !== null) throw new Error('CP exited'); return (await fetch(base + '/api/health')).ok; });
  token = await testSession(base, { serverCredential: 'multi-test' });
}
async function viewer(id, suffix = '') {
  const dir = path.join(output, 'viewer-' + id + suffix);
  // Exercise the legacy URL for default; named instances use the existing query parameter.
  const url = `${base}/tools/multi/receiver.html?token=${encodeURIComponent(token)}${id === 'default' ? '' : '&instance=' + encodeURIComponent(id)}`;
  launch(edge, ['--headless=new', '--remote-debugging-port=0', `--user-data-dir=${dir}`, '--no-first-run', '--no-default-browser-check',
    '--autoplay-policy=no-user-gesture-required', '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
    '--disable-backgrounding-occluded-windows', '--disable-features=WebRtcHideLocalIpsWithMdns,CalculateNativeWinOcclusion', '--window-size=800,600',
    url], 'viewer-' + id + suffix);
  const dp = await poll(() => { const f = path.join(dir, 'DevToolsActivePort'); return fs.existsSync(f) && Number(fs.readFileSync(f, 'utf8').split('\n')[0]); });
  const target = await poll(async () => (await (await fetch(`http://127.0.0.1:${dp}/json/list`)).json()).find(p => p.type === 'page' && p.url.startsWith(base)));
  const cdp = await CDP.connect(target.webSocketDebuggerUrl); viewers.set(id + suffix, cdp);
  await poll(() => cdp.evaluate('!!window.rk && !!document.getElementById("video")'));
  await cdp.evaluate(probe);
  await poll(async () => { const s = await cdp.evaluate('soakSnapshot(false)'); return s.frames >= 3 && s.inbound?.framesDecoded >= 3 && s; });
  return cdp;
}
async function snapshot(id) { return viewers.get(id).evaluate('soakSnapshot(false)'); }
async function noOldTree(rows) {
  await poll(() => { const now = processes(); return rows.every(old => !now.some(r => r.ProcessId === old.ProcessId && r.Created === old.Created)); }, 20000);
}
function browserTree(rows, pid) {
  const tree = new Set([pid]);
  for (let i = 0; i < rows.length; i++) for (const p of rows) if (tree.has(p.ParentProcessId)) tree.add(p.ProcessId);
  return rows.filter(p => tree.has(p.ProcessId));
}

try {
  const build = processes().filter(p => /^(siso|ninja|autoninja)\.exe$/i.test(p.Name));
  assert.equal(build.length, 0, 'wait for the shared build to finish');
  await startCP();
  await Promise.all(ids.slice(1).map(async id => {
    await api('profiles', 'POST', { id, name: id + ' profile' }, 201);
    await api('instances', 'POST', { id, name: id + ' browser', profile: id, isolation: 'job', autostart: true, jobLimits: report.limits,
      reArgs: ['--fps=15', '--encoder=openh264', `--url=${base}/tools/latency/remote-page.html?scenario=marker`], chromeArgs: ['--window-size=800,600'] }, 201);
    await api('instances/' + id + '/start', 'POST');
  }));
  await poll(async () => (await api('instances')).every(c => c.state === 'running'));
  const state = await api('instances');
  assert.equal(new Set(state.map(c => c.pid)).size, 3); assert.equal(new Set(state.map(c => c.userDataDir)).size, 3);
  for (const row of state) fs.writeFileSync(path.join(row.userDataDir, 'multi-persistence.txt'), row.id);
  log('three running endpoints with distinct profiles', state);
  await Promise.all(ids.map(id => viewer(id)));
  const before = await Promise.all(ids.map(snapshot)); await sleep(1200);
  const after = await Promise.all(ids.map(snapshot));
  after.forEach((s, i) => { assert.ok(s.inbound?.framesDecoded >= 3, `${ids[i]} endpoint disconnected during concurrent check`); assert.ok(s.frames > before[i].frames); report.frames[ids[i]] = { decoded: s.inbound.framesDecoded, frames: s.frames, delta: s.frames - before[i].frames, width: s.width, height: s.height }; });
  assert.ok((await api('instances')).every(c => c.viewerConnected));
  log('all three viewers decode new frames concurrently', report.frames);
  const choices = await api('instances');
  for (const choice of choices) {
    assert.equal(typeof choice.name, 'string'); assert.equal(typeof choice.profile, 'string');
    assert.equal(typeof choice.isolation, 'string'); assert.equal(choice.state, 'running'); assert.equal(choice.viewerConnected, true);
  }
  log('API provides instance IDs, labels, profiles, isolation and viewer status; legacy default and named URLs stream');
  await api('profiles/alpha', 'DELETE', undefined, 409);
  await api('instances', 'POST', { id: 'duplicate', profile: 'alpha' }, 201);
  await api('instances/duplicate/start', 'POST', undefined, 409); await api('instances/duplicate', 'DELETE', undefined, 204);
  log('running-profile exclusion and referenced-profile deletion refusal');

  // Cold release startup may retry before the first decoded frame; compare fault isolation
  // against the healthy PIDs immediately before injecting the crash.
  const preCrash = await api('instances');
  const current = preCrash.find(s => s.id === 'alpha'), all = track();
  const browser = all.find(p => p.ParentProcessId === current.pid && p.Name.toLowerCase() === 'chrome.exe' && !/--type=/.test(p.CommandLine));
  assert.ok(browser, 'owned alpha browser found'); const oldTree = browserTree(all, current.pid);
  const oldFrames = (await snapshot('alpha')).frames;
  killOwned(browser);
  await poll(async () => { const s = await api('instances/alpha'); return s.state === 'running' && s.pid !== current.pid && s.restarts >= 1; });
  await poll(async () => (await snapshot('alpha')).frames > oldFrames + 3);
  await noOldTree(oldTree); log('browser crash restarts only its endpoint, viewer recovers and old tree disappears');
  const otherState = await api('instances');
  for (const id of ['default', 'beta']) assert.equal(otherState.find(s => s.id === id).pid, preCrash.find(s => s.id === id).pid);
  log('unaffected endpoints retain their PIDs');

  const preRestart = await api('instances');
  const oldRows = track().filter(p => preRestart.some(s => browserTree([...owned.values()], s.pid).some(old => old.ProcessId === p.ProcessId)));
  // A forced CP exit also tests EOF cleanup, without taskkill masking re job ownership.
  await new Promise(resolve => { cp.once('exit', resolve); cp.kill(); });
  await noOldTree(oldRows);
  for (const c of viewers.values()) c.close(); viewers.clear();
  await startCP();
  await poll(async () => { const rows = await api('instances'); return rows.length === 3 && rows.every(c => c.state === 'running' && c.autostart); });
  assert.deepEqual((await api('instances')).map(c => c.userDataDir), preRestart.map(c => c.userDataDir));
  for (const row of await api('instances')) assert.equal(fs.readFileSync(path.join(row.userDataDir, 'multi-persistence.txt'), 'utf8'), row.id);
  // Reattach viewers to prove the relaunched media path on the same CP address.
  await Promise.all(ids.map(id => viewer(id, '-after')));
  log('forced CP restart reclaims old trees, preserves profile files and autostarts all three with decoded frames');

  const finalState = await api('instances'), finalRows = track();
  const endpointRows = finalRows.filter(p => finalState.some(s => browserTree(finalRows, s.pid).some(old => old.ProcessId === p.ProcessId)));
  await Promise.all(ids.map(id => api('instances/' + id + '/stop', 'POST')));
  assert.ok((await api('instances')).every(c => c.state === 'stopped' && c.pid === null));
  await noOldTree(endpointRows); log('stop cleanly reclaims all endpoint/browser process trees');
  for (const id of ids) { await api('instances/' + id, 'DELETE', undefined, 204); await api('profiles/' + id, 'DELETE', undefined, 204); }
  assert.deepEqual(await api('instances'), []); assert.deepEqual(await api('profiles'), []);
  assert.deepEqual(fs.readdirSync(path.join(output, 'data', 'profiles')), []);
  log('delete removes stopped instances, profile records and directories');
  report.passed = true;
} catch (e) { report.passed = false; report.error = e.stack; console.error(e.stack); process.exitCode = 1; }
finally {
  for (const c of viewers.values()) c.close();
  if (base && cp && cp.exitCode === null && cp.signalCode === null) {
    try { for (const row of await api('instances')) await api('instances/' + row.id + '/stop', 'POST'); } catch {}
  }
  track();
  for (const child of children) if (child.exitCode === null && child.signalCode === null) {
    spawnSync('taskkill.exe', ['/PID', String(child.pid), '/T', '/F'], { stdio: 'ignore', windowsHide: true, timeout: 10000 });
  }
  const live = processes();
  for (const row of owned.values()) if (live.some(p => p.ProcessId === row.ProcessId && p.Created === row.Created)) {
    try { killOwned(row); } catch (e) { report.cleanupError = e.message; }
  }
  report.finished = new Date().toISOString(); fs.writeFileSync(path.join(output, 'result.json'), JSON.stringify(report, null, 2));
  console.log('evidence: ' + output);
}
