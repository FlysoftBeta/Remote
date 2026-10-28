// remotekit control-plane sample (D13, docs/protocol.md section 1): static files + WebSocket signalling + sessions.
// Not on the media/input path. Run: node control-plane [--port 8080] [--tls-cert c.pem --tls-key k.pem | --tls-pfx c.pfx]
// [--http-redirect-port 80]; env CP_TOKEN (auth placeholder), ENDPOINT_PATH (stdio endpoint), TLS_* (see below).
import http from 'node:http';
import https from 'node:https';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { WebSocketServer } from 'ws';
import { endpointArgs } from './endpoint.js';
import { InstanceManager, ApiError } from './manager.js';
import { Auth, type User } from './auth.js';
import type { IncomingMessage, ServerResponse } from 'node:http';
import type { WebSocket } from 'ws';

const V = 1;
const repo = fileURLToPath(new URL('../../', import.meta.url));
const roots = { '/tools/': path.join(repo, 'tools'), '/': fs.existsSync(path.join(repo, 'viewer/dist/index.html')) ? path.join(repo, 'viewer/dist') : path.join(repo, 'viewer') };
const types = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.json': 'application/json' };
const argPort = process.argv.indexOf('--port');
const PORT = Number(argPort > 0 ? process.argv[argPort + 1] : process.env.PORT ?? 8080);
const log = (...a) => console.log(new Date().toISOString().slice(11, 23), ...a);
const arg = (name, env) => { const i = process.argv.indexOf(name); return i > 0 ? process.argv[i + 1] : process.env[env]; };

// TLS (optional): PEM cert+key (--tls-cert/--tls-key, TLS_CERT/TLS_KEY, optional chain --tls-ca/TLS_CA) or a PKCS#12
// bundle (--tls-pfx/TLS_PFX, passphrase TLS_PFX_PASSPHRASE). The cert files are re-read when they change (renewal).
// --http-redirect-port/HTTP_REDIRECT_PORT additionally serves plain HTTP that redirects to HTTPS.
const tls = {
  cert: arg('--tls-cert', 'TLS_CERT'), key: arg('--tls-key', 'TLS_KEY'), ca: arg('--tls-ca', 'TLS_CA'),
  pfx: arg('--tls-pfx', 'TLS_PFX'), passphrase: process.env.TLS_PFX_PASSPHRASE,
  redirectPort: arg('--http-redirect-port', 'HTTP_REDIRECT_PORT')
};
const TLS_ON = Boolean(tls.pfx || (tls.cert && tls.key));
if (!TLS_ON && (tls.cert || tls.key)) { console.error('TLS needs both --tls-cert and --tls-key (or --tls-pfx)'); process.exit(2); }
function tlsOptions() {
  if (tls.pfx) return { pfx: fs.readFileSync(tls.pfx), passphrase: tls.passphrase };
  return { cert: fs.readFileSync(tls.cert), key: fs.readFileSync(tls.key), ...(tls.ca ? { ca: fs.readFileSync(tls.ca) } : {}) };
}
function makeServer(handler) {
  if (!TLS_ON) return http.createServer(handler);
  const s = https.createServer(tlsOptions(), handler);
  let timer = null;
  const reload = () => { clearTimeout(timer); timer = setTimeout(() => {
    try { s.setSecureContext(tlsOptions()); log('TLS certificate reloaded'); } catch (e) { log(`TLS reload failed, keeping the old certificate: ${e.message}`); }
  }, 1000); };
  for (const f of [tls.pfx, tls.cert, tls.key, tls.ca].filter(Boolean)) fs.watchFile(f, { interval: 5000 }, reload);
  if (tls.redirectPort) {
    http.createServer((req, res) => {
      const host = (req.headers.host || 'localhost').replace(/:\d+$/, '');
      res.writeHead(301, { Location: `https://${host}${PORT === 443 ? '' : ':' + PORT}${req.url}` }).end();
    }).listen(Number(tls.redirectPort), process.env.HOST || undefined, () => log(`HTTP->HTTPS redirect on port ${tls.redirectPort}`));
  }
  return s;
}

// instance id -> { endpoint: conn|null, viewer: conn|null (current controller), sid }
const instances = new Map();
const inst = id => { if (!instances.has(id)) instances.set(id, { endpoint: null, viewer: null, sid: null }); return instances.get(id); };
const reports = [];
const fieldDir = process.env.FIELD_REPORT_DIR;
const fieldConfig = process.env.FIELD_CONFIG_JSON ? JSON.parse(process.env.FIELD_CONFIG_JSON) : null;
if (fieldDir) fs.mkdirSync(fieldDir, { recursive: true });

const FROM_VIEWER = new Set(['answer', 'ice', 'restart', 'bye']);
const FROM_ENDPOINT = new Set(['offer', 'ice', 'bye']);

function startSession(id) {
  const I = inst(id);
  if (!I.endpoint || !I.viewer || I.sid) return;
  I.sid = crypto.randomBytes(16).toString('hex');
  const sid = I.sid, viewer = I.viewer, endpoint = I.endpoint;
  viewer.sid = sid;
  viewer.send({ t: 'welcome', v: V, sid, instance: id, iceServers: [] });
  // netem lab (tools/netem): a viewer that asks for ?relay=turn:host:port makes both peers relay-only through that TURN server.
  const relay = viewer.relay ? { iceServers: [{ urls: viewer.relay, username: 'endpoint', credential: 'netem' }], iceTransportPolicy: 'relay' } : { iceServers: [] };
  if (I.sid !== sid) return;
  endpoint.send({ t: 'session.open', sid, viewer: { caps: viewer.caps, ua: viewer.ua }, ...relay });
  if (I.sid === sid) log(`session ${sid.slice(0, 8)} open on ${id}`);
}
function endSession(id, reason, notify = '') {
  const I = inst(id);
  if (!I.sid) return;
  const sid = I.sid;
  I.sid = null;
  if (notify !== 'endpoint') I.endpoint?.send({ t: 'bye', sid, reason });
  if (notify !== 'viewer' && I.viewer) { const viewer = I.viewer; viewer.sid = null; viewer.send({ t: 'bye', sid, reason }); }
  log(`session ${sid.slice(0, 8)} closed on ${id}: ${reason}`);
}

function onHello(conn, m) {
  if (m.v !== V) { conn.send({ t: 'error', code: 'version', message: `need v${V}` }); return conn.close(4400); }
  const I = inst(conn.instance);
  if (conn.role === 'endpoint') {
    if (I.endpoint && I.endpoint !== conn) { const old = I.endpoint; endSession(conn.instance, 'endpoint-replaced'); old.close(4409); }
    I.endpoint = conn;
    conn.name = m.name;
    log(`endpoint ${m.name || '?'} registered for ${conn.instance}`);
    startSession(conn.instance);
  } else {
    conn.caps = m.caps || {};
    conn.relay = typeof m.relay === 'string' && /^turn:[\w.-]+:\d+(\?transport=udp)?$/.test(m.relay) ? m.relay : null;
    conn.ua = String(m.ua || '').slice(0, 200);
    if (I.viewer && I.viewer !== conn) {
      const old = I.viewer;
      endSession(conn.instance, 'replaced');
      old.close(4000);
    }
    I.viewer = conn;
    log(`viewer joined ${conn.instance}: touch=${conn.caps.touch} osk=${conn.caps.osk} h264=${(conn.caps.codecs || []).length} ${(conn.ua.match(/(HeadlessChrome|Chrome|Edg)\/[\d.]+/g) || []).join(' ')}`);
    if (!I.endpoint) conn.send({ t: 'error', code: 'no-endpoint', message: 'waiting for endpoint' });
    startSession(conn.instance);
  }
}

function onMessage(conn, m) {
  if (!m || typeof m !== 'object' || typeof m.t !== 'string') return conn.close(4400);
  if (m.t === 'hello') return onHello(conn, m);
  const I = inst(conn.instance);
  if (conn.role === 'viewer') {
    if (I.viewer !== conn || !I.sid || m.sid !== I.sid || !FROM_VIEWER.has(m.t)) return;
    I.endpoint?.send(m);
    if (m.t === 'bye') { I.sid = null; conn.sid = null; log(`session closed by viewer: ${m.reason}`); }
  } else {
    if (I.endpoint !== conn || !FROM_ENDPOINT.has(m.t) || m.sid !== I.sid || !I.viewer) return;
    I.viewer.send(m);
    if (m.t === 'bye') { I.sid = null; if (I.viewer) I.viewer.sid = null; log(`session closed by endpoint: ${m.reason}`); }
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
  return new Promise<string>((resolve, reject) => {
    let n = 0; const parts = [];
    req.on('data', c => { n += c.length; if (n > limit) { reject(new Error('too large')); req.destroy(); } else parts.push(c); });
    req.on('end', () => resolve(Buffer.concat(parts).toString('utf8')));
    req.on('error', reject);
  });
}
const json = (res, code, obj) => { res.writeHead(code, { 'content-type': 'application/json' }); res.end(JSON.stringify(obj)); };
const manager = new InstanceManager({
  dataDir: arg('--data-dir', 'CP_DATA_DIR') || path.join(repo, '.data'), endpointPath: process.env.ENDPOINT_PATH, chromePath: process.env.CHROME_PATH,
  legacyArgs: endpointArgs(), onMessage, onClose, log, viewerConnected: id => !!instances.get(id)?.viewer
});
const sessions = new Map<WebSocket, { token: string; instance: string; user: User }>();
const auth = new Auth(manager, id => {
  for (const [ws, s] of sessions) if (s.user.id === id && !auth.authenticate(s.token)) ws.close(4401);
});
const requestToken = (req: IncomingMessage, url: URL) => req.headers.authorization?.replace(/^Bearer /, '') || url.searchParams.get('token') || '';
function owned<T extends { owner: string }>(row: T, user: User): T {
  if (user.role !== 'admin' && row.owner !== user.id) throw new ApiError(403, 'owner or admin required');
  return row;
}
function owner(input: any, user: User) {
  if (!input || typeof input !== 'object' || Array.isArray(input)) throw new ApiError(400, 'JSON object required');
  const value = input.owner ?? user.id;
  if (value !== user.id && user.role !== 'admin') throw new ApiError(403, 'cannot assign another owner');
  if (!manager.users.some(u => u.id === value)) throw new ApiError(400, 'unknown owner');
  return value;
}

async function management(req: IncomingMessage, res: ServerResponse, url: URL) {
  if (!['GET', 'HEAD'].includes(req.method) && req.headers.origin &&
      ![`http://${req.headers.host}`, `https://${req.headers.host}`].includes(req.headers.origin))
    return json(res, 403, { error: 'origin mismatch' });
  try {
    const parts = url.pathname.split('/').filter(Boolean).map(decodeURIComponent);
    const [type, id, action] = parts.slice(1);
    const body = async () => JSON.parse(await readBody(req, 64 * 1024));
    if (url.pathname === '/api/auth/config' && req.method === 'GET') return json(res, 200, auth.info());
    if (url.pathname === '/api/auth/register' && req.method === 'POST') {
      const input = await body(); await auth.create(input);
      return json(res, 201, await auth.login(input));
    }
    if (url.pathname === '/api/auth/login' && req.method === 'POST') return json(res, 200, await auth.login(await body()));
    const token = requestToken(req, url), user = auth.authenticate(token);
    if (!user) return json(res, 401, { error: 'login required' });
    if (url.pathname === '/api/auth/me' && req.method === 'GET') return json(res, 200, user);
    if (url.pathname === '/api/auth/logout' && req.method === 'POST') { await auth.logout(token); return res.writeHead(204).end(); }
    if (type === 'users') {
      if (user.role !== 'admin') throw new ApiError(403, 'admin required');
      if (parts.length === 2 && req.method === 'GET') return json(res, 200, auth.list());
      if (parts.length === 2 && req.method === 'POST') return json(res, 201, await auth.create(await body(), user));
      if (parts.length === 3 && req.method === 'DELETE') { await auth.delete(id); return res.writeHead(204).end(); }
      if (parts.length === 4 && action === 'password' && req.method === 'POST') { await auth.reset(id, (await body()).password); return res.writeHead(204).end(); }
    }
    if (type === 'instances') {
      if (parts.length === 2 && req.method === 'GET') return json(res, 200, manager.list().filter(c => user.role === 'admin' || c.owner === user.id));
      if (parts.length === 2 && req.method === 'POST') {
        const input = await body(); const selectedOwner = owner(input, user);
        const p = manager.listProfiles().find(p => p.id === input.profile);
        if (!p) throw new ApiError(404, 'profile not found'); owned(p, user);
        return json(res, 201, await manager.create({ ...input, owner: selectedOwner }));
      }
      if (id) owned(manager.get(id), user);
      if (parts.length === 3 && req.method === 'GET') return json(res, 200, manager.get(id));
      if (parts.length === 3 && req.method === 'PATCH') return json(res, 200, await manager.patch(id, await body()));
      if (parts.length === 3 && req.method === 'DELETE') {
        for (const [ws, s] of sessions) if (s.instance === id) ws.close(4403);
        await manager.delete(id); return res.writeHead(204).end();
      }
      if (parts.length === 4 && req.method === 'POST' && ['start', 'stop', 'restart'].includes(action)) return json(res, 200, await manager[action](id));
    } else if (type === 'profiles') {
      if (parts.length === 2 && req.method === 'GET') return json(res, 200, manager.listProfiles().filter(p => user.role === 'admin' || p.owner === user.id));
      if (parts.length === 2 && req.method === 'POST') { const input = await body(); return json(res, 201, await manager.createProfile({ ...input, owner: owner(input, user) })); }
      if (id) { const p = manager.listProfiles().find(p => p.id === id); if (!p) throw new ApiError(404, 'profile not found'); owned(p, user); }
      if (parts.length === 3 && req.method === 'DELETE') { await manager.deleteProfile(id); return res.writeHead(204).end(); }
    }
    return json(res, 405, { error: 'unsupported management route or method' });
  } catch (e) {
    if (!(e instanceof ApiError) && !(e instanceof SyntaxError)) log(`management request failed: ${e.stack}`);
    return json(res, e.status || (e instanceof SyntaxError ? 400 : 500), { error: e.status || e instanceof SyntaxError ? e.message : 'management operation failed; see server log' });
  }
}

const server = makeServer(async (req, res) => {
  try {
  const url = new URL(req.url, 'http://x');
  if (/^\/api\/(auth|users|instances|profiles)(\/|$)/.test(url.pathname)) return await management(req, res, url);
  if (url.pathname === '/field-config') return json(res, fieldConfig ? 200 : 404, fieldConfig || { error: 'field mode disabled' });
  if (url.pathname === '/field-report') {
    if (!fieldDir) return json(res, 404, { error: 'field mode disabled' });
    if (req.method !== 'POST') return json(res, 405, { error: 'POST required' });
    if (req.headers.origin && req.headers.origin !== `http://${req.headers.host}` &&
        req.headers.origin !== `https://${req.headers.host}`) return json(res, 403, { error: 'origin mismatch' });
    let report;
    try {
      report = JSON.parse(await readBody(req, 16 << 20));
      if (!report || report.kind !== 'field-latency' || !report.summary || !report.summary.latDecoded ||
          !report.summary.i2decoded || !report.summary.lat || !report.summary.i2display)
        throw new Error('invalid field report');
    } catch (e) { return json(res, 400, { error: e.message }); }
    report.at = new Date().toISOString();
    report.serverRun = fieldConfig;
    const name = `field-${report.at.replace(/[:.]/g, '-')}-${crypto.randomBytes(4).toString('hex')}.json`;
    try { await fs.promises.writeFile(path.join(fieldDir, name), JSON.stringify(report, null, 2), { flag: 'wx' }); }
    catch (e) { log(`field report save failed: ${e.message}`); return json(res, 500, { error: 'report save failed; download a copy on the viewer' }); }
    log(`field report saved: ${name}`);
    return json(res, 201, { ok: true, file: name });
  }
  if (url.pathname === '/api/health') {
    return json(res, 200, { ok: true, v: V, instances: [...instances].map(([id, I]) => ({ id, endpoint: I.endpoint?.name ?? null, viewer: !!I.viewer, session: !!I.sid })) });
  }
  if (url.pathname === '/api/report') {
    if (req.method === 'POST') {
      try { const r = JSON.parse(await readBody(req, 64 << 20)); r.at = new Date().toISOString(); reports.push(r); if (reports.length > 50) reports.shift();
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
  if (!file.startsWith(base + path.sep)) { res.writeHead(403); return res.end(); }
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404); return res.end(); }
    res.writeHead(200, { 'content-type': types[path.extname(file)] || 'application/octet-stream', 'cache-control': 'no-store' });
    res.end(data);
  });
  } catch (e) { json(res, 400, { error: 'invalid request path' }); }
});

const wss = new WebSocketServer({ server, path: '/signal', maxPayload: 64 * 1024 });
wss.on('connection', (ws, req) => {
  let conn = null;
  const failed = () => { ws.terminate(); if (conn) onClose(conn); };
  ws.on('error', failed);
  const send = m => {
    try { if (ws.readyState === 1) ws.send(JSON.stringify(m), error => { if (error) failed(); }); }
    catch (e) { failed(); }
  };
  const q = new URL(req.url, 'http://x').searchParams;
  const role = q.get('role') === 'endpoint' ? 'endpoint' : 'viewer';
  const token = q.get('token') || '', user = auth.authenticate(token), instance = (q.get('instance') || 'default').slice(0, 64);
  if (!user) { send({ t: 'error', code: 'auth' }); return ws.close(4401); }
  // WS test endpoints use an admin/owner session; native stdio endpoints never use this gate.
  const record = manager.instances.get(instance);
  if ((record && user.role !== 'admin' && record.owner !== user.id) || (!record && user.role !== 'admin')) {
    send({ t: 'error', code: 'auth' }); return ws.close(4403);
  }
  sessions.set(ws, { token, instance, user });
  conn = {
    role, instance, alive: true,
    send,
    close: code => ws.close(code)
  };
  ws.on('pong', () => { conn.alive = true; });
  ws.on('message', (data, isBinary) => {
    if (!auth.authenticate(token)) return ws.close(4401);
    let m; try { m = isBinary ? null : JSON.parse(data.toString('utf8')); } catch { m = null; }
    onMessage(conn, m);
  });
  ws.on('close', () => { sessions.delete(ws); onClose(conn); });
});
setInterval(() => wss.clients.forEach(ws => {
  // 15 s ping, drop after a missed pong (~30 s).
  const c = ws as WebSocket & { _rkDead?: boolean }; if (c._rkDead) return c.terminate(); c._rkDead = true; c.ping();
  const s = sessions.get(ws); if (s && !auth.authenticate(s.token)) return ws.close(4401);
  c.once('pong', () => { c._rkDead = false; });
}), 15000).unref();

await manager.boot();
server.listen(PORT, process.env.HOST || undefined, () => log(`control-plane v${V} on ${TLS_ON ? 'https' : 'http'}://localhost:${(server.address() as import('node:net').AddressInfo).port}/ (mock endpoint: /mock)`));
let stopping = false;
for (const sig of ['SIGINT', 'SIGTERM']) process.on(sig, async () => {
  if (stopping) return;
  stopping = true; server.close();
  try { await manager.shutdown(); process.exit(0); }
  catch (e) { log(`shutdown failed: ${e.message}`); process.exit(1); }
});
