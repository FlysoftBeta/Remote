// Client for the browser-endpoint control channel (framed JSON over the child's stdin/stdout, 01 section 2).
import { spawn } from 'node:child_process';
import { Decoder, Writer } from './framing.js';

export class Endpoint {
  #next = 1;
  #pending = new Map();
  constructor(exe, args = []) {
    this.child = spawn(exe, args, { stdio: ['pipe', 'pipe', 'inherit'], windowsHide: true });
    this.writer = new Writer(this.child.stdin);
    const decoder = new Decoder(m => this.#onMessage(m));
    this.child.stdout.on('data', c => { try { decoder.push(c); } catch (e) { this.#fail(e); } });
    this.child.on('exit', code => this.#fail(new Error(`endpoint exited (${code})`)));
  }
  call(method, params = {}, timeoutMs = 5000) {
    const id = this.#next++;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.#pending.delete(id); reject(new Error(`${method} timed out`)); }, timeoutMs);
      this.#pending.set(id, { resolve, reject, timer });
      this.writer.write({ v: 1, id, method, params }).catch(e => { this.#pending.delete(id); clearTimeout(timer); reject(e); });
    });
  }
  async hello() {
    const r = await this.call('hello');
    if (r.name !== 'remotekit-endpoint') throw new Error('incompatible endpoint hello');
    return r;
  }
  async stop() { try { await this.call('shutdown', {}, 2000); } catch { this.child.kill(); } }
  #onMessage(m) {
    const p = this.#pending.get(m.id);
    if (!p) return;
    this.#pending.delete(m.id);
    clearTimeout(p.timer);
    m.error ? p.reject(Object.assign(new Error(m.error.message), { code: m.error.code })) : p.resolve(m.result);
  }
  #fail(err) {
    for (const p of this.#pending.values()) { clearTimeout(p.timer); p.reject(err); }
    this.#pending.clear();
  }
}
