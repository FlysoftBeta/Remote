import test from 'node:test';
import assert from 'node:assert/strict';
import '../frame-rate.js';
const { estimate, controller, monitor } = globalThis.RKFrameRate;
const good = { decoded: 60, decMs: 3, dropped: 0, jbMs: 8, lost: 0, lossRatio: 0, senderFps: 60, encMs: 5 };
function setup(display = 60, maximum = 60) {
  const sent = [], policy = controller((fps, reason) => sent.push({ fps, reason }));
  policy.capabilities(display, maximum);
  return { policy, sent };
}
test('refresh estimate survives fractional refresh, jitter and a few missed callbacks', () => {
  assert.equal(estimate([...Array(50).fill(1000 / 59.94), ...Array(10).fill(50)]), 60);
  assert.equal(estimate(Array(60).fill(1000 / 119.88)), 120);
  assert.equal(estimate(Array(60).fill(1000 / 75)), 75);
  assert.equal(estimate([...Array(25).fill(1000 / 60), ...Array(35).fill(1000 / 30)]), 60);
  assert.equal(estimate([1, NaN, 1000]), null);
});
test('negotiation intersects the display and sender ceiling, including a slower display', () => {
  const { policy } = setup(120, 60);
  assert.equal(policy.get().targetFps, 60);
  policy.capabilities(30);
  assert.equal(policy.get().targetFps, 30);
  policy.capabilities(60, 20);
  assert.equal(policy.get().targetFps, 20);
});
test('static source and occasional losses do not lower a healthy connection', () => {
  const { policy, sent } = setup();
  for (let i = 1; i <= 60; i++) policy.sample({ ...good, decoded: 0, senderFps: 0, decMs: null }, i * 1000);
  policy.sample({ ...good, lost: 20, lossRatio: .1 }, 61000);
  for (let i = 62; i < 70; i++) policy.sample(good, i * 1000);
  assert.equal(sent.length, 1);
  assert.equal(policy.get().targetFps, 60);
});
test('sustained loss reduces offered fps and healthy active traffic recovers', () => {
  const { policy, sent } = setup();
  for (let i = 1; i <= 3; i++) policy.sample({ ...good, lost: 20, lossRatio: .1 }, i * 1000);
  assert.deepEqual(sent.at(-1), { fps: 30, reason: 'loss' });
  for (let i = 4; i <= 13; i++) policy.sample(good, i * 1000);
  assert.deepEqual(sent.at(-1), { fps: 60, reason: 'probe' });
});
test('decoder and encoder overload bound work using measured cost', () => {
  for (const [extra, reason] of [[{ decMs: 20 }, 'decode'], [{ encMs: 20 }, 'encode']]) {
    const { policy, sent } = setup();
    for (let i = 1; i <= 3; i++) policy.sample({ ...good, ...extra }, i * 1000);
    assert.deepEqual(sent.at(-1), { fps: 30, reason });
  }
});
test('sender dropping fresh raw frames lowers the offer; a 5fps source does not', () => {
  const { policy, sent } = setup();
  for (let i = 1; i <= 10; i++) policy.sample({ ...good, decoded: 5, sourceFps: 5, senderFps: 5 }, i * 1000);
  assert.equal(policy.get().targetFps, 60);
  for (let i = 11; i <= 13; i++) policy.sample({ ...good, decoded: 25, sourceFps: 60, senderFps: 25 }, i * 1000);
  assert.deepEqual(sent.at(-1), { fps: 30, reason: 'sender' });
});
test('persistent queueing and receiver drops are actionable without lost packets', () => {
  for (const [extra, reason] of [[{ jbMs: 180 }, 'queue'], [{ dropped: 15 }, 'drops']]) {
    const { policy, sent } = setup();
    for (let i = 1; i <= 3; i++) policy.sample({ ...good, ...extra }, i * 1000);
    assert.equal(sent.at(-1).reason, reason);
  }
});
test('hidden tabs do not adapt from throttled measurements; reconnect removes old penalties', () => {
  const { policy } = setup();
  for (let i = 1; i <= 20; i++) policy.sample({ ...good, hidden: true, decMs: 500 }, i * 1000);
  assert.equal(policy.get().targetFps, 60);
  for (let i = 21; i <= 23; i++) policy.sample({ ...good, decMs: 20 }, i * 1000);
  assert.equal(policy.get().targetFps, 30);
  policy.reset();
  assert.equal(policy.get().targetFps, 60);
});
test('refresh changes require two stable windows and ignore background time', () => {
  let callback, at = 0, hidden = false;
  globalThis.document = { get hidden() { return hidden; } };
  globalThis.requestAnimationFrame = fn => { callback = fn; };
  const changes = [], clock = monitor(fps => changes.push(fps));
  function frames(n, step) { for (let i = 0; i < n; i++) { at += step; callback(at); } }
  frames(121, 1000 / 120);
  assert.equal(clock.get(), 120);
  hidden = true; frames(5, 1000);
  hidden = false; frames(121, 1000 / 60);
  assert.equal(clock.get(), 60);
  assert.deepEqual(changes, [120, 60]);
  delete globalThis.document; delete globalThis.requestAnimationFrame;
});
