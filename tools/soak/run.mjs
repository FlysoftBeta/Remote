import fs from 'node:fs';
import path from 'node:path';
import net from 'node:net';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { CDP, sleep, processes, killOwned, killOwnedMany, idleBuild, json, slope } from './support.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const names = ['long-run', 'reconnect', 'browser-crash', 'endpoint-crash', 'gpu-restart', 'concurrent', 'resize'];
const argv = process.argv.slice(2);
const flag = k => argv.includes('--' + k);
function arg(k, fallback) {
  const i = argv.indexOf('--' + k);
  if (i < 0) return fallback;
  if (!argv[i + 1] || argv[i + 1].startsWith('--')) throw new Error(`--${k} needs a value`);
  return argv[i + 1];
}
function num(k, fallback, min = 0.1, max = 1e7) {
  const n = Number(arg(k, fallback));
  if (!Number.isFinite(n) || n < min || n > max) throw new Error(`invalid --${k}`);
  return n;
}
if (flag('help')) {
  console.log(`node tools/soak/run.mjs --all | ${names.map(n => '--' + n).join(' | ')}
ENDPOINT_PATH/ENDPOINT_ARGS select the endpoint. See tools/soak/README.md.
--long-secs 5400 --sample-secs 30 --resize-secs 60 --reconnects 5
--sessions 3 --concurrent-secs 30 --recovery-secs 30 --startup-secs 90
--scenario S|V --port 8400 --out <new-directory> --smoke`);
  process.exit(0);
}
const smoke = flag('smoke');
const selected = names.filter(n => flag('all') || flag(n));
if (!selected.length) throw new Error('select scenario flags or --all (see --help)');
if (!process.env.ENDPOINT_PATH) throw new Error('ENDPOINT_PATH is required; mock runs are not soak checks');
const opts = {
  longSecs: num('long-secs', smoke ? 16 : 5400), sampleSecs: num('sample-secs', smoke ? 4 : 30),
  resizeSecs: num('resize-secs', smoke ? 4 : 60), reconnects: num('reconnects', smoke ? 1 : 5, 1, 1000),
  sessions: num('sessions', smoke ? 2 : 3, 2, 3), concurrentSecs: num('concurrent-secs', smoke ? 5 : 30),
  recoverySecs: num('recovery-secs', 30), startupSecs: num('startup-secs', smoke ? 45 : 90),
  scenario: arg('scenario', 'S'), port: num('port', 8400, 1024, 63515),
  maxPrivateMiBHour: num('max-private-mib-hour', 100, 0), maxHandlesHour: num('max-handles-hour', 100, 0),
  maxFreezes: num('max-freezes', 0, 0), minFps: num('min-fps', 1),
  budgetSecs: num('budget-secs', smoke ? 540 : 7200), smoke
};
for (const k of ['reconnects', 'sessions', 'port']) if (!Number.isInteger(opts[k])) throw new Error(`${k} must be an integer`);
if (!['S', 'V'].includes(opts.scenario)) throw new Error('--scenario must be S or V');
const output = path.resolve(arg('out', `C:/src/tmp/m6/soak/run-${new Date().toISOString().replace(/[:.]/g, '-')}`));
fs.mkdirSync(output, { recursive: true });
const reportPath = path.join(output, 'soak.json');
if (fs.existsSync(reportPath)) throw new Error('output already has soak.json; use a new directory');
const report = { schema: 1, started: new Date().toISOString(), endpoint: path.resolve(process.env.ENDPOINT_PATH),
  options: opts, buildProcesses: [], scenarios: [], limitations: [
    'GPU memory is not sampled: no cheap reliable per-process counter is assumed; private bytes and handles include endpoint and browser trees separately.',
    'Decoded latency is source pixel marker to rVFC presentationTime using the imported viewer clock; it is not physical scanout latency.',
    'Marker failures are pixel-marker decode failures; inbound framesDropped/packetsLost are separate diagnostics, not a count of undecodable H264 frames.',
    'Concurrent mode uses independent control-planes, endpoint instances, browser profiles and ports; shared-control-plane multi-instance launching is not implemented by the current control-plane.'
  ] };
const active = new Set();
let serial = 0;
const globalStart = Date.now();
const budget = setTimeout(() => { report.interrupted = 'wall-clock budget exceeded'; void shutdown(124); }, opts.budgetSecs * 1000);
function write() {
  report.updated = new Date().toISOString();
  fs.writeFileSync(reportPath, JSON.stringify(report, null, 2));
  const rows = report.scenarios.map(r => `| ${r.name} | ${r.status} | ${r.criterion.replace(/\|/g, '/')} | ${tableNumbers(r).replace(/\|/g, '/').replace(/\r?\n/g, ' ')} |`);
  fs.writeFileSync(path.join(output, 'soak.md'), `# Soak ${report.started}\n\nEndpoint: ${report.endpoint}\n\n| Scenario | Result | Criterion | Numbers |\n|---|---|---|---|\n${rows.join('\n')}\n\nFull samples, process identities, build priority and limits: soak.json.\n`);
}
const fmt = n => n == null ? 'n/a' : Number(n).toFixed(1);
function tableNumbers(r) {
  if (!r.numbers) return r.error?.split('\n')[0] || '';
  const n = r.numbers;
  if (r.name === 'long-run') return `${fmt(n.seconds)}s, min ${fmt(n.minFps)} fps, ${n.freezesOver500ms} freezes; endpoint/chrome MiB/h ${fmt(n.slopes.endpoint.privateMiBHour)}/${fmt(n.slopes.chrome.privateMiBHour)}, handles/h ${fmt(n.slopes.endpoint.handlesHour)}/${fmt(n.slopes.chrome.handlesHour)}; leak assessed=${n.leakAssessed}`;
  if (r.name === 'reconnect') return `first frame ms: ${n.attempts.map(a => fmt(a.firstFrameMs)).join(', ')}`;
  if (r.name === 'concurrent') return n.sessions.map(s => `port ${s.port}: ${fmt(s.fps)} fps, +${s.frameDelta} frames`).join('; ');
  if (r.name === 'resize') return `${n.resizes} resizes, ${n.width}x${n.height} (expected ${n.expectedWidth}x${n.expectedHeight}), recovery ${fmt(n.recoveryMs)}ms, marker failures ${n.markerFailures}`;
  return `auto recovery ${fmt(n.automaticRecoveryMs)}ms, freezes ${n.freezesOver500ms}, marker failures ${n.markerFailuresDuringFault}, dropped delta ${n.framesDroppedDelta}; ${n.replacementGpuPid !== undefined ? 'new GPU PID ' + n.replacementGpuPid : 'automatic'}`;
}
async function freePort(port) {
  await new Promise((resolve, reject) => {
    const server = net.createServer();
    server.once('error', reject);
    server.listen(port, '127.0.0.1', () => server.close(resolve));
  });
}
const probe = fs.readFileSync(path.join(here, 'viewer-probe.js'), 'utf8');
class Session {
  constructor(slot, label) {
    this.port = opts.port + slot * 10; this.viewerPort = this.port + 1000; this.remotePort = this.port + 2000;
    this.base = `http://127.0.0.1:${this.port}`;
    this.dir = path.join(output, `${++serial}-${label}`);
    fs.mkdirSync(this.dir, { recursive: true });
    this.owned = new Map(); this.samples = []; this.cdp = null; this.worker = null;
  }
  async start() {
    active.add(this);
    await Promise.all([this.port, this.viewerPort, this.remotePort].map(freePort));
    const args = process.env.ENDPOINT_ARGS_JSON ? JSON.parse(process.env.ENDPOINT_ARGS_JSON) : (process.env.ENDPOINT_ARGS || '').split(' ').filter(Boolean);
    if (!Array.isArray(args) || args.some(a => typeof a !== 'string')) throw new Error('invalid ENDPOINT_ARGS_JSON');
    // Every instance must own its browser profile/CDP socket and navigate its own fixture.
    const extra = args.filter(a => !/^--(?:user-data-dir|cr-remote-debugging-port|url)=/.test(a));
    if (args.some(a => /^--(?:user-data-dir|cr-remote-debugging-port|url)$/.test(a))) throw new Error('use --key=value endpoint arguments');
    extra.push(`--user-data-dir=${path.join(this.dir, 'browser-profile')}`, `--cr-remote-debugging-port=${this.remotePort}`,
      `--url=${this.base}/tools/latency/remote-page.html?scenario=${opts.scenario}`);
    this.endpointArgs = extra;
    const fd = fs.openSync(path.join(this.dir, 'chain.log'), 'a');
    try {
      this.worker = spawn(process.execPath, [path.join(here, 'launch.mjs'), '--no-mock', '--secs', '10000000',
        '--port', String(this.port), '--viewer-flags', `--remote-debugging-port=${this.viewerPort}`,
        '--viewer-exe', arg('viewer-exe', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe')], {
        env: { ...process.env, ENDPOINT_ARGS_JSON: JSON.stringify(extra), PTRACK_TMP: this.dir },
        windowsHide: true, stdio: ['ignore', fd, fd]
      });
    } finally { fs.closeSync(fd); }
    this.worker.on('error', e => { this.launchError = e.message; });
    const start = Date.now();
    while (Date.now() - start < opts.startupSecs * 1000) {
      this.refresh();
      if (this.launchError || this.worker.exitCode !== null) throw new Error(`launcher failed: ${this.launchError || this.worker.exitCode}`);
      try {
        if (!this.cdp) await this.attach();
        const s = await this.snapshot();
        if (s.frames >= 3 && s.decodedLatencyMs?.n > 0) {
          this.firstFrameMs = Date.now() - start;
          // Let the latency viewer's initial 3 s warmup reset finish before window sampling.
          await sleep(3200);
          await this.snapshot(true);
          return this;
        }
      } catch { this.cdp?.close(); this.cdp = null; }
      await sleep(300);
    }
    throw new Error(`startup: no decoded marker within ${opts.startupSecs}s; see ${this.dir}/chain.log`);
  }
  refresh() {
    const all = processes();
    const ids = new Set();
    for (const row of all) {
      const old = this.owned.get(row.ProcessId);
      if (row.ProcessId === this.worker?.pid && !this.rootCreated && this.worker.exitCode === null) this.rootCreated = row.Created;
      if (old?.Created === row.Created || (row.ProcessId === this.worker?.pid && row.Created === this.rootCreated)) ids.add(row.ProcessId);
    }
    let changed = true;
    while (changed) {
      changed = false;
      for (const row of all) if (!ids.has(row.ProcessId) && ids.has(row.ParentProcessId)) { ids.add(row.ProcessId); changed = true; }
    }
    this.live = all.filter(r => ids.has(r.ProcessId));
    for (const row of this.live) this.owned.set(row.ProcessId, row);
    return this.live;
  }
  async attach() {
    const pages = await json(`http://127.0.0.1:${this.viewerPort}/json/list`);
    const page = pages.find(p => p.type === 'page' && p.url.startsWith(this.base + '/'));
    if (!page) throw new Error('viewer target not ready');
    this.targetId = page.id; this.viewerUrl = page.url;
    this.cdp = await CDP.connect(page.webSocketDebuggerUrl);
    await this.cdp.evaluate(probe);
  }
  async snapshot(drain = false) {
    await this.cdp.evaluate(probe);
    return this.cdp.evaluate(`window.soakSnapshot(${drain})`);
  }
  async sample() {
    const s = await this.snapshot(true);
    const live = this.refresh();
    const endpoint = live.find(r => r.ExecutablePath?.toLowerCase() === report.endpoint.toLowerCase());
    const descendants = root => {
      const ids = new Set(root ? [root.ProcessId] : []);
      for (let i = 0; i < live.length; i++) for (const p of live) if (ids.has(p.ParentProcessId)) ids.add(p.ProcessId);
      return live.filter(r => ids.has(r.ProcessId));
    };
    const browser = this.browser();
    const chromeRows = descendants(browser);
    const metric = rows => rows.length ? {
      privateMiB: rows.reduce((a, p) => a + Number(p.PrivatePageCount), 0) / 1048576,
      handles: rows.reduce((a, p) => a + Number(p.HandleCount), 0), pids: rows.map(p => p.ProcessId)
    } : null;
    s.processes = { endpoint: metric(endpoint ? [endpoint] : []), chrome: metric(chromeRows) };
    const prev = this.samples.at(-1);
    const dt = prev ? (s.at - prev.at) / 1000 : null;
    const fd = prev && s.inbound && prev.inbound ? s.inbound.framesDecoded - prev.inbound.framesDecoded : null;
    const bytes = prev && s.inbound && prev.inbound ? s.inbound.bytesReceived - prev.inbound.bytesReceived : null;
    s.window = { seconds: dt, fps: dt && fd >= 0 ? fd / dt : null, kbps: dt && bytes >= 0 ? bytes * 8 / dt / 1000 : null };
    this.samples.push(s);
    fs.appendFileSync(path.join(this.dir, 'samples.jsonl'), JSON.stringify(s) + '\n');
    return s;
  }
  browser() {
    const chrome = this.endpointArgs.find(a => a.startsWith('--chrome='))?.slice('--chrome='.length);
    return this.live?.find(r => r.ExecutablePath?.toLowerCase() === path.resolve(chrome || 'chrome.exe').toLowerCase()
      && r.CommandLine?.replace(/"/g, '').includes(`--user-data-dir=${path.join(this.dir, 'browser-profile')}`) && !/--type=/.test(r.CommandLine));
  }
  async progress(timeout = opts.recoverySecs, baseline = null) {
    const base = baseline ?? (await this.snapshot()).frames;
    const start = Date.now();
    while (Date.now() - start < timeout * 1000) {
      const s = await this.snapshot();
      // Ignore a handful of buffered frames immediately after a process dies.
      if (Date.now() - start >= 1000 && s.frames >= base + 3 && s.lastFrameAgeMs < 500) return { ms: Date.now() - start, sample: s };
      await sleep(100);
    }
    return null;
  }
  async stop() {
    this.cdp?.close(); this.cdp = null;
    if (this.worker) {
      try { this.refresh(); } catch (e) { report.cleanupErrors ??= []; report.cleanupErrors.push(String(e)); }
      // Stop launcher/CP/endpoint before remaining descendants to prevent respawn.
      const rows = [...this.owned.values()];
      const ordered = rows.sort((a, b) => (a.ProcessId === this.worker.pid ? -1 : b.ProcessId === this.worker.pid ? 1 : 0));
      try { killOwnedMany(ordered); } catch (e) { report.cleanupErrors ??= []; report.cleanupErrors.push(String(e)); }
    }
    active.delete(this);
  }
}

function leaks(samples) {
  return Object.fromEntries(['endpoint', 'chrome'].map(group => {
    const rows = samples.map(s => ({ at: s.at, ...s.processes[group] }));
    return [group, { privateMiBHour: slope(rows, 'privateMiB'), handlesHour: slope(rows, 'handles') }];
  }));
}
async function sampleFor(session, secs) {
  const start = Date.now();
  await session.sample();
  let next = opts.sampleSecs * 1000;
  while (Date.now() - start < secs * 1000) {
    await sleep(Math.max(1, Math.min(next, secs * 1000) - (Date.now() - start)));
    await session.sample();
    next += opts.sampleSecs * 1000;
    write();
  }
}
async function scenario(name) {
  const r = { name, status: 'running', criterion: '', started: new Date().toISOString() };
  report.scenarios.push(r); write();
  console.log(`Starting ${name}`);
  try {
    const s = await new Session(0, name).start();
    r.launch = { dir: s.dir, firstFrameMs: s.firstFrameMs, endpointArgs: s.endpointArgs };
    if (name === 'long-run') {
      r.criterion = `each window >=${opts.minFps} fps; freezes >500ms <=${opts.maxFreezes}; 15+ min slopes <=${opts.maxPrivateMiBHour} MiB/h and ${opts.maxHandlesHour} handles/h per group`;
      await sampleFor(s, opts.longSecs);
      const slopes = leaks(s.samples), windows = s.samples.slice(1);
      const observedSeconds = (s.samples.at(-1).at - s.samples[0].at) / 1000;
      const leakAssessed = observedSeconds >= 900 && s.samples.length >= 3;
      const freezes = s.samples.at(-1).freezesOver500ms - s.samples[0].freezesOver500ms;
      const decodePass = windows.length > 0 && windows.every(w => w.window.fps >= opts.minFps && w.decodedLatencyMs?.n > 0) && freezes <= opts.maxFreezes;
      const resourcesPresent = s.samples.every(w => w.processes.endpoint && w.processes.chrome);
      const leakPass = Object.values(slopes).every(v => v.privateMiBHour !== null && v.handlesHour !== null && v.privateMiBHour <= opts.maxPrivateMiBHour && v.handlesHour <= opts.maxHandlesHour);
      r.numbers = { seconds: observedSeconds, windows: windows.length, freezesOver500ms: freezes,
        minFps: Math.min(...windows.map(w => w.window.fps)), slopes, leakAssessed };
      r.status = !decodePass || !resourcesPresent || (leakAssessed && !leakPass) ? 'fail' : leakAssessed ? 'pass' : 'partial';
      if (!resourcesPresent) r.error = 'Missing endpoint/browser process metrics';
      r.samples = s.samples;
    } else if (name === 'reconnect') {
      r.criterion = `close/reopen viewer tab ${opts.reconnects} times; each first decoded frame <=${opts.recoverySecs}s`;
      const attempts = [];
      for (let i = 0; i < opts.reconnects; i++) {
        const v = await json(`http://127.0.0.1:${s.viewerPort}/json/version`);
        const b = await CDP.connect(v.webSocketDebuggerUrl);
        let attempt;
        const start = Date.now();
        try {
          const old = s.targetId;
          const { targetId } = await b.call('Target.createTarget', { url: 'about:blank' });
          s.cdp.close(); s.cdp = null;
          await b.call('Target.closeTarget', { targetId: old });
          // Navigate only after closing the old viewer so CP sees a disconnect.
          const pages = await json(`http://127.0.0.1:${s.viewerPort}/json/list`);
          const page = await CDP.connect(pages.find(p => p.id === targetId).webSocketDebuggerUrl);
          try { await page.call('Page.navigate', { url: s.viewerUrl }); } finally { page.close(); }
          while (Date.now() - start < opts.recoverySecs * 1000) {
            try { if (!s.cdp) await s.attach(); if ((await s.snapshot()).frames >= 1) { attempt = { firstFrameMs: Date.now() - start }; break; } } catch { s.cdp?.close(); s.cdp = null; }
            await sleep(100);
          }
        } finally { b.close(); }
        attempts.push(attempt || { firstFrameMs: null, error: 'no first frame before deadline' });
        if (!attempt) break;
      }
      r.numbers = { attempts }; r.status = attempts.length === opts.reconnects && attempts.every(a => a.firstFrameMs !== null) ? 'pass' : 'fail';
    } else if (name === 'browser-crash' || name === 'endpoint-crash' || name === 'gpu-restart') {
      const before = await s.sample();
      const target = name === 'browser-crash' ? s.browser() : name === 'endpoint-crash'
        ? s.live.find(p => p.ExecutablePath?.toLowerCase() === report.endpoint.toLowerCase())
        : s.live.find(p => /--type=gpu-process/.test(p.CommandLine || '') && p.Name.toLowerCase() === 'chrome.exe');
      if (!target) throw new Error('owned fault target not found');
      r.faultTarget = target;
      const killedAt = Date.now(); killOwned(target);
      const postKillFrames = (await s.snapshot()).frames;
      const recovery = await s.progress(opts.recoverySecs, postKillFrames);
      const health = await json(s.base + '/api/health');
      const after = await s.sample();
      const log = fs.readFileSync(path.join(s.dir, 'chain.log'), 'utf8');
      const diagnostics = log.split(/\r?\n/).filter(l => /exit|disconnect|crash|endpoint-gone|no-endpoint|browser.*(lost|closed)|mojo.*(lost|closed)/i.test(l)).slice(-15);
      r.numbers = { killedPid: target.ProcessId, observationMs: Date.now() - killedAt, automaticRecoveryMs: recovery?.ms ?? null,
        markerFailuresDuringFault: after.markerFailures, framesDroppedDelta: (after.inbound?.framesDropped ?? 0) - (before.inbound?.framesDropped ?? 0),
        undecodableH264Frames: null, health, diagnostics, freezesOver500ms: after.freezesOver500ms - before.freezesOver500ms };
      if (name === 'gpu-restart') {
        r.criterion = `owned GPU process replaced and 3 new viewer frames within ${opts.recoverySecs}s`;
        const replacement = s.live.find(p => /--type=gpu-process/.test(p.CommandLine || '') && p.Name.toLowerCase() === 'chrome.exe' && p.ProcessId !== target.ProcessId);
        r.numbers.replacementGpuPid = replacement?.ProcessId ?? null;
        r.status = recovery && replacement ? 'pass' : 'fail';
      } else {
        // The control plane restarts the endpoint (backoff) and the viewer renegotiates on its own.
        r.criterion = `${name === 'browser-crash' ? 'browser' : 'endpoint'} killed: new viewer frames without operator action within ${opts.recoverySecs}s`;
        r.status = recovery && health.instances.some(i => i.endpoint && i.session) ? 'pass' : 'fail';
      }
      r.samples = s.samples;
    } else if (name === 'concurrent') {
      r.criterion = `${opts.sessions} separate CP/endpoint/profile/port sessions each continue decoding for ${opts.concurrentSecs}s`;
      const sessions = [s];
      for (let i = 1; i < opts.sessions; i++) sessions.push(await new Session(i, `concurrent-${i + 1}`).start());
      const before = await Promise.all(sessions.map(t => t.sample()));
      await sleep(opts.concurrentSecs * 1000);
      const after = await Promise.all(sessions.map(t => t.sample()));
      r.numbers = { sessions: sessions.map((t, i) => ({ port: t.port, dir: t.dir, frameDelta: after[i].frames - before[i].frames,
        fps: after[i].window.fps, markerSamples: after[i].decodedLatencyMs?.n ?? 0 })) };
      r.status = r.numbers.sessions.every(t => t.frameDelta > 0 && t.fps >= opts.minFps && t.markerSamples > 0) ? 'pass' : 'fail';
    } else if (name === 'resize') {
      r.criterion = `${opts.resizeSecs}s viewport storm at ~10 Hz; final decoded dimensions equal final video rectangle and 3 new frames within ${opts.recoverySecs}s`;
      await s.sample();
      let count = 0;
      const start = Date.now();
      while (Date.now() - start < opts.resizeSecs * 1000) {
        await s.cdp.call('Emulation.setDeviceMetricsOverride', { width: count % 2 ? 1280 : 960, height: count % 2 ? 720 : 540, deviceScaleFactor: 1, mobile: false });
        count++; await sleep(100);
      }
      await s.cdp.call('Emulation.setDeviceMetricsOverride', { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
      const base = (await s.snapshot()).frames;
      let final = null;
      const settle = Date.now();
      while (Date.now() - settle < opts.recoverySecs * 1000) {
        const a = await s.snapshot();
        if (a.frames >= base + 3 && a.width === a.expectedWidth && a.height === a.expectedHeight) { final = a; break; }
        await sleep(100);
      }
      const after = await s.sample();
      r.numbers = { resizes: count + 1, recoveryMs: final ? Date.now() - settle : null, width: after.width, height: after.height,
        expectedWidth: after.expectedWidth, expectedHeight: after.expectedHeight, markerFailures: after.markerFailures };
      r.samples = s.samples; r.status = final ? 'pass' : 'fail';
    }
  } catch (e) { r.status = 'fail'; r.error = String(e.stack || e); r.criterion ||= 'scenario starts and completes without harness errors'; }
  finally {
    for (const s of [...active]) await s.stop();
    r.finished = new Date().toISOString(); write();
    console.log(`${name}: ${r.status}`);
  }
}
let shuttingDown = false;
async function shutdown(code) {
  if (shuttingDown) return;
  shuttingDown = true; clearTimeout(budget);
  for (const s of [...active]) await s.stop();
  report.finished = new Date().toISOString(); report.elapsedSeconds = (Date.now() - globalStart) / 1000;
  if (report.interrupted) for (const r of report.scenarios) if (r.status === 'running') { r.status = 'fail'; r.error = report.interrupted; }
  write(); console.log(`Reports: ${output}`); process.exit(code);
}
process.on('SIGINT', () => { report.interrupted = 'SIGINT'; void shutdown(130); });
process.on('SIGTERM', () => { report.interrupted = 'SIGTERM'; void shutdown(143); });
try {
  report.buildProcesses = idleBuild(); write();
  for (const name of selected) await scenario(name);
  await shutdown(report.scenarios.some(r => r.status === 'fail') || report.cleanupErrors?.length ? 1 : 0);
} catch (e) { report.error = String(e.stack || e); await shutdown(1); }
