import 'fake-indexeddb/auto';
import test from 'node:test';
import assert from 'node:assert/strict';
import { remembered } from '../src/auth.ts';

test('IDB sessions persist, remain scoped to the server, and are removed on sign out', async () => {
  const login = { token: 'random-session', user: { id: 'user-1', username: 'alice', role: 'user' }, expiresAt: Date.now() + 10000 };
  assert.equal(await remembered('https://cp-a'), null);
  await remembered('https://cp-a', login);
  assert.deepEqual(await remembered('https://cp-a'), login);
  assert.equal(await remembered('https://cp-b'), null);
  await remembered('https://cp-a', null);
  assert.equal(await remembered('https://cp-a'), null);
});
