// Durable configuration; live processes and restart timers are deliberately not persisted.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnEndpoint } from './endpoint.js';

export class ApiError extends Error {
  status: number;
  constructor(status, message) { super(message); this.status = status; }
}
const bad = message => { throw new ApiError(400, message); };
// Lowercase IDs also avoid aliases on Windows' case-insensitive filesystem.
const idPattern = /^[a-z0-9][a-z0-9_-]{0,63}$/;
function id(value) { if (typeof value !== 'string' || !idPattern.test(value)) bad('id must be 1..64 lowercase letters, digits, _ or -, beginning with a letter or digit'); return value; }
function name(value) { if (typeof value !== 'string' || !value.trim() || value.length > 200) bad('name must be 1..200 characters'); return value; }
function object(value) { if (!value || typeof value !== 'object' || Array.isArray(value)) bad('JSON object required'); }
function keys(value, allowed) { object(value); for (const key of Object.keys(value)) if (!allowed.includes(key)) bad(`unknown field: ${key}`); }
function args(value, chrome = false) {
  if (!Array.isArray(value) || value.length > 128 || value.some(a => typeof a !== 'string' || a.length > 8192 || a.includes('\0'))) bad('args must be an array of up to 128 strings');
  for (const a of value) {
    if (!/^--[a-zA-Z0-9_-]+(?:=.*)?$/s.test(a)) bad('use --switch or --switch=value arguments');
    const key = a.slice(2).split('=')[0].toLowerCase();
    if (key === 'user-data-dir' || (!chrome && (['instance', 'isolation'].includes(key) || key.startsWith('job-') || key === 'cr-user-data-dir'))) bad(`reserved argument: ${a}`);
  }
  return [...value];
}
const instanceFields = ['id', 'name', 'profile', 'isolation', 'jobLimits', 'jobUiLimits', 'reArgs', 'chromeArgs', 'autostart', 'owner'];
function config(input) {
  keys(input, instanceFields);
  const result = { id: id(input.id), name: name(input.name ?? input.id), profile: id(input.profile), owner: input.owner ?? null as string | null,
    isolation: input.isolation ?? 'job', jobLimits: (input.jobLimits ?? {}) as Record<string, number>, jobUiLimits: input.jobUiLimits ?? 'none',
    reArgs: args(input.reArgs ?? []), chromeArgs: args(input.chromeArgs ?? [], true), autostart: input.autostart ?? false };
  if (!['off', 'job', 'lpac', 'appcontainer'].includes(result.isolation)) bad('invalid isolation mode');
  keys(result.jobLimits, ['memoryMB', 'cpuRate', 'processCount']);
  result.jobLimits = { ...result.jobLimits };
  for (const [key, value] of Object.entries(result.jobLimits)) {
    const max = { memoryMB: 1048576, cpuRate: 10000, processCount: 65535 }[key];
    if (!Number.isInteger(value) || value < 1 || value > max) bad(`jobLimits.${key} must be 1..${max}`);
  }
  const ui = ['none', 'all', 'handles', 'readclipboard', 'writeclipboard', 'systemparameters', 'displaysettings', 'globalatoms', 'desktop', 'exitwindows'];
  if (typeof result.jobUiLimits !== 'string' || result.jobUiLimits.split(',').some(v => !ui.includes(v))) bad('invalid jobUiLimits');
  if (result.isolation === 'off' && (Object.keys(result.jobLimits).length || result.jobUiLimits !== 'none')) bad('job limits require isolation=job|lpac|appcontainer');
  if (typeof result.autostart !== 'boolean') bad('autostart must be boolean');
  if (result.owner !== null) id(result.owner);
  return result;
}

export function restartDelay(failures, base = 1000, max = 30000) {
  return Math.min(max, base * 2 ** Math.min(30, Math.max(0, failures - 1)));
}

// Windows Chromium holds lockfile with GENERIC_WRITE and FILE_SHARE_READ.
// Opening an existing file for write detects that live owner without modifying
// the file. Existence alone is insufficient: an abandoned file is harmless.
export function checkProfileAvailable(dir) {
  if (process.platform !== 'win32') return;
  try { const fd = fs.openSync(path.join(dir, 'lockfile'), 'r+'); fs.closeSync(fd); }
  catch (e) {
    if (e.code === 'ENOENT') return;
    if (e.code === 'EBUSY') throw new ApiError(409, `Profile directory is already in use by another browser: ${dir}. Close that browser, then start the instance again.`);
    throw new ApiError(409, `Cannot access profile lock in ${dir} (${e.code}). Close any browser using this profile and check its permissions, then start the instance again.`);
  }
}

export class InstanceManager {
  root: string; file: string; profileRoot: string; endpointPath: string; chromePath: string;
  onMessage: Function; onClose: Function; viewerConnected: Function; log: Function; spawn: Function;
  baseDelay: number; maxDelay: number; stableMs: number; startupMs: number; graceMs: number;
  instances = new Map<string, ReturnType<typeof config>>();
  profiles = new Map<string, { id: string; name: string; owner: string | null; externalPath?: string }>();
  runtime = new Map<string, any>();
  users: import('./auth.js').StoredUser[] = [];
  serverCredential = '';
  queue: Promise<any>; closing: boolean;
  constructor({ dataDir, endpointPath = '', chromePath = '', legacyArgs = [], onMessage = (..._args: any[]) => {}, onClose = (..._args: any[]) => {},
    viewerConnected = (..._args: any[]) => false, log = console.log, spawn = spawnEndpoint,
    baseDelay = 1000, maxDelay = 30000, stableMs = 60000, startupMs = 90000, graceMs = 3000 }) {
    this.root = path.resolve(dataDir); this.file = path.join(this.root, 'config.json');
    this.profileRoot = path.join(this.root, 'profiles');
    fs.mkdirSync(this.profileRoot, { recursive: true });
    this.endpointPath = endpointPath; this.onMessage = onMessage; this.onClose = onClose;
    this.chromePath = chromePath || path.join(path.dirname(endpointPath), 'chrome.exe');
    this.viewerConnected = viewerConnected; this.log = log; this.spawn = spawn;
    Object.assign(this, { baseDelay, maxDelay, stableMs, startupMs, graceMs });
    this.instances = new Map(); this.profiles = new Map(); this.runtime = new Map();
    this.queue = Promise.resolve(); this.closing = false;
    const legacyFile = path.join(this.root, 'instances.json');
    const loadFile = fs.existsSync(this.file) ? this.file : fs.existsSync(legacyFile) ? legacyFile : null;
    if (loadFile) {
      const saved = JSON.parse(fs.readFileSync(loadFile, 'utf8'));
      if (saved.version !== 1 || !Array.isArray(saved.instances) || !Array.isArray(saved.profiles)) throw new Error('invalid configuration schema');
      if (loadFile === this.file && (!Array.isArray(saved.users) || typeof saved.serverCredential !== 'string')) throw new Error('invalid config.json users/credential');
      this.users = saved.users ?? [];
      this.serverCredential = saved.serverCredential ?? '';
      for (const u of this.users) {
        if (!u || !idPattern.test(u.id) || !idPattern.test(u.username) || !['admin', 'user'].includes(u.role) ||
            !/^[a-f0-9]{32}$/.test(u.salt) || !/^[a-f0-9]{128}$/.test(u.passwordHash) || !Array.isArray(u.sessions) ||
            u.sessions.some(s => !/^[a-f0-9]{64}$/.test(s.hash) || !Number.isFinite(s.expiresAt))) throw new Error('invalid persisted user');
      }
      if (new Set(this.users.map(u => u.id)).size !== this.users.length || new Set(this.users.map(u => u.username)).size !== this.users.length ||
          (this.users.length && !this.users.some(u => u.role === 'admin'))) throw new Error('invalid persisted users');
      for (const p of saved.profiles) {
        keys(p, ['id', 'name', 'externalPath', 'owner']); id(p.id); name(p.name); p.owner ??= null;
        if (this.profiles.has(p.id)) throw new Error('duplicate persisted profile');
        if (p.externalPath && (p.id !== 'default' || typeof p.externalPath !== 'string' || !path.isAbsolute(p.externalPath))) throw new Error('invalid legacy profile');
        this.profiles.set(p.id, p);
      }
      for (const input of saved.instances) {
        const c = config(input);
        if (this.instances.has(c.id) || !this.profiles.has(c.profile)) throw new Error('invalid persisted instance/profile reference');
        this.instances.set(c.id, c);
      }
      for (const p of this.profiles.values()) if (p.owner !== null && !this.users.some(u => u.id === p.owner)) throw new Error('invalid profile owner');
      for (const c of this.instances.values()) if (c.owner !== this.profiles.get(c.profile).owner) throw new Error('instance/profile owner mismatch');
      if (loadFile !== this.file) this.save(); // Leave the legacy file untouched; config.json wins on every later boot.
    } else {
      // Import the old default command once, including its explicitly selected profile.
      let profile, isolation = 'off', jobUiLimits = 'none'; const reArgs = [], jobLimits: Record<string, number> = {};
      const special = { 'user-data-dir': v => { profile = path.resolve(v); }, isolation: v => { isolation = v; },
        'job-ui-limits': v => { jobUiLimits = v; }, 'job-memory-mb': v => { jobLimits.memoryMB = Number(v); },
        'job-cpu-rate': v => { jobLimits.cpuRate = Number(v); }, 'job-process-count': v => { jobLimits.processCount = Number(v); } };
      for (let i = 0; i < legacyArgs.length; i++) {
        const match = /^--([^=]+)(?:=(.*))?$/s.exec(legacyArgs[i]);
        if (match && special[match[1]]) { const v = match[2] ?? legacyArgs[++i]; if (!v || v.startsWith('--')) bad(`missing value for ${match[1]}`); special[match[1]](v); }
        else if (match && match[1] !== 'instance') {
          let a = legacyArgs[i];
          if (match[2] === undefined && legacyArgs[i + 1] && !legacyArgs[i + 1].startsWith('--')) a += '=' + legacyArgs[++i];
          reArgs.push(a);
        } else bad('invalid legacy endpoint argument');
      }
      this.profiles.set('default', { id: 'default', name: 'Default', owner: null, ...(profile ? { externalPath: profile } : {}) });
      this.instances.set('default', config({ id: 'default', name: 'Default', profile: 'default', isolation, jobUiLimits, jobLimits, reArgs, autostart: !!endpointPath }));
      this.save();
    }
  }
  save(instances = this.instances, profiles = this.profiles, users = this.users) {
    const tmp = this.file + '.' + crypto.randomUUID() + '.tmp';
    try {
      const fd = fs.openSync(tmp, 'wx');
      try { fs.writeFileSync(fd, JSON.stringify({ version: 1, serverCredential: this.serverCredential, users, profiles: [...profiles.values()], instances: [...instances.values()] }, null, 2) + '\n'); fs.fsyncSync(fd); }
      finally { fs.closeSync(fd); }
      fs.renameSync(tmp, this.file);
    } finally { fs.rmSync(tmp, { force: true }); }
  }
  mutate(fn) {
    const work = this.queue.then(() => { if (this.closing) throw new ApiError(503, 'control-plane is shutting down'); return fn(); });
    this.queue = work.catch(() => {}); return work;
  }
  require(id) { const c = this.instances.get(id); if (!c) throw new ApiError(404, 'instance not found'); return c; }
  live(id) {
    if (!this.runtime.has(id)) this.runtime.set(id, { state: 'stopped', conn: null, desired: false, failures: 0, restarts: 0, lastExit: null, timer: null, startup: null, nextRestartAt: null });
    return this.runtime.get(id);
  }
  get(id) {
    const c = this.require(id), r = this.live(id);
    return structuredClone({ ...c, userDataDir: this.profilePath(c.profile), state: r.state, pid: r.conn?.pid ?? null,
      restarts: r.restarts, lastExit: r.lastExit, nextRestartAt: r.nextRestartAt, viewerConnected: !!this.viewerConnected(id) });
  }
  list() { return [...this.instances.keys()].map(id => this.get(id)); }
  profilePath(id) {
    const p = this.profiles.get(id); if (!p) throw new ApiError(404, 'profile not found');
    return p.externalPath || path.join(this.profileRoot, p.id);
  }
  prepareProfile(id) {
    const p = this.profiles.get(id), dir = this.profilePath(id);
    fs.mkdirSync(dir, { recursive: true });
    if (!p.externalPath && (fs.lstatSync(dir).isSymbolicLink() || path.dirname(fs.realpathSync(dir)).toLowerCase() !== fs.realpathSync(this.profileRoot).toLowerCase())) throw new ApiError(409, 'profile path is redirected outside its data root');
    return dir;
  }
  listProfiles() { return [...this.profiles.values()].map(p => ({ id: p.id, name: p.name, owner: p.owner, userDataDir: this.profilePath(p.id), managed: !p.externalPath,
    instances: [...this.instances.values()].filter(c => c.profile === p.id).map(c => c.id) })); }
  createProfile(input) { return this.mutate(() => {
    keys(input, ['id', 'name', 'owner']); const p = { id: id(input.id ?? crypto.randomUUID()), name: name(input.name ?? input.id ?? 'Profile'), owner: input.owner ?? null };
    if (p.owner !== null && !this.users.some(u => u.id === p.owner)) bad('unknown owner');
    if (this.profiles.has(p.id)) throw new ApiError(409, 'profile already exists');
    const profiles = new Map(this.profiles); profiles.set(p.id, p);
    fs.mkdirSync(path.join(this.profileRoot, p.id), { recursive: true });
    this.save(this.instances, profiles); this.profiles = profiles;
    return this.listProfiles().find(v => v.id === p.id);
  }); }
  deleteProfile(id) { return this.mutate(() => {
    const p = this.profiles.get(id); if (!p) throw new ApiError(404, 'profile not found');
    if ([...this.instances.values()].some(c => c.profile === id)) throw new ApiError(409, 'profile is referenced by an instance');
    if (p.externalPath) throw new ApiError(409, 'legacy external profiles must be removed manually');
    const dir = this.profilePath(id);
    if (fs.existsSync(dir)) { this.prepareProfile(id); fs.rmSync(dir, { recursive: true }); }
    const profiles = new Map(this.profiles); profiles.delete(id); this.save(this.instances, profiles); this.profiles = profiles;
  }); }
  create(input) { return this.mutate(() => {
    object(input);
    const c = config({ ...input, id: input.id ?? crypto.randomUUID() });
    if (this.instances.has(c.id)) throw new ApiError(409, 'instance already exists');
    this.profilePath(c.profile);
    if (c.owner !== this.profiles.get(c.profile).owner) bad('instance and profile must have the same owner');
    const instances = new Map(this.instances); instances.set(c.id, c); this.save(instances); this.instances = instances;
    return this.get(c.id);
  }); }
  patch(id, input) { return this.mutate(() => {
    keys(input, instanceFields.filter(k => k !== 'id')); const old = this.require(id), r = this.live(id);
    if ((r.conn || r.desired) && Object.keys(input).some(k => !['name', 'autostart'].includes(k))) throw new ApiError(409, 'stop instance before changing launch configuration');
    const c = config({ ...old, ...input }); this.profilePath(c.profile);
    if (c.owner !== old.owner || c.owner !== this.profiles.get(c.profile).owner) bad('instance and profile must retain the same owner');
    const instances = new Map(this.instances); instances.set(id, c); this.save(instances); this.instances = instances; return this.get(id);
  }); }
  delete(id) { return this.mutate(async () => {
    this.require(id); await this.stopNow(id);
    const instances = new Map(this.instances); instances.delete(id); this.save(instances); this.instances = instances; this.runtime.delete(id);
  }); }
  start(id) { return this.mutate(() => this.startNow(id)); }
  startNow(id) {
    const c = this.require(id), r = this.live(id);
    if (r.desired) return this.get(id);
    if (!this.endpointPath) throw new ApiError(409, 'set ENDPOINT_PATH to start managed instances');
    const profileKey = fs.realpathSync(this.prepareProfile(c.profile)).toLowerCase();
    for (const other of this.instances.values()) if (other.id !== id && (this.live(other.id).desired || this.live(other.id).conn) && fs.realpathSync(this.profilePath(other.profile)).toLowerCase() === profileKey) throw new ApiError(409, 'profile is already running or reserved for restart');
    r.desired = true; r.failures = 0; this.launch(id); return this.get(id);
  }
  launch(id) {
    const c = this.require(id), r = this.live(id);
    if (!r.desired || this.closing) return;
    r.timer = null; r.nextRestartAt = null; r.state = 'starting'; const started = Date.now();
    try {
      const profile = this.prepareProfile(c.profile);
      checkProfileAvailable(profile);
      const extra = [...c.reArgs];
      if (!extra.some(a => a.startsWith('--chrome='))) extra.push(`--chrome=${this.chromePath}`);
      extra.push(`--user-data-dir=${profile}`, `--isolation=${c.isolation}`);
      if (c.isolation !== 'off') extra.push(`--job-ui-limits=${c.jobUiLimits}`);
      for (const [key, flag] of Object.entries({ memoryMB: 'memory-mb', cpuRate: 'cpu-rate', processCount: 'process-count' })) if (c.jobLimits[key]) extra.push(`--job-${flag}=${c.jobLimits[key]}`);
      extra.push(...c.chromeArgs.map(a => '--cr-' + a.slice(2)));
      const conn = this.spawn(this.endpointPath, id, (conn, m) => {
        if (r.conn !== conn) return;
        if (m.t === 'hello' && m.v === 1) { r.state = 'running'; clearTimeout(r.startup); r.startup = null; }
        this.onMessage(conn, m);
      }, (conn, exit) => {
        if (r.conn !== conn) return;
        clearTimeout(r.startup); r.startup = null; r.conn = null; this.onClose(conn);
        this.exited(id, exit, started);
      }, this.log, extra);
      r.conn = conn;
      r.startup = setTimeout(() => { this.log(`instance ${id} startup timed out`); conn.close(); }, this.startupMs);
    } catch (e) {
      // A profile/configuration failure needs operator action, not a restart loop.
      r.desired = false; r.state = 'crashed';
      r.lastExit = { code: null, signal: null, error: e.message, at: new Date().toISOString() };
      this.log(`instance ${id} cannot start: ${e.message}`);
    }
  }
  exited(id, exit, started) {
    const r = this.live(id); r.lastExit = exit;
    if (!r.desired || this.closing) { r.state = 'stopped'; return; }
    r.state = 'crashed';
    if (Date.now() - started >= this.stableMs) r.failures = 0;
    const delay = restartDelay(++r.failures, this.baseDelay, this.maxDelay);
    r.nextRestartAt = new Date(Date.now() + delay).toISOString(); r.state = 'backoff';
    this.log(`instance ${id} exited; restart in ${delay} ms`);
    r.timer = setTimeout(() => { if (r.desired && !this.closing) { r.restarts++; this.launch(id); } }, delay);
  }
  stop(id) { return this.mutate(async () => { await this.stopNow(id); return this.get(id); }); }
  async stopNow(id) {
    this.require(id); const r = this.live(id); r.desired = false;
    clearTimeout(r.timer); clearTimeout(r.startup); r.timer = r.startup = r.nextRestartAt = null;
    if (r.conn) await r.conn.stop(this.graceMs);
    r.state = 'stopped';
  }
  restart(id) { return this.mutate(async () => { await this.stopNow(id); return this.startNow(id); }); }
  async boot() {
    for (const c of this.instances.values()) if (c.autostart) {
      try { await this.start(c.id); } catch (e) { this.live(c.id).state = 'crashed'; this.live(c.id).lastExit = { error: e.message, at: new Date().toISOString() }; this.log(`autostart ${c.id}: ${e.message}`); }
    }
  }
  async shutdown() {
    this.closing = true; await this.queue;
    await Promise.all([...this.instances.keys()].map(id => this.stopNow(id)));
  }
}
