import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { InstanceManager, restartDelay } from '../manager.js';

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(fn) {
  const end = Date.now() + 2000;
  while (!fn()) { if (Date.now() > end) assert.fail('timed out'); await sleep(5); }
}
function setup(t, options = {}) {
  // Override TEMP/TMP in the test command to keep all test artifacts in the task directory.
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'rk-manager-'));
  const children = [];
  const spawn = (exe, id, message, close, log, args) => {
    let closed = false;
    const conn = { pid: 1000 + children.length, args, id,
      exit(code = 1) { if (closed) return; closed = true; close(conn, { code, signal: null, at: new Date().toISOString() }); },
      async stop() { conn.exit(0); }, close() { conn.exit(); } };
    children.push(conn);
    queueMicrotask(() => { if (!closed) message(conn, { t: 'hello', v: 1 }); });
    return conn;
  };
  const manager = new InstanceManager({ dataDir, endpointPath: 're.exe', spawn, log() {}, baseDelay: 10, maxDelay: 80, ...options });
  t.after(async () => { await manager.shutdown(); fs.rmSync(dataDir, { recursive: true, force: true }); });
  return { manager, children, dataDir, spawn };
}

test('durable configs survive reload, and only autostart instances boot', async t => {
  const { manager: m, dataDir, spawn, children } = setup(t);
  await m.patch('default', { autostart: false });
  await m.createProfile({ id: 'work', name: 'Work profile' });
  await m.create({ id: 'work', profile: 'work', autostart: true, jobLimits: { memoryMB: 1024, cpuRate: 5000, processCount: 32 }, chromeArgs: ['--lang=en-US'] });
  await m.create({ id: 'manual', profile: 'default' });
  const saved = JSON.parse(fs.readFileSync(m.file));
  assert.equal(saved.version, 1); assert.equal(saved.instances[1].profile, 'work');
  assert.ok(saved.instances.every(c => !('pid' in c)));
  const restored = new InstanceManager({ dataDir, endpointPath: 're.exe', spawn, log() {} });
  t.after(() => restored.shutdown());
  await restored.boot(); await until(() => restored.get('work').state === 'running');
  assert.equal(restored.get('manual').state, 'stopped');
  assert.equal(restored.get('default').state, 'stopped');
  assert.ok(children[0].args.includes('--job-memory-mb=1024'));
  assert.ok(children[0].args.includes('--job-cpu-rate=5000'));
  assert.ok(children[0].args.includes('--job-process-count=32'));
  assert.ok(children[0].args.includes('--cr-lang=en-US'));
});

test('one profile reservation covers starting, running and backoff; stop cancels restart', async t => {
  const { manager: m, children } = setup(t, { baseDelay: 50 });
  await m.create({ id: 'other', profile: 'default' });
  await Promise.all([m.start('default'), assert.rejects(m.start('other'), { status: 409 })]);
  assert.equal(m.get('default').state, 'running');
  children[0].exit();
  assert.equal(m.get('default').state, 'backoff');
  assert.equal(m.get('default').lastExit.code, 1);
  await assert.rejects(m.start('other'), { status: 409 });
  await m.stop('default'); await sleep(100);
  assert.equal(children.length, 1); assert.equal(m.get('default').state, 'stopped');
  await m.start('other'); assert.equal(children.length, 2);
});

test('crashes restart repeatedly, stable runs reset backoff, and restart is explicit', async t => {
  const { manager: m, children } = setup(t, { stableMs: 1000 });
  await m.start('default'); children[0].exit();
  await until(() => children.length === 2);
  children[1].exit(); assert.equal(m.live('default').failures, 2);
  await until(() => children.length === 3);
  m.stableMs = 0; children[2].exit(); assert.equal(m.live('default').failures, 1);
  await until(() => children.length === 4);
  assert.equal(m.get('default').restarts, 3);
  await m.restart('default'); assert.equal(children.length, 5);
  assert.equal(m.get('default').restarts, 3);
  assert.deepEqual([1, 2, 3, 6, 20].map(n => restartDelay(n)), [1000, 2000, 4000, 30000, 30000]);
});

test('delete stops before removing config; referenced profiles cannot be removed', async t => {
  const { manager: m, children } = setup(t);
  const p = await m.createProfile({ id: 'remove' });
  fs.writeFileSync(path.join(p.userDataDir, 'marker'), 'profile data');
  await m.create({ id: 'remove', profile: 'remove' });
  await assert.rejects(m.deleteProfile('remove'), { status: 409 });
  await m.start('remove'); await m.delete('remove');
  assert.equal(children.length, 1); assert.throws(() => m.get('remove'), { status: 404 });
  await m.deleteProfile('remove'); assert.equal(fs.existsSync(p.userDataDir), false);
});

test('rejects path aliases, reserved switches, invalid limits, and edits to active launch settings', async t => {
  const { manager: m } = setup(t);
  for (const input of [{ id: '..' }, { id: 'UPPER' }, { id: 'a/b' }]) await assert.rejects(m.createProfile(input), { status: 400 });
  for (const input of [null, { id: 'x', profile: 'default', reArgs: ['--cr-user-data-dir=C:/other'] },
    { id: 'x', profile: 'default', chromeArgs: ['--user-data-dir=x'] },
    { id: 'x', profile: 'default', jobLimits: { cpuRate: 10001 } },
    { id: 'x', profile: 'default', isolation: 'off', jobLimits: { memoryMB: 1 } }]) await assert.rejects(m.create(input), { status: 400 });
  await assert.rejects(m.create({ id: 'x', profile: 'missing' }), { status: 404 });
  await m.start('default'); await assert.rejects(m.patch('default', { isolation: 'job' }), { status: 409 });
  await m.patch('default', { name: 'Renamed', autostart: false }); assert.equal(m.get('default').name, 'Renamed');
});

test('failed atomic replacement preserves old configuration and cleans temporary file', async t => {
  const { manager: m, dataDir } = setup(t);
  const before = fs.readFileSync(m.file, 'utf8'), realFile = m.file;
  m.file = path.join(dataDir, 'blocked'); fs.mkdirSync(m.file);
  try { await assert.rejects(m.patch('default', { name: 'Unsaved' })); }
  finally { m.file = realFile; }
  assert.equal(m.get('default').name, 'Default'); assert.equal(fs.readFileSync(realFile, 'utf8'), before);
  assert.equal(fs.readdirSync(dataDir).some(f => f.endsWith('.tmp')), false);
});

test('legacy endpoint profile and arguments import once; invalid store is never overwritten', async t => {
  const { manager: m, dataDir } = setup(t, { legacyArgs: ['--chrome', 'C:/with spaces/chrome.exe', '--user-data-dir=C:/legacy-profile', '--isolation=job', '--job-ui-limits=none'] });
  assert.ok(m.get('default').reArgs.includes('--chrome=C:/with spaces/chrome.exe'));
  assert.equal(m.get('default').userDataDir, path.resolve('C:/legacy-profile'));
  assert.equal(m.listProfiles()[0].managed, false);
  await m.delete('default'); await assert.rejects(m.deleteProfile('default'), { status: 409 });
  fs.writeFileSync(m.file, '{broken');
  assert.throws(() => new InstanceManager({ dataDir })); assert.equal(fs.readFileSync(m.file, 'utf8'), '{broken');
});

test('startup timeout kills stalled endpoint and schedules restart', async t => {
  let killed = 0;
  const { manager: m } = setup(t, { startupMs: 10, baseDelay: 100,
    spawn(exe, id, message, close) {
      const conn = { pid: 1, close() { killed++; close(conn, { code: 1 }); }, async stop() { close(conn, { code: 0 }); } }; return conn;
    } });
  await m.start('default'); assert.equal(m.get('default').state, 'starting');
  await until(() => killed === 1); assert.equal(m.get('default').state, 'backoff');
});
