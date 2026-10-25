import test from 'node:test';
import assert from 'node:assert/strict';
import { encode, Decoder } from '../framing.js';

test('framing roundtrip with fragmentation', () => {
  const got = [];
  const d = new Decoder(m => got.push(m));
  const buf = Buffer.concat([encode({ v: 1, a: 1 }), encode({ v: 1, b: 'x' })]);
  for (const byte of buf) d.push(Buffer.from([byte]));
  d.end();
  assert.deepEqual(got, [{ v: 1, a: 1 }, { v: 1, b: 'x' }]);
});

test('rejects wrong version', () => {
  assert.throws(() => new Decoder(() => {}).push(encode({ v: 2 })));
});
