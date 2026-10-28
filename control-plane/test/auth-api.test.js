import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocket } from 'ws';

async function setup(t, credential = 'test-credential') {
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'rk-api-')), children = [], sockets = [];
  t.after(async () => {
    for (const ws of sockets) ws.terminate();
    await Promise.all(children.map(stop));
    fs.rmSync(dataDir, { recursive: true, force: true });
  });
  async function stop(child) {
    if (child.exitCode !== null || child.signalCode !== null) return;
    await new Promise(resolve => { child.once('exit', resolve); child.kill(); });
  }
  async function server() {
    const child = spawn(process.execPath, [fileURLToPath(new URL('../index.js', import.meta.url)), '--port', '0'], {
      env: { ...process.env, HOST: '127.0.0.1', CP_TOKEN: credential, CP_DATA_DIR: dataDir, ENDPOINT_PATH: '', ENDPOINT_ARGS: '', ENDPOINT_ARGS_JSON: '',
        TLS_CERT: '', TLS_KEY: '', TLS_PFX: '', TLS_CA: '', HTTP_REDIRECT_PORT: '' }, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe']
    }); children.push(child);
    const base = await new Promise((resolve, reject) => {
      let output = '';
      const timer = setTimeout(() => reject(new Error('CP timeout: ' + output)), 10000);
      child.stdout.on('data', chunk => { output += chunk; const m = /http:\/\/localhost:(\d+)\//.exec(output); if (m) { clearTimeout(timer); resolve('http://127.0.0.1:' + m[1]); } });
      child.stderr.on('data', chunk => { output += chunk; });
      child.once('error', reject); child.once('exit', code => { clearTimeout(timer); reject(new Error(`CP exit ${code}: ${output}`)); });
    });
    return { base, child };
  }
  let current = await server();
  async function api(route, method = 'GET', body, status = 200, token = '') {
    const r = await fetch(current.base + '/api/' + route, { method, headers: { Authorization: 'Bearer ' + token, 'Content-Type': 'application/json' }, body: body === undefined ? undefined : JSON.stringify(body) });
    assert.equal(r.status, status, route + ': ' + await r.clone().text());
    return status === 204 ? null : r.json();
  }
  async function socket(role, token, instance = 'default', code) {
    const ws = new WebSocket(current.base.replace('http:', 'ws:') + `/signal?role=${role}&instance=${instance}&token=${token}`); sockets.push(ws);
    const closed = new Promise(resolve => ws.once('close', resolve));
    await new Promise((resolve, reject) => { ws.once('open', resolve); ws.once('error', reject); });
    if (code) { assert.equal(await closed, code); return; }
    ws.closed = closed; return ws;
  }
  return { api, socket, dataDir, base: () => current.base, restart: async () => { await stop(current.child); current = await server(); } };
}

test('registration credential, users, ownership, persistence, revocation and signalling', { timeout: 30000 }, async t => {
  const { api, socket, dataDir, restart, base } = await setup(t);
  const first = { username: 'admin', password: 'admin-password', serverCredential: 'test-credential' };
  assert.deepEqual(await api('auth/config'), { registrationCredentialRequired: true, firstUser: true });
  await api('instances', 'GET', undefined, 401);
  await api('auth/register', 'POST', { ...first, serverCredential: '' }, 401);
  const admin = await api('auth/register', 'POST', first, 201);
  assert.equal(admin.user.role, 'admin'); assert.ok(admin.token.length >= 32);
  assert.equal((await api('instances/default', 'GET', undefined, 200, admin.token)).owner, admin.user.id);
  const alice = await api('auth/register', 'POST', { username: 'alice', password: 'alice-password', serverCredential: first.serverCredential, role: 'admin' }, 201);
  assert.equal(alice.user.role, 'user');
  await api('auth/login', 'POST', { username: 'alice', password: 'wrong' }, 401);
  await api('auth/register', 'POST', { username: 'alice', password: 'another-password', serverCredential: first.serverCredential }, 409);
  await api('auth/register', 'POST', { username: 'bad', password: 'short', serverCredential: first.serverCredential }, 400);
  assert.deepEqual(await api('instances', 'GET', undefined, 200, alice.token), []);
  await api('users', 'GET', undefined, 403, alice.token);
  for (const method of ['GET', 'PATCH', 'DELETE']) await api('instances/default', method, method === 'PATCH' ? { name: 'no' } : undefined, 403, alice.token);
  for (const action of ['start', 'stop', 'restart']) await api('instances/default/' + action, 'POST', undefined, 403, alice.token);
  await api('profiles/default', 'DELETE', undefined, 403, alice.token);
  await api('profiles', 'POST', { id: 'alice', owner: admin.user.id }, 403, alice.token);
  await api('profiles', 'POST', { id: 'alice' }, 201, alice.token);
  await api('instances', 'POST', { id: 'stolen', profile: 'default' }, 403, alice.token);
  await api('instances', 'POST', { id: 'alice', profile: 'alice' }, 201, alice.token);
  assert.equal((await api('instances', 'GET', undefined, 200, alice.token)).length, 1);
  await api('instances/alice', 'PATCH', { name: 'Alice browser' }, 200, alice.token);
  await api('instances/alice', 'PATCH', { owner: admin.user.id }, 400, alice.token);
  await api('instances/alice', 'PATCH', { profile: 'default' }, 400, alice.token);
  await api('instances', 'POST', null, 400, alice.token);
  await api('instances/alice/start', 'POST', undefined, 409, alice.token);
  await api('profiles/alice', 'DELETE', undefined, 409, alice.token);
  const denied = await fetch(base() + '/api/profiles', { method: 'POST', headers: { Authorization: 'Bearer ' + admin.token, Origin: 'http://elsewhere.invalid' }, body: '{}' });
  assert.equal(denied.status, 403);
  const publicUsers = await api('users', 'GET', undefined, 200, admin.token);
  assert.equal(publicUsers.length, 2); assert.deepEqual(Object.keys(publicUsers[0]).sort(), ['id', 'role', 'username']);
  await socket('viewer', 'test-credential', 'default', 4401);
  await socket('viewer', alice.token, 'default', 4403);
  await socket('endpoint', alice.token, 'default', 4403);
  const endpoint = await socket('endpoint', admin.token);
  endpoint.send(JSON.stringify({ t: 'hello', v: 1, name: 'test-external' }));
  const viewer = await socket('viewer', admin.token);
  const welcome = new Promise(resolve => viewer.on('message', data => { const m = JSON.parse(data); if (m.t === 'welcome') resolve(m); }));
  viewer.send(JSON.stringify({ t: 'hello', v: 1 }));
  assert.equal((await welcome).instance, 'default');
  assert.equal((await api('instances/default', 'GET', undefined, 200, admin.token)).viewerConnected, true);
  const saved = JSON.parse(fs.readFileSync(path.join(dataDir, 'config.json'), 'utf8'));
  assert.ok(saved.users.every(u => u.passwordHash.length === 128 && u.salt.length === 32));
  assert.ok(!JSON.stringify(saved).includes(first.password)); assert.ok(!JSON.stringify(saved).includes(admin.token));
  await restart();
  assert.equal((await api('instances/alice', 'GET', undefined, 200, admin.token)).name, 'Alice browser');
  await api('auth/me', 'GET', undefined, 200, alice.token);
  const aliceViewer = await socket('viewer', alice.token, 'alice');
  aliceViewer.send(JSON.stringify({ t: 'hello', v: 1 }));
  await api('users/' + alice.user.id + '/password', 'POST', { password: 'new-alice-password' }, 204, admin.token);
  assert.equal(await aliceViewer.closed, 4401);
  await api('auth/me', 'GET', undefined, 401, alice.token);
  await api('auth/login', 'POST', { username: 'alice', password: 'alice-password' }, 401);
  const reset = await api('auth/login', 'POST', { username: 'alice', password: 'new-alice-password' });
  await api('auth/logout', 'POST', undefined, 204, reset.token);
  await api('auth/me', 'GET', undefined, 401, reset.token);
  await api('users/' + admin.user.id, 'DELETE', undefined, 409, admin.token);
  await api('users/' + alice.user.id, 'DELETE', undefined, 409, admin.token);
  for (const route of ['instances/alice', 'profiles/alice']) await api(route, 'DELETE', undefined, 204, admin.token);
  await api('users/' + alice.user.id, 'DELETE', undefined, 204, admin.token);
  const created = await api('users', 'POST', { username: 'bob', password: 'bob-password' }, 201, admin.token);
  assert.equal(created.role, 'user');
  await api('users/' + created.id, 'DELETE', undefined, 204, admin.token);
  await restart();
  assert.equal((await api('users', 'GET', undefined, 200, admin.token)).length, 1);
  assert.equal(fs.existsSync(path.join(dataDir, 'profiles', 'alice')), false);
});

test('open registration elects exactly one first admin concurrently', async t => {
  const { api } = await setup(t, '');
  assert.equal((await api('auth/config')).registrationCredentialRequired, false);
  const users = await Promise.all(['one', 'two'].map(username => api('auth/register', 'POST', { username, password: 'test-password' }, 201)));
  assert.equal(users.filter(s => s.user.role === 'admin').length, 1);
});
