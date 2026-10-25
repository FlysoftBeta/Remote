// remotekit control-plane sample (D13): static viewer + WebSocket signalling + instance API.
// M0 skeleton: serves public/, starts the endpoint and answers /api/health. Signalling arrives in M1.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocketServer } from 'ws';
import { Endpoint } from './endpoint.js';

const root = fileURLToPath(new URL('./', import.meta.url));
const exe = process.env.ENDPOINT_PATH || path.join(root, '../endpoint/target/debug/endpoint.exe');
const types = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css' };

const endpoint = new Endpoint(exe);
const hello = await endpoint.hello();

const server = http.createServer((req, res) => {
  const url = new URL(req.url, 'http://x');
  if (url.pathname === '/api/health') {
    res.writeHead(200, { 'content-type': 'application/json' });
    return res.end(JSON.stringify({ ok: true, endpoint: hello }));
  }
  const file = path.join(root, 'public', url.pathname === '/' ? 'index.html' : path.normalize(url.pathname));
  if (!file.startsWith(path.join(root, 'public'))) { res.writeHead(403); return res.end(); }
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404); return res.end(); }
    res.writeHead(200, { 'content-type': types[path.extname(file)] || 'application/octet-stream' });
    res.end(data);
  });
});
const wss = new WebSocketServer({ server, path: '/signal' });
wss.on('connection', ws => ws.send(JSON.stringify({ type: 'hello', endpoint: hello })));

server.listen(Number(process.env.PORT ?? 8080), () => console.log(`control-plane on :${server.address().port}`));
for (const sig of ['SIGINT', 'SIGTERM']) process.on(sig, async () => { await endpoint.stop(); process.exit(0); });
