// Unit self-test of shaper + scheduler: node selftest.mjs  (Link/Scheduler directly, no sockets)
import { Scheduler, Link, now } from './shaper.mjs';
const run = (prof, pps, secs, size = 1200) => new Promise(res => {
  const s = new Scheduler(1), l = new Link('t', prof, s), lat = []; let n = 0, last = -1, reord = 0; const t0 = now();
  const tick = setInterval(() => {
    const el = (now() - t0) / 1000;
    if (el > secs) { clearInterval(tick); setTimeout(() => res({ l, s, lat, reord }), 500); return; }
    while (n < el * pps) { const id = n++, ts = now(); l.push(size, () => { lat.push(now() - ts); if (id < last) reord++; last = Math.max(last, id); }); }
  }, 2);
});
const pct = (a, p) => { const b = a.slice().sort((x, y) => x - y); return b[Math.floor(p * b.length)]?.toFixed(2); };
for (const [name, prof, pps] of [['d50j10 uncapped', { delayMs: 50, jitterMs: 10 }, 500], ['c12 cap, offered ~14.4Mbps', { rateKbps: 12000, delayMs: 20 }, 1500], ['wifi GE loss', { delayMs: 25, jitterMs: 5, ge: { p: 0.0025, r: 0.25, lossBad: 1 } }, 1000]]) {
  const { l, s, lat, reord } = await run(prof, pps, 8);
  console.log(name, 'lat P50/P99', pct(lat, 0.5), pct(lat, 0.99), 'reordered', reord, JSON.stringify(l.report()), 'timer', JSON.stringify(s.errStats()));
}
process.exit(0);
