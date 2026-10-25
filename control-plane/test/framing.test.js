import test from 'node:test';
import assert from 'node:assert/strict';
import { encode, Decoder } from '../framing.js';

test('framing roundtrip with fragmentation', () => {
  const got = [];
  const d = new Decoder(m => got.push(m));
  const buf = Buffer.concat([encode({ t: 'hello', v: 1 }), encode({ t: 'ice', sid: 'x' })]);
  for (const byte of buf) d.push(Buffer.from([byte]));
  d.end();
  assert.deepEqual(got, [{ t: 'hello', v: 1 }, { t: 'ice', sid: 'x' }]);
});

test('rejects non-object records', () => {
  assert.throws(() => new Decoder(() => {}).push(encode([1])));
});
