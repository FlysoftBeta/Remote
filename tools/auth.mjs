// Shared test login for smoke/latency/soak/multi. Tokens are passed only to the owned test browser.
import crypto from 'node:crypto';
const temporaryPassword = crypto.randomBytes(24).toString('base64url');
export async function testSession(base, options = {}) {
  const argv = process.argv.slice(2), arg = key => { const i = argv.indexOf('--' + key); return i < 0 ? undefined : argv[i + 1]; };
  const provided = arg('test-token') || process.env.RK_TEST_TOKEN;
  if (provided) return provided;
  const username = arg('test-user') || process.env.RK_TEST_USER || options.username || 'rk-test';
  const password = arg('test-password') || process.env.RK_TEST_PASSWORD || options.password || temporaryPassword;
  const serverCredential = arg('server-credential') || process.env.RK_SERVER_CREDENTIAL || process.env.CP_TOKEN || options.serverCredential || '';
  const request = async route => {
    const r = await fetch(base + '/api/auth/' + route, { method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username, password, serverCredential }) });
    const data = await r.json(); return { r, data };
  };
  let { r, data } = await request('login');
  if (r.status === 401) ({ r, data } = await request('register'));
  if (!r.ok) throw new Error('Test login failed: ' + data.error + '. Supply RK_TEST_USER/RK_TEST_PASSWORD or --test-user/--test-password.');
  return data.token;
}
