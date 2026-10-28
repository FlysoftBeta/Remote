import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { InstanceManager } from '../manager.js';
import { Auth } from '../dist/auth.js';

test('instances.json migrates once and is claimed atomically by the first admin', async t => {
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'rk-migrate-'));
  t.after(() => fs.rmSync(dataDir, { recursive: true, force: true }));
  const old = JSON.stringify({ version: 1, profiles: [{ id: 'legacy', name: 'Legacy' }], instances: [{ id: 'legacy', profile: 'legacy', isolation: 'job' }] });
  fs.writeFileSync(path.join(dataDir, 'instances.json'), old);
  const m = new InstanceManager({ dataDir });
  assert.equal(m.get('legacy').owner, null);
  const auth = new Auth(m);
  const first = await auth.create({ username: 'admin', password: 'migration-password', serverCredential: process.env.CP_TOKEN });
  assert.equal(m.get('legacy').owner, first.id); assert.equal(m.listProfiles()[0].owner, first.id);
  const restored = new InstanceManager({ dataDir });
  assert.equal(restored.users[0].id, first.id); assert.equal(restored.get('legacy').owner, first.id);
  fs.writeFileSync(path.join(dataDir, 'instances.json'), '{invalid');
  assert.equal(new InstanceManager({ dataDir }).list().length, 1);
  fs.writeFileSync(m.file, '{invalid');
  assert.throws(() => new InstanceManager({ dataDir }));
  assert.equal(fs.readFileSync(m.file, 'utf8'), '{invalid');
});
