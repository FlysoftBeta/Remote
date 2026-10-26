// stdio transport for the in-tree endpoint (design/06-protocol.md section 1.4): the same JSON messages as the
// WebSocket endpoint role, framed as 4-byte LE length + UTF-8 JSON on stdin/stdout; stderr is the endpoint log.
import { spawn } from 'node:child_process';
import { Decoder, Writer } from './framing.js';

export function spawnEndpoint(exe, instance, onMessage, onClose, log = console.log) {
  // Field launcher uses an array so executable/profile paths may contain spaces.
  const extra = process.env.ENDPOINT_ARGS_JSON ? JSON.parse(process.env.ENDPOINT_ARGS_JSON) :
    (process.env.ENDPOINT_ARGS || '').split(' ').filter(Boolean);
  if (!Array.isArray(extra) || extra.some(arg => typeof arg !== 'string'))
    throw new Error('ENDPOINT_ARGS_JSON must be an array of strings');
  const child = spawn(exe, [`--instance=${instance}`, ...extra], { stdio: ['pipe', 'pipe', 'inherit'], windowsHide: true });
  const writer = new Writer(child.stdin);
  let closed = false;
  const finish = () => { if (!closed) { closed = true; onClose(conn); } };
  const fail = e => { if (closed) return; log(`endpoint transport failed: ${e.message}`); finish(); child.kill(); };
  const conn = {
    role: 'endpoint', instance,
    send: m => {
      if (closed) return;
      try { writer.write(m).catch(fail); } catch (e) { fail(e); }
    },
    close: () => child.kill()
  };
  const decoder = new Decoder(m => onMessage(conn, m));
  child.stdin.on('error', fail);
  child.stdout.on('error', fail);
  child.stdout.on('data', c => { if (!closed) { try { decoder.push(c); } catch (e) { fail(e); } } });
  child.on('error', fail);
  child.on('exit', code => { log(`endpoint exited (${code})`); finish(); });
  return conn;
}
