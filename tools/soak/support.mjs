import { spawnSync } from 'node:child_process';

export const sleep = ms => new Promise(r => setTimeout(r, ms));
export function ps(script) {
  const r = spawnSync('powershell', ['-NoProfile', '-NonInteractive', '-Command', script], {
    encoding: 'utf8', windowsHide: true, timeout: 20000, maxBuffer: 16 * 1024 * 1024
  });
  if (r.error || r.status !== 0) throw new Error(r.error?.message || r.stderr || `PowerShell exit ${r.status}`);
  return r.stdout.trim();
}
export function processes() {
  return JSON.parse(ps("$ErrorActionPreference='Stop'; $rows=@(Get-CimInstance Win32_Process | Select-Object ProcessId,ParentProcessId,Name,ExecutablePath,CommandLine,@{n='Created';e={$_.CreationDate.ToUniversalTime().ToString('o')}},PrivatePageCount,HandleCount); ConvertTo-Json -Compress -InputObject $rows"));
}
export function idleBuild() {
  return JSON.parse(ps("$rows=@(Get-Process siso*,clang-cl,lld-link -ErrorAction SilentlyContinue | ForEach-Object { try { $_.PriorityClass='Idle'; [pscustomobject]@{pid=$_.Id;name=$_.ProcessName;priority=$_.PriorityClass.ToString()} } catch { [pscustomobject]@{pid=$_.Id;error=$_.Exception.Message} } }); ConvertTo-Json -Compress -InputObject $rows"));
}
export function killOwned(row) {
  killOwnedMany([row], true);
}
export function killOwnedMany(rows, wait = false) {
  for (const row of rows) if (!row || !Number.isInteger(row.ProcessId) || !/^[-:.TZ\d]+$/.test(row.Created)) throw new Error('invalid process identity');
  // Creation time is checked immediately before each kill to reject PID reuse.
  if (!rows.length) return;
  const identities = rows.map(row => `@{id=${row.ProcessId};created='${row.Created}'}`).join(',');
  const waitCode = wait ? "$remaining=Get-Process -Id $r.id -ErrorAction SilentlyContinue; if ($remaining) { Wait-Process -InputObject $remaining -Timeout 10 -ErrorAction Stop }" : '';
  ps(`$ErrorActionPreference='Stop'; foreach ($r in @(${identities})) { $p=Get-CimInstance Win32_Process -Filter ('ProcessId='+$r.id); if ($p -and $p.CreationDate.ToUniversalTime().ToString('o') -eq $r.created) { Stop-Process -Id $r.id -Force -ErrorAction SilentlyContinue; ${waitCode} } ${wait ? "else { throw 'fault target exited or PID identity changed before injection' }" : ''} }`);
}
export async function json(url) {
  const r = await fetch(url, { signal: AbortSignal.timeout(2500) });
  if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
  return r.json();
}
export class CDP {
  constructor(ws) { this.ws = ws; this.next = 0; this.pending = new Map(); }
  static async connect(url) {
    const c = new CDP(new WebSocket(url));
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => { c.ws.close(); reject(new Error('CDP connect timeout')); }, 5000);
      c.ws.onopen = () => { clearTimeout(timer); resolve(); };
      c.ws.onerror = () => { clearTimeout(timer); reject(new Error('CDP socket error')); };
    });
    c.ws.onmessage = e => {
      const m = JSON.parse(e.data), p = c.pending.get(m.id);
      if (!p) return;
      clearTimeout(p.timer); c.pending.delete(m.id);
      m.error ? p.reject(new Error(m.error.message)) : p.resolve(m.result);
    };
    return c;
  }
  call(method, params = {}) {
    return new Promise((resolve, reject) => {
      const id = ++this.next;
      const timer = setTimeout(() => { this.pending.delete(id); reject(new Error(`CDP timeout: ${method}`)); }, 5000);
      this.pending.set(id, { resolve, reject, timer });
      this.ws.send(JSON.stringify({ id, method, params }));
    });
  }
  async evaluate(expression) {
    const r = await this.call('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
    if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails));
    return r.result.value;
  }
  close() {
    for (const p of this.pending.values()) { clearTimeout(p.timer); p.reject(new Error('CDP closed')); }
    this.pending.clear(); this.ws.close();
  }
}

export function slope(samples, key) {
  const rows = samples.filter(s => Number.isFinite(s[key]));
  if (rows.length < 3) return null;
  const t0 = rows[0].at, x = rows.map(s => (s.at - t0) / 3600000);
  const mx = x.reduce((a, b) => a + b) / x.length;
  const my = rows.reduce((a, b) => a + b[key], 0) / rows.length;
  const den = x.reduce((a, b) => a + (b - mx) ** 2, 0);
  return den ? rows.reduce((a, b, i) => a + (x[i] - mx) * (b[key] - my), 0) / den : null;
}
