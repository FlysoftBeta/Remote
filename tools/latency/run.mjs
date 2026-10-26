// Glass-to-glass harness runner: control-plane + mock endpoint (headless Edge/Chrome) + viewer (headless, any Chromium
// incl. Chrome 89) on this machine; prints P50/P80/P95/P99 and writes the per-frame CSV.
//   node tools/latency/run.mjs [--secs 30] [--fps 30] [--w 1920 --h 1080] [--kbps 12000] [--delay 0|off] [--mock-my <source px>] [--my <viewer override px>]
//        [--no-mock: use the real endpoint started by the CP via ENDPOINT_PATH/ENDPOINT_ARGS] [--viewer-browser <exe>] [--mock-browser <exe>] [--old-headless] [--headed] [--port 8090] [--mock-only]
//        [--viewer-window-size 1928,1162] [--validate-frame: sample VideoFrame/rVFC timestamp agreement]
//        [--netem <profile|none>: start tools/netem/relay.mjs with that profile (profiles.json), relay-only on viewer+endpoint; recorded in the report]
//        [--netem-queue-ms N] [--netem-port 3478] [--remote-cdp <port>: remote chrome started with --cr-remote-debugging-port=<port> (GPU info)]
//        [--endpoint-log <file>: log of the chain (encoder/capture lines), default C:/src/tmp/m1/cp1.log]
//        Every report JSON carries "pipeline" (render/capture/encoder/viewer/display/network) and, with --netem, "netem" (relay stats).
// Without the control-plane already running on --port it starts one. Temp profiles/output under C:\src\tmp\ptrack.
import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { pickHost } from '../netem/relay.mjs';
import { collectPipeline, pipelineLines } from './pipeline.mjs';

const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf('--' + k); return i < 0 ? d : (argv[i + 1] && !argv[i + 1].startsWith('--') ? argv[i + 1] : true); };
const repo = fileURLToPath(new URL('../../', import.meta.url));
const TMP = process.env.PTRACK_TMP || 'C:/src/tmp/ptrack';
const EDGE = 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
const port = +arg('port', 8090), secs = +arg('secs', 30), fps = +arg('fps', 30), w = +arg('w', 1920), h = +arg('h', 1080);
const kbps = +arg('kbps', 12000), delay = arg('delay', '0');
const viewerExe = arg('viewer-browser', EDGE), mockExe = arg('mock-browser', EDGE);
const base = `http://127.0.0.1:${port}`;
const netemProfile = arg('netem', null), netemPort = +arg('netem-port', 3478), netemHttp = netemPort + 1, netemHost = pickHost();
const mockMy = arg('mock-my', null), my = arg('my', null);
const markerQuery = value => value == null ? '' : '&my=' + encodeURIComponent(value);
fs.mkdirSync(TMP, { recursive: true });
const kids = [];
const cleanup = () => { for (const k of kids) { try { spawnSync('taskkill', ['/PID', String(k.pid), '/T', '/F'], { stdio: 'ignore' }); } catch {} } };
process.on('exit', cleanup); process.on('SIGINT', () => process.exit(130));

async function up() { try { return (await fetch(base + '/api/health')).ok; } catch { return false; } }
function browser(exe, name, url, oldHeadless) {
  const dir = path.join(TMP, `profile-${name}-${port}-${Date.now()}`);
  const args = [
    oldHeadless ? '--headless --no-sandbox --enable-features=NetworkServiceInProcess --remote-debugging-port=0' : (arg('headed') ? null : '--headless=new'), `--user-data-dir=${dir}`, '--no-first-run', '--no-default-browser-check',
    '--autoplay-policy=no-user-gesture-required', '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
    '--disable-backgrounding-occluded-windows', '--disable-features=WebRtcHideLocalIpsWithMdns,CalculateNativeWinOcclusion',
    '--enable-logging=stderr', '--v=0', '--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream', '--window-size=' + arg('viewer-window-size', '1920,1080'), url
  ].filter(Boolean).flatMap(a => a.startsWith('--headless --') ? a.split(' ') : [a]);
  const p = spawn(exe, args, { stdio: ['ignore', 'ignore', 'pipe'] });
  p.stderr.on('data', d => { for (const line of d.toString().split(/\r?\n/)) if (/\[(mock|viewer)\]|Uncaught|ERROR:.*(rtc|webrtc)/i.test(line)) console.log(`  ${name}| ${line.replace(/^.*?CONSOLE\(\d+\)\] /, '')}`); });
  kids.push(p);
  return p;
}

if (!(await up())) {
  const cp = spawn(process.execPath, [path.join(repo, 'control-plane/index.js'), '--port', String(port)], { stdio: ['ignore', 'inherit', 'inherit'] });
  kids.push(cp);
  for (let i = 0; i < 50 && !(await up()); i++) await new Promise(r => setTimeout(r, 100));
}
let relayProc = null;
if (netemProfile) {
  const ra = ['--profile', String(netemProfile), '--host', netemHost, '--port', String(netemPort), '--http', String(netemHttp)];
  if (arg('netem-queue-ms')) ra.push('--queue-ms', String(arg('netem-queue-ms')));
  relayProc = spawn(process.execPath, [path.join(repo, 'tools/netem/relay.mjs'), ...ra], { stdio: ['ignore', 'inherit', 'inherit'] });
  kids.push(relayProc);
  for (let i = 0; i < 50; i++) { try { if ((await fetch(`http://${netemHost}:${netemHttp}/stats`)).ok) break; } catch {} await new Promise(r => setTimeout(r, 100)); }
}
const relayQuery = netemProfile ? '&relay=' + encodeURIComponent(`turn:${netemHost}:${netemPort}`) : '';
const before = (await (await fetch(base + '/api/report')).json()).length;
if (!arg('no-mock')) browser(mockExe, 'mock', `${base}/mock?w=${w}&h=${h}&fps=${fps}&kbps=${kbps}${markerQuery(mockMy)}`);
if (arg('mock-only')) { console.log(`mock endpoint running; open ${base}/?measure=1`); await new Promise(() => {}); }
const inputQuery = arg('no-mock') ? 'autoinput=0&i2p=colour' : 'autoinput=1';
browser(viewerExe, 'viewer', `${base}/?measure=1&${inputQuery}&report=${secs}&delay=${delay}&hwinfo=1${relayQuery}${markerQuery(my)}${arg('validate-frame') ? '&validateFrame=1' : ''}`, !!arg('old-headless'));
console.log(`measuring ${secs}s (+warm-up) ${w}x${h}@${fps} ${kbps} kbps, viewer=${path.basename(path.dirname(path.dirname(viewerExe)))}/${path.basename(viewerExe)}`);

const deadline = Date.now() + (secs + 60) * 1000;
let report = null;
const netemSamples = [];
while (Date.now() < deadline) {
  await new Promise(r => setTimeout(r, 1000));
  if (netemProfile) {
    try { netemSamples.push({ at: Date.now(), stats: await (await fetch(`http://${netemHost}:${netemHttp}/stats`)).json() }); } catch {}
  }
  const all = await (await fetch(base + '/api/report')).json();
  if (all.length > before) { report = all[all.length - 1]; break; }
}
if (!report) { console.error('no report received (viewer did not connect or marker not decoded)'); process.exit(1); }
report.netem = null;
if (netemProfile) { try { report.netem = await (await fetch(`http://${netemHost}:${netemHttp}/stats`)).json(); } catch (e) { report.netem = { error: String(e) }; } }
if (netemSamples.length) {
  const start = report.summary.measurementStartWall;
  const first = netemSamples.reduce((a, b) => Math.abs(b.at - start) < Math.abs(a.at - start) ? b : a);
  const last = netemSamples[netemSamples.length - 1];
  const bytes = sample => sample.stats.allocations.filter(a => a.user === 'endpoint').reduce((sum, a) => sum + a.sentBytes, 0);
  report.senderEgress = { source: 'TURN allocation username=endpoint sentBytes; endpoint payload egress including media, retransmissions, RTCP and DataChannels, excluding TURN/IP/UDP framing',
    startWall: first.at, endWall: last.at, durationSeconds: (last.at - first.at) / 1000, bytes: bytes(last) - bytes(first),
    meanMbps: (bytes(last) - bytes(first)) * 8 / (last.at - first.at) / 1000,
    note: 'Nearest 1 Hz relay snapshot to viewer measurement start; relay drops/max queue are full-connection counters including warmup' };
  report.netemSamples = netemSamples;
}
report.pipeline = { ...(report.pipeline || {}), ...(await collectPipeline({ report, cdpPort: arg('remote-cdp', null), endpointLog: arg('endpoint-log', 'C:/src/tmp/m1/cp1.log'),
  headed: !!arg('headed'), viewerExe, windowSize: arg('viewer-window-size', '1920,1080'), netemProfile })) };
const s = report.summary, f = v => v == null ? '-' : v.toFixed(1);
const stamp = new Date().toISOString().replace(/[:.]/g, '-');
const csv = path.join(TMP, `latency-${stamp}.csv`);
fs.writeFileSync(csv, report.csv);
delete report.csv;
fs.writeFileSync(csv.replace(/\.csv$/, '.json'), JSON.stringify(report, null, 2));
console.log(`viewer UA: ${report.ua}`);
console.log(`hint: ${report.hint}; codec ${report.stats?.codec} ${report.stats?.decoder} ${report.stats?.frameWidth}x${report.stats?.frameHeight}; sender ${report.sender?.backend}`);
console.log(`frames ${s.frames}, marker decoded ${s.decoded}, failed ${s.failed}, dupes ${s.dupes}, skipped ${s.skipped}; clock offset ${f(s.offset)} ms, min RTT ${s.minRtt?.toFixed(2)} ms`);
console.log(`marker row ${s.markerY ?? 'unknown'} source px; alignment scans ${s.markerScans ?? 'unknown'}`);
console.log(`capture->display ms: P50 ${f(s.lat.p50)}  P80 ${f(s.lat.p80)}  P95 ${f(s.lat.p95)}  P99 ${f(s.lat.p99)}  max ${f(s.lat.max)}  (n=${s.lat.n})`);
if (s.rx && s.rx.n) console.log(`capture->received ms: P50 ${f(s.rx.p50)}  P80 ${f(s.rx.p80)}  P99 ${f(s.rx.p99)}  (rVFC receiveTime, n=${s.rx.n})`);
if (s.i2p.n) console.log(`${arg('no-mock') ? 'input->decoded flip' : 'input->display estimate'} ms: P50 ${f(s.i2p.p50)}  P80 ${f(s.i2p.p80)}  P95 ${f(s.i2p.p95)}  P99 ${f(s.i2p.p99)}  (n=${s.i2p.n}, ${s.inputMethod}, attempts=${s.inputAttempts}, timeouts=${s.inputTimeouts})`);
console.log(`jitter buffer ${f(report.stats?.derived?.jbMs)} ms, decode ${f(report.stats?.derived?.decMs)} ms, fps ${f(report.stats?.derived?.fps)}`);
if (report.senderEgress) console.log(`endpoint send ${f(report.senderEgress.meanMbps)} Mbps; freezes >200ms ${s.freezes?.count}`);
for (const l of pipelineLines(report)) console.log(l);
console.log(`csv: ${csv}`);
process.exit(0);
