import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocket } from 'ws';

test('management HTTP auth, validation, CRUD, persistence and existing signalling', { timeout: 30000 }, async t => {
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'rk-api-'));
  const sockets = [], children = [];
  t.after(async () => {
    for (const ws of sockets) ws.terminate();
    await Promise.all(children.map(child => new Promise(resolve => {
      if (child.exitCode !== null || child.signalCode !== null) return resolve();
      child.once('exit', resolve); child.kill();
    })));
    fs.rmSync(dataDir, { recursive: true, force: true });
  });
  async function server() {
    const child = spawn(process.execPath, [fileURLToPath(new URL('../index.js', import.meta.url)), '--port', '0'], {
      env: { ...process.env, HOST: '127.0.0.1', CP_TOKEN: 'test-token', CP_DATA_DIR: dataDir, ENDPOINT_PATH: '', ENDPOINT_ARGS: '', ENDPOINT_ARGS_JSON: '',
        TLS_CERT: '', TLS_KEY: '', TLS_PFX: '', TLS_CA: '', HTTP_REDIRECT_PORT: '' }, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe']
    });
    children.push(child);
    const base = await new Promise((resolve, reject) => {
      let output = '';
      const timer = setTimeout(() => reject(new Error('CP timeout: ' + output)), 10000);
      child.stdout.on('data', chunk => { output += chunk; const m = /http:\/\/localhost:(\d+)\//.exec(output); if (m) { clearTimeout(timer); resolve('http://127.0.0.1:' + m[1]); } });
      child.stderr.on('data', chunk => { output += chunk; });
      child.once('error', reject);
      child.once('exit', code => { clearTimeout(timer); reject(new Error(`CP exit ${code}: ${output}`)); });
    });
    return { base, child };
  }
  const { base, child } = await server();
  async function api(route, method = 'GET', body, status = 200) {
    const r = await fetch(base + '/api/' + route, { method, headers: { Authorization: 'Bearer test-token', 'Content-Type': 'application/json' }, body: body === undefined ? undefined : JSON.stringify(body) });
    assert.equal(r.status, status, await r.clone().text());
    return status === 204 ? null : r.json();
  }
  assert.equal((await fetch(base + '/api/instances')).status, 401);
  assert.equal((await fetch(base + '/api/profiles?token=test-token')).status, 200);
  await api('profiles', 'POST', { id: 'work', name: 'Work' }, 201);
  await api('instances', 'POST', { id: 'work', profile: 'work', name: 'Work browser' }, 201);
  await api('instances/work', 'PATCH', { name: 'Renamed' });
  assert.equal((await api('instances/work')).name, 'Renamed');
  const list = await api('instances'), item = await api('instances/work');
  assert.deepEqual(list.find(row => row.id === 'work'), item);
  for (const key of ['id', 'name', 'profile', 'isolation', 'state', 'pid', 'restarts', 'lastExit', 'nextRestartAt', 'viewerConnected', 'autostart']) assert.ok(key in item, `picker metadata: ${key}`);
  assert.equal(item.name, 'Renamed'); assert.equal(item.state, 'stopped'); assert.equal(item.viewerConnected, false);
  for (const action of ['start', 'restart']) await api('instances/work/' + action, 'POST', undefined, 409);
  assert.equal((await api('instances/work/stop', 'POST')).state, 'stopped');
  await api('profiles/work', 'DELETE', undefined, 409);
  await api('instances', 'POST', null, 400);
  await api('instances', 'POST', [], 400);
  await api('instances/missing', 'GET', undefined, 404);
  const denied = await fetch(base + '/api/profiles', { method: 'POST', headers: { Authorization: 'Bearer test-token', Origin: 'http://elsewhere.invalid' }, body: '{}' });
  assert.equal(denied.status, 403);
  const connect = async role => {
    const ws = new WebSocket(base.replace('http:', 'ws:') + `/signal?role=${role}&instance=default&token=test-token`); sockets.push(ws);
    await new Promise((resolve, reject) => { ws.once('open', resolve); ws.once('error', reject); });
    return ws;
  };
  const endpoint = await connect('endpoint');
  endpoint.send(JSON.stringify({ t: 'hello', v: 1, name: 'test-external' }));
  const viewer = await connect('viewer');
  const welcome = new Promise(resolve => viewer.on('message', data => { const m = JSON.parse(data); if (m.t === 'welcome') resolve(m); }));
  viewer.send(JSON.stringify({ t: 'hello', v: 1 }));
  assert.equal((await welcome).instance, 'default');
  assert.equal((await api('instances/default')).viewerConnected, true);
  await new Promise(resolve => { child.once('exit', resolve); child.kill(); });
  const restarted = await server();
  const rows = await (await fetch(restarted.base + '/api/instances?token=test-token')).json();
  assert.equal(rows.find(c => c.id === 'work').name, 'Renamed');
  await new Promise(resolve => { restarted.child.once('exit', resolve); restarted.child.kill(); });
  // Remove through the API on a fresh process so both persisted delete paths are checked.
  const third = await server();
  for (const route of ['instances/work', 'profiles/work']) {
    const r = await fetch(third.base + '/api/' + route, { method: 'DELETE', headers: { Authorization: 'Bearer test-token' } }); assert.equal(r.status, 204);
  }
  assert.equal(fs.existsSync(path.join(dataDir, 'profiles', 'work')), false);
});
