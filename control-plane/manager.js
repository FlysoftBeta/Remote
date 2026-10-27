// Durable configuration; live processes and restart timers are deliberately not persisted.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnEndpoint } from './endpoint.js';

export class ApiError extends Error {
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
const instanceFields = ['id', 'name', 'profile', 'isolation', 'jobLimits', 'jobUiLimits', 'reArgs', 'chromeArgs', 'autostart'];
function config(input) {
  keys(input, instanceFields);
  const result = { id: id(input.id), name: name(input.name ?? input.id), profile: id(input.profile),
    isolation: input.isolation ?? 'job', jobLimits: input.jobLimits ?? {}, jobUiLimits: input.jobUiLimits ?? 'none',
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
  return result;
}

export function restartDelay(failures, base = 1000, max = 30000) {
  return Math.min(max, base * 2 ** Math.min(30, Math.max(0, failures - 1)));
}

export class InstanceManager {
  constructor({ dataDir, endpointPath, legacyArgs = [], onMessage = () => {}, onClose = () => {},
    viewerConnected = () => false, log = console.log, spawn = spawnEndpoint,
    baseDelay = 1000, maxDelay = 30000, stableMs = 60000, startupMs = 90000, graceMs = 3000 }) {
    this.root = path.resolve(dataDir); this.file = path.join(this.root, 'instances.json');
    this.profileRoot = path.join(this.root, 'profiles');
    fs.mkdirSync(this.profileRoot, { recursive: true });
    this.endpointPath = endpointPath; this.onMessage = onMessage; this.onClose = onClose;
    this.viewerConnected = viewerConnected; this.log = log; this.spawn = spawn;
    Object.assign(this, { baseDelay, maxDelay, stableMs, startupMs, graceMs });
    this.instances = new Map(); this.profiles = new Map(); this.runtime = new Map();
    this.queue = Promise.resolve(); this.closing = false;
    if (fs.existsSync(this.file)) {
      const saved = JSON.parse(fs.readFileSync(this.file, 'utf8'));
      if (saved.version !== 1 || !Array.isArray(saved.instances) || !Array.isArray(saved.profiles)) throw new Error('invalid instances.json schema');
      for (const p of saved.profiles) {
        keys(p, ['id', 'name', 'externalPath']); id(p.id); name(p.name);
        if (this.profiles.has(p.id)) throw new Error('duplicate persisted profile');
        if (p.externalPath && (p.id !== 'default' || typeof p.externalPath !== 'string' || !path.isAbsolute(p.externalPath))) throw new Error('invalid legacy profile');
        this.profiles.set(p.id, p);
      }
      for (const input of saved.instances) {
        const c = config(input);
        if (this.instances.has(c.id) || !this.profiles.has(c.profile)) throw new Error('invalid persisted instance/profile reference');
        this.instances.set(c.id, c);
      }
    } else {
      // Import the old default command once, including its explicitly selected profile.
      let profile, isolation = 'off', jobUiLimits = 'none'; const reArgs = [], jobLimits = {};
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
      this.profiles.set('default', { id: 'default', name: 'Default', ...(profile ? { externalPath: profile } : {}) });
      this.instances.set('default', config({ id: 'default', name: 'Default', profile: 'default', isolation, jobUiLimits, jobLimits, reArgs, autostart: !!endpointPath }));
      this.save();
    }
  }
  save(instances = this.instances, profiles = this.profiles) {
    const tmp = this.file + '.' + crypto.randomUUID() + '.tmp';
    try {
      const fd = fs.openSync(tmp, 'wx');
      try { fs.writeFileSync(fd, JSON.stringify({ version: 1, profiles: [...profiles.values()], instances: [...instances.values()] }, null, 2) + '\n'); fs.fsyncSync(fd); }
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
  listProfiles() { return [...this.profiles.values()].map(p => ({ id: p.id, name: p.name, userDataDir: this.profilePath(p.id), managed: !p.externalPath,
    instances: [...this.instances.values()].filter(c => c.profile === p.id).map(c => c.id) })); }
  createProfile(input) { return this.mutate(() => {
    keys(input, ['id', 'name']); const p = { id: id(input.id ?? crypto.randomUUID()), name: name(input.name ?? input.id ?? 'Profile') };
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
    const instances = new Map(this.instances); instances.set(c.id, c); this.save(instances); this.instances = instances;
    return this.get(c.id);
  }); }
  patch(id, input) { return this.mutate(() => {
    keys(input, instanceFields.filter(k => k !== 'id')); const old = this.require(id), r = this.live(id);
    if ((r.conn || r.desired) && Object.keys(input).some(k => !['name', 'autostart'].includes(k))) throw new ApiError(409, 'stop instance before changing launch configuration');
    const c = config({ ...old, ...input }); this.profilePath(c.profile);
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
    const profileKey = this.profilePath(c.profile).toLowerCase();
    for (const other of this.instances.values()) if (other.id !== id && this.profilePath(other.profile).toLowerCase() === profileKey && (this.live(other.id).desired || this.live(other.id).conn)) throw new ApiError(409, 'profile is already running or reserved for restart');
    this.prepareProfile(c.profile);
    r.desired = true; r.failures = 0; this.launch(id); return this.get(id);
  }
  launch(id) {
    const c = this.require(id), r = this.live(id);
    if (!r.desired || this.closing) return;
    r.timer = null; r.nextRestartAt = null; r.state = 'starting'; const started = Date.now();
    try {
      const extra = [...c.reArgs];
      if (!extra.some(a => a.startsWith('--chrome='))) extra.push(`--chrome=${path.join(path.dirname(this.endpointPath), 'chrome.exe')}`);
      extra.push(`--user-data-dir=${this.prepareProfile(c.profile)}`, `--isolation=${c.isolation}`);
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
    } catch (e) { this.exited(id, { code: null, signal: null, error: e.message, at: new Date().toISOString() }, started); }
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
