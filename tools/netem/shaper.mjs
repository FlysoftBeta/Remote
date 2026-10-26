// One-direction FIFO link emulator: loss (uniform + Gilbert-Elliott), drop-tail byte queue, rate serializer, delay + jitter (+ spikes).
// push(buf, size, deliver) -> shapes; deliver(buf) is called at the computed departure time by the shared Scheduler.
import { performance } from 'node:perf_hooks';
export const now = () => performance.now();

// Min-heap scheduler with hybrid timer: setTimeout until ~spinMs before due, then setImmediate spin. Records timer error.
export class Scheduler {
  constructor(spinMs = 1.0) { this.h = []; this.seq = 0; this.timer = null; this.spinMs = spinMs; this.err = []; this.armedFor = Infinity; }
  add(due, fn) {
    const h = this.h, e = { due, seq: this.seq++, fn }; h.push(e);
    let i = h.length - 1;
    while (i > 0) { const p = (i - 1) >> 1; if (this.less(h[i], h[p])) { [h[i], h[p]] = [h[p], h[i]]; i = p; } else break; }
    this.arm();
  }
  less(a, b) { return a.due < b.due || (a.due === b.due && a.seq < b.seq); }
  pop() {
    const h = this.h, top = h[0], last = h.pop();
    if (h.length) { h[0] = last; let i = 0; for (;;) { let l = 2 * i + 1, r = l + 1, m = i;
      if (l < h.length && this.less(h[l], h[m])) m = l; if (r < h.length && this.less(h[r], h[m])) m = r;
      if (m === i) break; [h[i], h[m]] = [h[m], h[i]]; i = m; } }
    return top;
  }
  arm() {
    if (!this.h.length) return;
    const due = this.h[0].due;
    if (this.timer && this.armedFor <= due) return;
    if (this.timer) { clearTimeout(this.timer); clearImmediate(this.timer); }
    this.armedFor = due;
    const wait = due - now() - this.spinMs;
    this.timer = wait > 0 ? setTimeout(() => this.spin(), wait) : setImmediate(() => this.spin());
  }
  spin() {
    this.timer = null; this.armedFor = Infinity;
    while (this.h.length && this.h[0].due <= now()) {
      const e = this.pop(), lag = now() - e.due;
      if (this.err.length < 200000) this.err.push(lag);
      e.fn();
    }
    this.arm();
  }
  errStats() {
    const a = this.err.slice().sort((x, y) => x - y); if (!a.length) return null;
    const q = p => a[Math.min(a.length - 1, Math.floor(p * a.length))];
    return { n: a.length, p50: q(.5), p95: q(.95), p99: q(.99), max: a[a.length - 1] };
  }
}

function gauss(rnd) { let u = 0, v = 0; while (!u) u = rnd(); v = rnd(); return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * v); }
export function mulberry(seed) { return () => { seed |= 0; seed = seed + 0x6D2B79F5 | 0; let t = Math.imul(seed ^ seed >>> 15, 1 | seed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }; }

export class Link {
  constructor(name, prof, sched, opts = {}) {
    this.name = name; this.p = prof || {}; this.s = sched; this.rnd = mulberry(opts.seed ?? 1);
    this.rate = this.p.rateKbps ? this.p.rateKbps / 8 : 0; // bytes per ms
    this.queueBytesLimit = Infinity;
    if (this.rate) this.queueBytesLimit = this.p.queueBytes ?? Math.round(this.rate * (this.p.queueMs ?? 50));
    this.free = 0; this.lastOut = 0; this.bad = false; this.reorder = !!(opts.reorder || this.p.reorder);
    this.t0 = null; this.spikeNext = null; this.spikeEnd = 0; this.spikeExtra = 0;
    this.st = { pkts: 0, bytes: 0, outPkts: 0, outBytes: 0, dropQueue: 0, dropLoss: 0, dropBytes: 0, maxQueueDelayMs: 0, maxQueueBytes: 0, firstMs: null, lastMs: null, spikes: 0 };
    this.inflight = []; // [{out,size}] for queued-bytes calc
  }
  queuedBytes(t) { return this.free > t ? (this.free - t) * this.rate : 0; }
  push(size, deliver) {
    const t = now(), st = this.st, p = this.p;
    if (st.firstMs == null) { st.firstMs = t; this.t0 = t; }
    st.lastMs = t; st.pkts++; st.bytes += size;
    // loss
    if (p.ge) { // transition then loss
      this.bad = this.bad ? !(this.rnd() < p.ge.r) : this.rnd() < p.ge.p;
      if (this.bad && this.rnd() < (p.ge.lossBad ?? 1)) { st.dropLoss++; st.dropBytes += size; return; }
    }
    if (p.lossPct && this.rnd() * 100 < p.lossPct) { st.dropLoss++; st.dropBytes += size; return; }
    let depart = t;
    if (this.rate) {
      const qb = this.queuedBytes(t);
      if (qb + size > this.queueBytesLimit) { st.dropQueue++; st.dropBytes += size; return; }
      const start = Math.max(t, this.free); st.maxQueueDelayMs = Math.max(st.maxQueueDelayMs, start - t);
      st.maxQueueBytes = Math.max(st.maxQueueBytes, qb + size);
      this.free = start + size / this.rate; depart = this.free;
    }
    let d = (p.delayMs || 0) + (p.jitterMs ? gauss(this.rnd) * p.jitterMs : 0);
    const sp = p.spike;
    if (sp) {
      if (this.spikeNext == null) this.spikeNext = t + sp.everyS * 1000;
      if (t >= this.spikeNext) { this.spikeEnd = t + sp.durationMs; this.spikeExtra = sp.minMs + this.rnd() * (sp.maxMs - sp.minMs); this.spikeNext += sp.everyS * 1000; st.spikes++; }
      if (t < this.spikeEnd) d += this.spikeExtra;
    }
    if (d < 0) d = 0;
    let out = depart + d;
    if (!this.reorder && out < this.lastOut) out = this.lastOut; // FIFO link: no overtaking
    this.lastOut = Math.max(this.lastOut, out);
    if (out - t < 0.01) { st.outPkts++; st.outBytes += size; deliver(); return; }
    this.s.add(out, () => { st.outPkts++; st.outBytes += size; st.lastOutMs = now(); deliver(); });
  }
  report() {
    const st = this.st, dur = st.firstMs == null ? 0 : (st.lastMs - st.firstMs) / 1000;
    return { name: this.name, ...st, durationS: +dur.toFixed(2), offeredMbps: dur ? +(st.bytes * 8 / dur / 1e6).toFixed(3) : 0,
      achievedMbps: dur ? +(st.outBytes * 8 / dur / 1e6).toFixed(3) : 0, queueLimitBytes: Number.isFinite(this.queueBytesLimit) ? this.queueBytesLimit : null,
      maxQueueDelayMs: +st.maxQueueDelayMs.toFixed(2) };
  }
}
