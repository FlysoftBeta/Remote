import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import { Endpoint } from '../endpoint.js';

const exe = process.env.ENDPOINT_PATH || fileURLToPath(new URL('../../endpoint/target/debug/endpoint.exe', import.meta.url));

test('endpoint hello/ping/shutdown', { skip: !fs.existsSync(exe) && 'endpoint not built' }, async () => {
  const ep = new Endpoint(exe);
  const hello = await ep.hello();
  assert.equal(hello.name, 'remotekit-endpoint');
  assert.deepEqual(await ep.call('ping'), {});
  await assert.rejects(ep.call('nope'), /unknown method/);
  await ep.stop();
});
