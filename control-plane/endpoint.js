// stdio transport for the in-tree endpoint (docs/protocol.md section 1.4): the same JSON messages as the
// WebSocket endpoint role, framed as 4-byte LE length + UTF-8 JSON on stdin/stdout; stderr is the endpoint log.
import { spawn } from 'node:child_process';
import { Decoder, Writer } from './framing.js';

export function endpointArgs() {
  // Field launcher uses an array so executable/profile paths may contain spaces.
  const extra = process.env.ENDPOINT_ARGS_JSON ? JSON.parse(process.env.ENDPOINT_ARGS_JSON) :
    (process.env.ENDPOINT_ARGS || '').split(' ').filter(Boolean);
  if (!Array.isArray(extra) || extra.some(arg => typeof arg !== 'string'))
    throw new Error('ENDPOINT_ARGS_JSON must be an array of strings');
  return extra;
}

export function spawnEndpoint(exe, instance, onMessage, onClose, log = console.log, extra = endpointArgs()) {
  const child = spawn(exe, [`--instance=${instance}`, ...extra], { stdio: ['pipe', 'pipe', 'inherit'], windowsHide: true });
  const writer = new Writer(child.stdin);
  let closed = false, failure = null, forcePromise = null, stopPromise = null;
  let resolveExit;
  const exited = new Promise(resolve => { resolveExit = resolve; });
  const force = () => forcePromise ||= (async () => {
    if (closed) return;
    if (process.platform === 'win32' && child.pid) {
      await new Promise(resolve => {
        const killer = spawn(`${process.env.SystemRoot || 'C:\\Windows'}\\System32\\taskkill.exe`,
          ['/PID', String(child.pid), '/T', '/F'], { windowsHide: true, stdio: 'ignore' });
        killer.once('error', resolve); killer.once('close', resolve);
      });
    }
    if (!closed) child.kill('SIGKILL');
  })();
  const fail = e => {
    if (closed || failure) return;
    failure = e.message;
    log(`endpoint transport failed: ${failure}`);
    void force().catch(e => log(`endpoint kill failed: ${e.message}`));
  };
  const conn = {
    role: 'endpoint', instance,
    get pid() { return closed ? null : child.pid ?? null; },
    exited,
    send: m => {
      if (closed) return;
      try { writer.write(m).catch(fail); } catch (e) { fail(e); }
    },
    close: () => { void force().catch(e => log(`endpoint kill failed: ${e.message}`)); },
    stop: (graceMs = 3000) => stopPromise ||= (async () => {
      if (closed) return exited;
      // EOF is re's clean shutdown request. Flush queued records before EOF.
      void writer.tail.then(() => { if (!closed && !child.stdin.destroyed) child.stdin.end(); }).catch(fail);
      let timer;
      const done = await Promise.race([exited.then(() => true), new Promise(resolve => { timer = setTimeout(() => resolve(false), graceMs); })]);
      clearTimeout(timer);
      if (!done) await force();
      return exited;
    })()
  };
  const decoder = new Decoder(m => onMessage(conn, m));
  child.stdin.on('error', fail);
  child.stdout.on('error', fail);
  child.stdout.on('data', c => { if (!closed) { try { decoder.push(c); } catch (e) { fail(e); } } });
  child.on('error', fail);
  child.on('close', (code, signal) => {
    if (closed) return;
    closed = true;
    const exit = { code, signal, error: failure, at: new Date().toISOString() };
    log(`endpoint ${instance} exited (${code ?? signal})`);
    resolveExit(exit);
    onClose(conn, exit);
  });
  return conn;
}
