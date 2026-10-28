import crypto from 'node:crypto';
import { promisify } from 'node:util';
import { ApiError, type InstanceManager } from './manager.js';

const scrypt = promisify(crypto.scrypt);
const hashToken = (token: string) => crypto.createHash('sha256').update(token).digest('hex');
const publicUser = (u: StoredUser) => ({ id: u.id, username: u.username, role: u.role });
export interface StoredUser {
  id: string; username: string; role: 'admin' | 'user'; salt: string; passwordHash: string;
  sessions: { hash: string; expiresAt: number }[];
}
export type User = ReturnType<typeof publicUser>;

export class Auth {
  constructor(private manager: InstanceManager, private revoked: (id: string) => void = () => {}) {}
  get credential() { return process.env.CP_TOKEN ?? this.manager.serverCredential; }
  info() { return { registrationCredentialRequired: !!this.credential, firstUser: this.manager.users.length === 0 }; }
  list() { return this.manager.users.map(publicUser); }
  authenticate(token: string): User | null {
    if (!token || token.length > 256) return null;
    const hash = hashToken(token), now = Date.now();
    const user = this.manager.users.find(u => u.sessions.some(s => s.hash === hash && s.expiresAt > now));
    return user ? publicUser(user) : null;
  }
  async password(password: unknown) {
    if (typeof password !== 'string' || password.length < 8 || password.length > 1024) throw new ApiError(400, 'password must be 8..1024 characters');
    const salt = crypto.randomBytes(16).toString('hex');
    return { salt, passwordHash: ((await scrypt(password, salt, 64)) as Buffer).toString('hex') };
  }
  async create(input: any, admin?: User) {
    if (!input || typeof input !== 'object' || Array.isArray(input)) throw new ApiError(400, 'JSON object required');
    if (!admin && this.credential && input.serverCredential !== this.credential) throw new ApiError(401, 'server credential required to register');
    if (typeof input.username !== 'string' || !/^[a-z0-9][a-z0-9_-]{0,63}$/.test(input.username)) throw new ApiError(400, 'username must be 1..64 lowercase letters, digits, _ or -');
    if (admin && input.role && !['user', 'admin'].includes(input.role)) throw new ApiError(400, 'invalid role');
    const password = await this.password(input.password);
    return this.manager.mutate(() => {
      if (this.manager.users.some(u => u.username === input.username)) throw new ApiError(409, 'username already exists');
      const first = !this.manager.users.length;
      const user: StoredUser = { id: crypto.randomUUID(), username: input.username, role: first ? 'admin' : admin ? input.role ?? 'user' : 'user', ...password, sessions: [] };
      const users = [...this.manager.users, user];
      const instances = new Map(this.manager.instances), profiles = new Map(this.manager.profiles);
      if (first) {
        for (const [id, c] of instances) if (!c.owner) instances.set(id, { ...c, owner: user.id });
        for (const [id, p] of profiles) if (!p.owner) profiles.set(id, { ...p, owner: user.id });
      }
      this.manager.save(instances, profiles, users);
      Object.assign(this.manager, { users, instances, profiles });
      return publicUser(user);
    });
  }
  async login(input: any) {
    if (!input || typeof input.username !== 'string' || typeof input.password !== 'string' || input.password.length > 1024) throw new ApiError(400, 'username and password required');
    const user = this.manager.users.find(u => u.username === input.username);
    // Also do the KDF for an unknown username.
    const actual = await scrypt(input.password, user?.salt ?? '00000000000000000000000000000000', 64) as Buffer;
    if (!user || !crypto.timingSafeEqual(actual, Buffer.from(user.passwordHash, 'hex'))) throw new ApiError(401, 'invalid username or password');
    return this.manager.mutate(() => {
      const current = this.manager.users.find(u => u.id === user.id);
      if (!current || current.passwordHash !== user.passwordHash) throw new ApiError(401, 'login changed; try again');
      const token = crypto.randomBytes(32).toString('base64url'), expiresAt = Date.now() + 30 * 86400000;
      const sessions = [...current.sessions.filter(s => s.expiresAt > Date.now()).slice(-15), { hash: hashToken(token), expiresAt }];
      const users = this.manager.users.map(u => u.id === current.id ? { ...u, sessions } : u);
      this.manager.save(undefined, undefined, users); this.manager.users = users;
      return { token, user: publicUser(current), expiresAt };
    });
  }
  logout(token: string) { return this.manager.mutate(() => {
    const hash = hashToken(token), user = this.authenticate(token);
    const users = this.manager.users.map(u => ({ ...u, sessions: u.sessions.filter(s => s.hash !== hash) }));
    this.manager.save(undefined, undefined, users); this.manager.users = users;
    if (user) this.revoked(user.id);
  }); }
  async reset(id: string, password: unknown) {
    const replacement = await this.password(password);
    return this.manager.mutate(() => {
      if (!this.manager.users.some(u => u.id === id)) throw new ApiError(404, 'user not found');
      const users = this.manager.users.map(u => u.id === id ? { ...u, ...replacement, sessions: [] } : u);
      this.manager.save(undefined, undefined, users); this.manager.users = users; this.revoked(id);
    });
  }
  delete(id: string) { return this.manager.mutate(() => {
    const user = this.manager.users.find(u => u.id === id);
    if (!user) throw new ApiError(404, 'user not found');
    if (user.role === 'admin' && this.manager.users.filter(u => u.role === 'admin').length === 1) throw new ApiError(409, 'cannot delete the last admin');
    if ([...this.manager.instances.values(), ...this.manager.profiles.values()].some(r => r.owner === id)) throw new ApiError(409, 'delete this user’s instances and profiles first');
    const users = this.manager.users.filter(u => u.id !== id);
    this.manager.save(undefined, undefined, users); this.manager.users = users; this.revoked(id);
  }); }
}
