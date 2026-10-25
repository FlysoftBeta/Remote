// remotekit control-plane sample (D13, design/06-protocol.md section 1): static files + WebSocket signalling + sessions.
// Not on the media/input path. Run: node control-plane [--port 8080]; env CP_TOKEN (auth placeholder), ENDPOINT_PATH (stdio endpoint).
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { WebSocketServer } from 'ws';
import { spawnEndpoint } from './endpoint.js';

const V = 1;
const repo = fileURLToPath(new URL('../', import.meta.url));
const roots = { '/tools/': path.join(repo, 'tools'), '/': path.join(repo, 'viewer') };
const types = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.json': 'application/json' };
const TOKEN = process.env.CP_TOKEN || '';
const argPort = process.argv.indexOf('--port');
const PORT = Number(argPort > 0 ? process.argv[argPort + 1] : process.env.PORT ?? 8080);
const log = (...a) => console.log(new Date().toISOString().slice(11, 23), ...a);

// instance id -> { endpoint: conn|null, viewer: conn|null (current controller), sid }
const instances = new Map();
const inst = id => { if (!instances.has(id)) instances.set(id, { endpoint: null, viewer: null, sid: null }); return instances.get(id); };
const reports = [];

const FROM_VIEWER = new Set(['answer', 'ice', 'restart', 'bye']);
const FROM_ENDPOINT = new Set(['offer', 'ice', 'bye']);

function startSession(id) {
  const I = inst(id);
  if (!I.endpoint || !I.viewer || I.sid) return;
  I.sid = crypto.randomBytes(16).toString('hex');
  I.viewer.sid = I.sid;
  I.viewer.send({ t: 'welcome', v: V, sid: I.sid, instance: id, iceServers: [] });
  I.endpoint.send({ t: 'session.open', sid: I.sid, viewer: { caps: I.viewer.caps, ua: I.viewer.ua }, iceServers: [] });
  log(`session ${I.sid.slice(0, 8)} open on ${id}`);
}
function endSession(id, reason, notify) {
  const I = inst(id);
  if (!I.sid) return;
  const sid = I.sid;
  I.sid = null;
  if (notify !== 'endpoint') I.endpoint?.send({ t: 'bye', sid, reason });
  if (notify !== 'viewer' && I.viewer) { I.viewer.send({ t: 'bye', sid, reason }); I.viewer.sid = null; }
  log(`session ${sid.slice(0, 8)} closed on ${id}: ${reason}`);
}

function onHello(conn, m) {
  if (m.v !== V) { conn.send({ t: 'error', code: 'version', message: `need v${V}` }); return conn.close(4400); }
  const I = inst(conn.instance);
  if (conn.role === 'endpoint') {
    if (I.endpoint && I.endpoint !== conn) { endSession(conn.instance, 'endpoint-replaced', 'viewer'); I.endpoint.close(4409); }
    I.endpoint = conn;
    conn.name = m.name;
    log(`endpoint ${m.name || '?'} registered for ${conn.instance}`);
    startSession(conn.instance);
  } else {
    conn.caps = m.caps || {};
    conn.ua = String(m.ua || '').slice(0, 200);
    if (I.viewer && I.viewer !== conn) {
      const old = I.viewer;
      endSession(conn.instance, 'replaced');
      old.close(4000);
    }
    I.viewer = conn;
    if (!I.endpoint) conn.send({ t: 'error', code: 'no-endpoint', message: 'waiting for endpoint' });
    startSession(conn.instance);
  }
}

function onMessage(conn, m) {
  if (!m || typeof m !== 'object' || typeof m.t !== 'string') return conn.close(4400);
  if (m.t === 'hello') return onHello(conn, m);
  const I = inst(conn.instance);
  if (conn.role === 'viewer') {
    if (I.viewer !== conn || !I.sid || !FROM_VIEWER.has(m.t)) return;
    m.sid = I.sid;
    I.endpoint?.send(m);
    if (m.t === 'bye') { I.sid = null; conn.sid = null; log(`session closed by viewer: ${m.reason}`); }
  } else {
    if (I.endpoint !== conn || !FROM_ENDPOINT.has(m.t) || m.sid !== I.sid || !I.viewer) return;
    I.viewer.send(m);
    if (m.t === 'bye') { I.sid = null; I.viewer.sid = null; log(`session closed by endpoint: ${m.reason}`); }
  }
}

function onClose(conn) {
  const I = inst(conn.instance);
  if (conn.role === 'viewer' && I.viewer === conn) { endSession(conn.instance, 'viewer-gone', 'viewer'); I.viewer = null; }
  if (conn.role === 'endpoint' && I.endpoint === conn) {
    endSession(conn.instance, 'endpoint-gone', 'endpoint'); I.endpoint = null;
    I.viewer?.send({ t: 'error', code: 'no-endpoint', message: 'endpoint disconnected' });
  }
}

// ---- HTTP: static viewer/tools, health, latency reports ----
function readBody(req, limit = 1 << 20) {
  return new Promise((resolve, reject) => {
    let n = 0; const parts = [];
    req.on('data', c => { n += c.length; if (n > limit) { reject(new Error('too large')); req.destroy(); } else parts.push(c); });
    req.on('end', () => resolve(Buffer.concat(parts).toString('utf8')));
    req.on('error', reject);
  });
}
const json = (res, code, obj) => { res.writeHead(code, { 'content-type': 'application/json' }); res.end(JSON.stringify(obj)); };

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://x');
  if (url.pathname === '/api/health') {
    return json(res, 200, { ok: true, v: V, instances: [...instances].map(([id, I]) => ({ id, endpoint: I.endpoint?.name ?? null, viewer: !!I.viewer, session: !!I.sid })) });
  }
  if (url.pathname === '/api/report') {
    if (req.method === 'POST') {
      try { const r = JSON.parse(await readBody(req)); r.at = new Date().toISOString(); reports.push(r); if (reports.length > 50) reports.shift();
        if (process.env.REPORT_FILE) fs.appendFileSync(process.env.REPORT_FILE, JSON.stringify(r) + '\n');
        log(`report: ${r.kind || '?'} n=${r.n ?? '?'}`); return json(res, 200, { ok: true });
      } catch (e) { return json(res, 400, { error: e.message }); }
    }
    return json(res, 200, reports);
  }
  let p = url.pathname === '/' ? '/index.html' : url.pathname === '/mock' ? '/tools/mock-endpoint/index.html' : url.pathname;
  const prefix = p.startsWith('/tools/') ? '/tools/' : '/';
  const base = roots[prefix];
  const file = path.join(base, path.normalize(decodeURIComponent(p.slice(prefix.length))));
  if (!file.startsWith(base)) { res.writeHead(403); return res.end(); }
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404); return res.end(); }
    res.writeHead(200, { 'content-type': types[path.extname(file)] || 'application/octet-stream', 'cache-control': 'no-store' });
    res.end(data);
  });
});

const wss = new WebSocketServer({ server, path: '/signal', maxPayload: 64 * 1024 });
wss.on('connection', (ws, req) => {
  const q = new URL(req.url, 'http://x').searchParams;
  const role = q.get('role') === 'endpoint' ? 'endpoint' : 'viewer';
  if (TOKEN && q.get('token') !== TOKEN) { ws.send(JSON.stringify({ t: 'error', code: 'auth' })); return ws.close(4401); }
  const conn = {
    role, instance: (q.get('instance') || 'default').slice(0, 64), alive: true,
    send: m => { if (ws.readyState === 1) ws.send(JSON.stringify(m)); },
    close: code => ws.close(code)
  };
  ws.on('pong', () => { conn.alive = true; });
  ws.on('message', (data, isBinary) => {
    let m; try { m = isBinary ? null : JSON.parse(data.toString('utf8')); } catch { m = null; }
    onMessage(conn, m);
  });
  ws.on('close', () => onClose(conn));
});
setInterval(() => wss.clients.forEach(ws => {
  // 15 s ping, drop after a missed pong (~30 s).
  const c = ws; if (c._rkDead) return c.terminate(); c._rkDead = true; c.ping();
  c.once('pong', () => { c._rkDead = false; });
}), 15000).unref();

// Optional in-tree endpoint over stdio (06 section 1.4).
if (process.env.ENDPOINT_PATH) {
  const conn = spawnEndpoint(process.env.ENDPOINT_PATH, 'default', onMessage, onClose, log);
  conn.role = 'endpoint';
}

server.listen(PORT, () => log(`control-plane v${V} on http://localhost:${server.address().port}/ (mock endpoint: /mock)`));
for (const sig of ['SIGINT', 'SIGTERM']) process.on(sig, () => process.exit(0));
