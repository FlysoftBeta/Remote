// stdio transport for the in-tree endpoint (design/06-protocol.md section 1.4): the same JSON messages as the
// WebSocket endpoint role, framed as 4-byte LE length + UTF-8 JSON on stdin/stdout; stderr is the endpoint log.
import { spawn } from 'node:child_process';
import { Decoder, Writer } from './framing.js';

export function spawnEndpoint(exe, instance, onMessage, onClose, log = console.log) {
  const child = spawn(exe, [`--instance=${instance}`], { stdio: ['pipe', 'pipe', 'inherit'], windowsHide: true });
  const writer = new Writer(child.stdin);
  const conn = {
    role: 'endpoint', instance,
    send: m => { writer.write(m).catch(e => log(`endpoint write failed: ${e.message}`)); },
    close: () => child.kill()
  };
  const decoder = new Decoder(m => onMessage(conn, m));
  child.stdout.on('data', c => { try { decoder.push(c); } catch (e) { log(`endpoint protocol error: ${e.message}`); child.kill(); } });
  child.on('error', e => log(`endpoint spawn failed: ${e.message}`));
  child.on('exit', code => { log(`endpoint exited (${code})`); onClose(conn); });
  return conn;
}
