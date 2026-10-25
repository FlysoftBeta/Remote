import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import { Endpoint } from '../endpoint.js';

const exe = process.env.ENDPOINT_PATH || 'C:/src/chromium/src/out/dev/prts_endpoint.exe';

test('endpoint hello/ping/shutdown', { skip: !fs.existsSync(exe) && 'endpoint not built' }, async () => {
  const ep = new Endpoint(exe);
  const hello = await ep.hello();
  assert.equal(typeof hello.name, 'string');
  assert.deepEqual(await ep.call('ping'), {});
  await assert.rejects(ep.call('nope'), /unknown method/);
  await ep.stop();
});
