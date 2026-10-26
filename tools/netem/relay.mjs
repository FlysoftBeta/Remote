// Minimal UDP TURN relay (RFC 5766 subset: Allocate/Refresh/CreatePermission/ChannelBind/Send/Data/ChannelData, long-term credentials)
// whose relay-to-relay forwarding runs through a per-direction link emulator (shaper.mjs). Both peers use this server, so every packet
// between them (media, DataChannels/SCTP, STUN checks) is shaped once per direction. Run: node relay.mjs --profile d50j10-c12
//   [--port 3478] [--http 3479] [--user-pass netem] [--queue-ms 50 | --queue-bytes N] [--reorder] [--seed 1] [--profiles file] [--delay ms --jitter ms --rate kbps]
// Direction: the allocation whose USERNAME is "endpoint" sends 'down' (endpoint->viewer); every other username sends 'up'.
import dgram from 'node:dgram';
import http from 'node:http';
import crypto from 'node:crypto';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import os from 'node:os';
import { spawn } from 'node:child_process';

// Windows default timer tick is 15.6 ms (setTimeout error 4-15 ms). A helper process holds timeBeginPeriod(1) while the relay runs.
export function holdTimerResolution() {
  const cmd = "Add-Type -Name W -Namespace N -MemberDefinition '[System.Runtime.InteropServices.DllImport(\"winmm.dll\")] public static extern uint timeBeginPeriod(uint p);'; [N.W]::timeBeginPeriod(1) | Out-Null; Start-Sleep -Seconds 7200";
  const p = spawn('powershell', ['-NoProfile', '-Command', cmd], { stdio: 'ignore', windowsHide: true });
  process.on('exit', () => { try { p.kill(); } catch {} });
  return p;
}
import { Scheduler, Link, now } from './shaper.mjs';

// libwebrtc does not gather/use loopback (a port bound to a LAN interface cannot send to 127.0.0.1, WSAEADDRNOTAVAIL), so the relay must listen on a
// real local interface address. Packets to the machine's own address never leave the host (no Wi-Fi traffic).
export function pickHost() {
  const c = Object.values(os.networkInterfaces()).flat().filter(i => i.family === 'IPv4' && !i.internal && !i.address.startsWith('169.254.') && !i.address.startsWith('198.18.'));
  return (c.find(i => i.address.startsWith('192.168.')) || c[0])?.address || '127.0.0.1';
}

const MAGIC = 0x2112A442, REALM = 'netem', NONCE = 'netem-nonce-0001';
const T = { BIND: 1, ALLOC: 3, REFRESH: 4, SEND: 0x16, DATA: 0x17, PERM: 8, CHAN: 9 };
const A = { USER: 6, MI: 8, ERR: 9, CHANNEL: 0xC, LIFETIME: 0xD, PEER: 0x12, DATA: 0x13, REALM: 0x14, NONCE: 0x15, RELAYED: 0x16, MAPPED: 0x20 };

function parse(b) {
  if (b.length < 20 || (b[0] & 0xC0) || b.readUInt32BE(4) !== MAGIC || b.readUInt16BE(2) + 20 > b.length) return null;
  const m = { type: b.readUInt16BE(0), tx: b.subarray(8, 20), attrs: new Map(), raw: b };
  const end = 20 + b.readUInt16BE(2);
  for (let o = 20; o + 4 <= end;) {
    const t = b.readUInt16BE(o), l = b.readUInt16BE(o + 2);
    if (o + 4 + l > end) return null;
    if (!m.attrs.has(t)) m.attrs.set(t, b.subarray(o + 4, o + 4 + l));
    if (t === A.MI) m.miOff = o;
    o += 4 + ((l + 3) & ~3);
  }
  return m;
}
function xorAddr(ip, port) {
  const b = Buffer.alloc(8); b[1] = 1; b.writeUInt16BE(port ^ (MAGIC >>> 16), 2);
  const p = ip.split('.').map(Number); const v = ((p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]) >>> 0) ^ MAGIC; b.writeUInt32BE(v >>> 0, 4); return b;
}
function unxor(v) { const port = v.readUInt16BE(2) ^ (MAGIC >>> 16), x = (v.readUInt32BE(4) ^ MAGIC) >>> 0; return { ip: [x >>> 24, x >>> 16 & 255, x >>> 8 & 255, x & 255].join('.'), port }; }
function build(type, tx, attrs, key) {
  const parts = [];
  for (const [t, v] of attrs) { const h = Buffer.alloc(4); h.writeUInt16BE(t, 0); h.writeUInt16BE(v.length, 2); parts.push(h, v, Buffer.alloc((4 - v.length % 4) % 4)); }
  let body = Buffer.concat(parts);
  const head = Buffer.alloc(20); head.writeUInt16BE(type, 0); head.writeUInt32BE(MAGIC, 4); tx.copy(head, 8);
  if (key) {
    head.writeUInt16BE(body.length + 24, 2);
    const mi = crypto.createHmac('sha1', key).update(Buffer.concat([head, body])).digest();
    const h = Buffer.alloc(4); h.writeUInt16BE(A.MI, 0); h.writeUInt16BE(20, 2); body = Buffer.concat([body, h, mi]);
  }
  head.writeUInt16BE(body.length, 2);
  return Buffer.concat([head, body]);
}
const u32 = n => { const b = Buffer.alloc(4); b.writeUInt32BE(n); return b; };
const errAttr = (code, text) => { const b = Buffer.alloc(4 + text.length); b[2] = code / 100 | 0; b[3] = code % 100; b.write(text, 4); return b; };

export async function startRelay(opts = {}) {
  const traceBuf = opts.trace || process.env.RELAY_TRACE ? [] : null;
  const host = opts.host || pickHost(), port = opts.port ?? 3478, pass = opts.pass || 'netem';
  const prof = opts.profile || {};
  const sched = new Scheduler(opts.spinMs ?? 1.0);
  const linkOpts = { seed: opts.seed ?? 1, reorder: opts.reorder };
  const links = { down: new Link('endpoint->viewer', prof, sched, linkOpts), up: new Link('viewer->endpoint', prof, sched, { ...linkOpts, seed: (opts.seed ?? 1) + 1 }) };
  const main = dgram.createSocket('udp4');
  const allocs = new Map(); // "ip:port" client -> alloc
  const byRelay = new Map(); // "ip:port" relayed -> alloc
  const key = u => crypto.createHash('md5').update(`${u}:${REALM}:${pass}`).digest();
  const tally = { allocations: 0, unshapedPkts: 0, unauthorized: 0 };

  const sendClient = (al, buf) => main.send(buf, al.cport, al.chost);
  function deliver(dst, srcRelay, payload) { // relay -> client of dst: ChannelData if bound, else Data indication
    const pk = `${srcRelay.ip}:${srcRelay.port}`, ch = dst.chanByPeer.get(pk);
    if (ch != null) { const h = Buffer.alloc(4); h.writeUInt16BE(ch, 0); h.writeUInt16BE(payload.length, 2); sendClient(dst, Buffer.concat([h, payload])); return; }
    sendClient(dst, build(0x0017, crypto.randomBytes(12), [[A.PEER, xorAddr(srcRelay.ip, srcRelay.port)], [A.DATA, payload]]));
  }
  function forward(al, peerIp, peerPort, payload) {
    const dst = byRelay.get(`${peerIp}:${peerPort}`);
    if (dst) {
      const link = al.user === 'endpoint' ? links.down : links.up;
      al.sent += payload.length; al.sentPkts++;
      let rec = null;
      if (traceBuf) { // m3-trace: per-packet dwell keyed by RTP seq/timestamp (RTP header is in clear under SRTP); kinds: r=RTP, c=RTCP, t=STUN, d=DTLS/SCTP, o=other
        const b0 = payload[0], b1 = payload[1];
        let kind = 'o', seq = 0, ts = 0, ssrc = 0, mk = 0;
        if (b0 < 4) kind = 't'; else if (b0 >= 20 && b0 <= 63) kind = 'd';
        else if (b0 >> 6 === 2 && payload.length >= 12) { if (b1 >= 192 && b1 <= 223) kind = 'c'; else { kind = 'r'; seq = payload.readUInt16BE(2); ts = payload.readUInt32BE(4); ssrc = payload.readUInt32BE(8); mk = b1 >> 7; } }
        rec = [al.user === 'endpoint' ? 'D' : 'U', now(), null, payload.length, kind, seq, ts, mk, ssrc]; traceBuf.push(rec);
      }
      link.push(payload.length + 28, () => { if (rec) rec[2] = now(); deliver(dst, { ip: host, port: al.rport }, payload); });
    } else { tally.unshapedPkts++; al.sock.send(payload, peerPort, peerIp); }
  }
  function onClient(msg, ri) {
    const ck = `${ri.address}:${ri.port}`;
    if (msg.length >= 4 && msg[0] >= 0x40 && msg[0] <= 0x7F) { // ChannelData
      const al = allocs.get(ck); if (!al) return; const ch = msg.readUInt16BE(0), len = msg.readUInt16BE(2), p = al.peerByChan.get(ch);
      if (p) forward(al, p.ip, p.port, msg.subarray(4, 4 + len));
      return;
    }
    const m = parse(msg); if (!m) return;
    if (m.type === T.SEND) {
      const al = allocs.get(ck), pa = m.attrs.get(A.PEER), d = m.attrs.get(A.DATA);
      if (al && pa && d) { const p = unxor(pa); forward(al, p.ip, p.port, d); }
      return;
    }
    if (m.type === T.BIND) { main.send(build(0x0101, m.tx, [[A.MAPPED, xorAddr(ri.address, ri.port)]]), ri.port, ri.address); return; }
    if (m.type & 0x110) return; // requests only below
    const method = (m.type & 0xF) | ((m.type >> 1) & 0x70) | ((m.type >> 2) & 0xF80);
    const reply = (cl, attrs, k) => main.send(build((m.type & ~0x110) | cl, m.tx, attrs, k), ri.port, ri.address);
    const unauth = () => { tally.unauthorized++; reply(0x110, [[A.ERR, errAttr(401, 'Unauthorized')], [A.REALM, Buffer.from(REALM)], [A.NONCE, Buffer.from(NONCE)]]); };
    const user = m.attrs.get(A.USER)?.toString();
    if (!user || m.miOff == null) return unauth();
    const k = key(user);
    const chk = Buffer.from(m.raw.subarray(0, m.miOff)); chk.writeUInt16BE(m.miOff + 24 - 20, 2);
    if (!crypto.createHmac('sha1', k).update(chk).digest().equals(m.attrs.get(A.MI))) return unauth();
    let al = allocs.get(ck);
    if (method === T.ALLOC) {
      if (al) return reply(0x110, [[A.ERR, errAttr(437, 'Allocation Mismatch')]]);
      const sock = dgram.createSocket('udp4');
      sock.bind(0, host, () => {
        al = { user, chost: ri.address, cport: ri.port, sock, rport: sock.address().port, peerByChan: new Map(), chanByPeer: new Map(), sent: 0, sentPkts: 0, rcvExt: 0 };
        allocs.set(ck, al); byRelay.set(`${host}:${al.rport}`, al); tally.allocations++;
        sock.on('message', (d, r) => { al.rcvExt += d.length; deliver(al, { ip: r.address, port: r.port }, d); }); // external (unshaped) inbound
        reply(0x100, [[A.RELAYED, xorAddr(host, al.rport)], [A.LIFETIME, u32(600)], [A.MAPPED, xorAddr(ri.address, ri.port)]], k);
      });
      return;
    }
    if (!al) return reply(0x110, [[A.ERR, errAttr(437, 'Allocation Mismatch')]]);
    if (method === T.REFRESH) {
      const lt = m.attrs.get(A.LIFETIME)?.readUInt32BE(0) ?? 600;
      if (lt === 0) { allocs.delete(ck); byRelay.delete(`${host}:${al.rport}`); al.sock.close(); }
      return reply(0x100, [[A.LIFETIME, u32(lt)]], k);
    }
    if (method === T.PERM) return reply(0x100, [], k); // permissions are implicit (single trusted lab server)
    if (method === T.CHAN) {
      const c = m.attrs.get(A.CHANNEL), p = m.attrs.get(A.PEER);
      if (!c || !p) return reply(0x110, [[A.ERR, errAttr(400, 'Bad Request')]]);
      const ch = c.readUInt16BE(0), pe = unxor(p); al.peerByChan.set(ch, pe); al.chanByPeer.set(`${pe.ip}:${pe.port}`, ch);
      return reply(0x100, [], k);
    }
    reply(0x110, [[A.ERR, errAttr(400, 'Bad Request')]]);
  }
  main.on('message', onClient);
  await new Promise((res, rej) => { main.once('error', rej); main.bind(port, host, res); });
  const bound = main.address().port;

  const stats = () => ({
    profile: opts.profileName || null, profileConfig: prof, queueMs: prof.queueMs ?? (prof.rateKbps ? 50 : null),
    links: { down: links.down.report(), up: links.up.report() }, timerError: sched.errStats(), tally,
    allocations: [...allocs.values()].map(a => ({ user: a.user, relayPort: a.rport, sentBytes: a.sent, sentPkts: a.sentPkts, externalInBytes: a.rcvExt })) });
  const reset = () => {
    for (const l of Object.values(links)) Object.assign(l.st, { pkts: 0, bytes: 0, outPkts: 0, outBytes: 0, dropQueue: 0, dropLoss: 0, dropBytes: 0, maxQueueDelayMs: 0, maxQueueBytes: 0, firstMs: null, lastMs: null, spikes: 0 });
    sched.err.length = 0;
    for (const a of allocs.values()) { a.sent = 0; a.sentPkts = 0; }
  };
  let httpServer = null;
  if (opts.http != null) {
    httpServer = http.createServer((q, r) => {
      if (q.method === 'POST' && q.url === '/reset') { reset(); if (traceBuf) traceBuf.length = 0; r.end('ok'); return; }
      if (q.url === '/now') { r.setHeader('access-control-allow-origin', '*'); r.setHeader('cache-control', 'no-store'); r.end(String(Number(process.hrtime.bigint()) / 1e6)); return; } // hrtime ms (same QPC base as TimeTicks, verified by correlation)
      if (q.url === '/trace') { r.setHeader('content-type', 'application/json'); r.end(JSON.stringify({ hrMinusPerf: Number(process.hrtime.bigint()) / 1e6 - now(), cols: ['dir', 'tIn', 'tOut', 'size', 'kind', 'seq', 'ts', 'marker', 'ssrc'], rows: traceBuf || [] })); return; }
      r.setHeader('content-type', 'application/json'); r.end(JSON.stringify(stats()));
    });
    await new Promise(res => httpServer.listen(opts.http, host, res));
  }
  return { host, port: bound, stats, reset, close() { main.close(); httpServer?.close(); for (const a of allocs.values()) try { a.sock.close(); } catch {} } };
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const argv = process.argv.slice(2), arg = (k, d) => { const i = argv.indexOf('--' + k); return i < 0 ? d : (argv[i + 1] && !argv[i + 1].startsWith('--') ? argv[i + 1] : true); };
  const profiles = JSON.parse(fs.readFileSync(arg('profiles', fileURLToPath(new URL('./profiles.json', import.meta.url))), 'utf8'));
  const name = arg('profile', 'none');
  if (!profiles[name]) { console.error('unknown profile ' + name + '; have: ' + Object.keys(profiles).join(' ')); process.exit(2); }
  const prof = { ...profiles[name] };
  if (arg('queue-ms')) prof.queueMs = +arg('queue-ms');
  if (arg('queue-bytes')) prof.queueBytes = +arg('queue-bytes');
  if (arg('delay')) prof.delayMs = +arg('delay');
  if (arg('jitter')) prof.jitterMs = +arg('jitter');
  if (arg('rate')) prof.rateKbps = +arg('rate');
  if (argv.includes('--hires')) holdTimerResolution();
  const r = await startRelay({ host: arg('host', undefined), port: +arg('port', 3478), http: +arg('http', 3479), pass: arg('user-pass', 'netem'), profile: prof, profileName: name, reorder: !!arg('reorder'), seed: +arg('seed', 1), spinMs: +arg('spin-ms', 16) });
  console.log(`netem relay udp ${r.host}:${r.port} http ${arg('http', 3479)} profile ${name} ${JSON.stringify(prof)}`);
  setInterval(() => {
    const s = r.stats();
    console.log(`[netem] down ${s.links.down.achievedMbps}/${s.links.down.offeredMbps} Mbps drop q${s.links.down.dropQueue} l${s.links.down.dropLoss} maxQ ${s.links.down.maxQueueDelayMs}ms | up ${s.links.up.achievedMbps} Mbps | timer p99 ${s.timerError?.p99?.toFixed(2)}ms`);
  }, 5000).unref();
}
