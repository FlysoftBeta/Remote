import { useEffect, useState, type FormEvent } from 'react';
import { createRoot } from 'react-dom/client';
import { createPortal } from 'react-dom';
import { api, remembered, type Login, type User, type Instance, type Profile } from './auth';
import shell from './session-shell.html?raw';
import './ui.css';

declare global { interface Window { rkToken?: string; viewerFinish?: (message?: string) => void } }
const params = new URLSearchParams(location.search);
const server = params.get('cp') ? location.protocol + '//' + params.get('cp') : '';
const serverKey = server || location.origin;
const message = (e: unknown) => e instanceof Error ? e.message : String(e);
function loadScript(src: string) {
  return new Promise<void>((resolve, reject) => {
    const script = document.createElement('script'); script.src = src;
    script.onload = () => resolve(); script.onerror = () => reject(new Error('Unable to load ' + src));
    document.body.appendChild(script);
  });
}

function App() {
  const [login, setLogin] = useState<Login | null>(null), [ready, setReady] = useState(false);
  const [config, setConfig] = useState({ registrationCredentialRequired: false, firstUser: false });
  const [register, setRegister] = useState(false), [error, setError] = useState(''), [busy, setBusy] = useState(false);
  useEffect(() => { void (async () => {
    try {
      const info = await api(server)<typeof config>('auth/config'); setConfig(info); setRegister(info.firstUser);
      let saved: Login | null = null;
      try { saved = await remembered(serverKey); } catch { /* Login remains usable when browser storage is disabled. */ }
      const token = params.get('token') || saved?.token;
      if (token) {
        try {
          const user = await api(server, token)<User>('auth/me');
          const session = { token, user, expiresAt: saved?.expiresAt || Date.now() + 30 * 86400000 };
          setLogin(session); try { await remembered(serverKey, session); } catch {}
        } catch { try { await remembered(serverKey, null); } catch {} }
      }
      // URL tokens remain accepted for tools, then leave the address bar and browser history.
      if (params.has('token')) { params.delete('token'); history.replaceState(null, '', location.pathname + '?' + params + location.hash); }
      try {
        const settings = JSON.parse(localStorage.getItem('remotekit.viewer.settings') || '{}');
        delete settings.token; localStorage.setItem('remotekit.viewer.settings', JSON.stringify(settings));
      } catch {}
    } catch (e) { setError(message(e)); }
    setReady(true);
  })(); }, []);
  async function submit(e: FormEvent<HTMLFormElement>) {
    e.preventDefault(); const input = Object.fromEntries(new FormData(e.currentTarget)); setBusy(true); setError('');
    try {
      const result = await api(server)<Login>('auth/' + (register ? 'register' : 'login'), 'POST', input);
      try { await remembered(serverKey, result); } catch { /* Retain for this tab when IDB is unavailable. */ }
      setLogin(result);
    } catch (e) { setError(message(e)); } finally { setBusy(false); }
  }
  async function logout() {
    try { await api(server, login?.token)('auth/logout', 'POST'); } catch {}
    try { await remembered(serverKey, null); } catch {}
    const url = new URL(location.href); url.searchParams.delete('connect'); url.searchParams.delete('token'); location.replace(url);
  }
  if (login) { window.rkToken = login.token; return <Session login={login} logout={logout} />; }
  return <section id="settings"><div className="shell"><header className="brand"><div><h1>Remote viewer</h1><p className="lede">{register ? 'Create your account' : 'Sign in to your browsers'}</p></div></header>
    {!ready ? <p>Loading…</p> : <form className="panel" onSubmit={submit}>
      {register && config.firstUser && <p>The first account is the administrator.</p>}
      <div className="field"><label htmlFor="username">Username</label><input id="username" name="username" required pattern="[a-z0-9]([a-z0-9_]|-){0,63}" autoComplete="username" autoCapitalize="none" /></div>
      <div className="field"><label htmlFor="password">Password</label><input id="password" name="password" type="password" required minLength={register ? 8 : 1} maxLength={1024} autoComplete={register ? 'new-password' : 'current-password'} /></div>
      {register && config.registrationCredentialRequired && <div className="field"><label htmlFor="server-credential">Server credential</label><input id="server-credential" name="serverCredential" type="password" required autoComplete="off" /><p className="help">Required only when creating an account.</p></div>}
      {error && <p role="alert" className="manage-msg-error">{error}</p>}
      <div className="account-actions"><button className="btn btn-primary" disabled={busy}>{register ? 'Create account' : 'Sign in'}</button><button type="button" className="btn btn-ghost" onClick={() => { setRegister(!register); setError(''); }}>{register ? 'Sign in instead' : 'Create account'}</button></div>
      <p className="help">This browser remembers your session.</p>
    </form>}
    {!ready && error && <p role="alert">{error}</p>}
  </div></section>;
}

function Session({ login, logout }: { login: Login; logout: () => void }) {
  const [slot, setSlot] = useState<HTMLElement | null>(null), [admin, setAdmin] = useState(false), [profiles, setProfiles] = useState(false);
  useEffect(() => {
    setSlot(document.getElementById('account-slot'));
    void loadScript('/session-ui.js').then(() => loadScript('/settings.js')).catch(e => alert(message(e)));
  }, []);
  // React owns the shell boundary; the original runtime owns the media/input DOM inside it.
  return <><div dangerouslySetInnerHTML={{ __html: shell }} />{slot && createPortal(<>
    <nav className="account-actions"><span>{login.user.username} {login.user.role === 'admin' && <span className="tag">Admin</span>}</span><span className="account-spacer" /><button type="button" className="btn btn-small" onClick={() => setProfiles(true)}>Profiles</button>{login.user.role === 'admin' && <button type="button" className="btn btn-small" onClick={() => setAdmin(true)}>Administration</button>}<button type="button" className="btn btn-small btn-ghost" onClick={logout}>Sign out</button></nav>
    {admin && <Admin login={login} close={() => setAdmin(false)} />}
    {profiles && <Profiles login={login} close={() => setProfiles(false)} />}
  </>, slot)}</>;
}

function Profiles({ login, close }: { login: Login; close: () => void }) {
  const call = api(server, login.token), [rows, setRows] = useState<Profile[]>([]), [error, setError] = useState(''), [busy, setBusy] = useState(false);
  async function refresh() { setRows(await call<Profile[]>('profiles')); }
  useEffect(() => { void refresh().catch(e => setError(message(e))); }, []);
  async function act(work: () => Promise<unknown>) {
    setBusy(true); setError(''); try { await work(); await refresh(); } catch (e) { setError(message(e)); } finally { setBusy(false); }
  }
  return <div id="admin-page"><div className="shell"><header className="account-actions"><h1>Profiles</h1><span className="account-spacer" /><button className="btn" onClick={close}>Back</button></header>
    {error && <p role="alert" className="manage-msg-error">{error}</p>}
    <fieldset className="admin-controls" disabled={busy}><section className="panel">{rows.map(p => <div className="admin-row" key={p.id}><span>{p.name}<small className="sub">{p.id} · {p.instances.join(', ') || 'Unused'}</small></span><span className="account-spacer" /><button className="btn btn-small btn-danger" disabled={!!p.instances.length || !p.managed} onClick={() => { if (confirm('Delete ' + p.id + ' and its browser data?')) void act(() => call('profiles/' + p.id, 'DELETE')); }}>Delete</button></div>)}</section>
    <form className="panel" onSubmit={e => { e.preventDefault(); const form = e.currentTarget, body = Object.fromEntries(new FormData(form)); void act(async () => { await call('profiles', 'POST', body); form.reset(); }); }}><div className="field"><label>New profile ID<input name="id" required pattern="[a-z0-9]([a-z0-9_]|-){0,63}" /></label></div><button className="btn">Create profile</button></form></fieldset>
  </div></div>;
}

function Admin({ login, close }: { login: Login; close: () => void }) {
  const call = api(server, login.token);
  const [users, setUsers] = useState<User[]>([]), [instances, setInstances] = useState<Instance[]>([]), [profiles, setProfiles] = useState<Profile[]>([]);
  const [error, setError] = useState(''), [busy, setBusy] = useState(false);
  async function refresh() {
    const [u, i, p] = await Promise.all([call<User[]>('users'), call<Instance[]>('instances'), call<Profile[]>('profiles')]);
    setUsers(u); setInstances(i); setProfiles(p);
  }
  useEffect(() => { void refresh().catch(e => setError(message(e))); }, []);
  async function act(work: () => Promise<unknown>) {
    setBusy(true); setError(''); try { await work(); await refresh(); } catch (e) { setError(message(e)); } finally { setBusy(false); }
  }
  function createUser(e: FormEvent<HTMLFormElement>) {
    e.preventDefault(); const form = e.currentTarget, input = Object.fromEntries(new FormData(form));
    void act(async () => { await call('users', 'POST', input); form.reset(); });
  }
  function createResource(e: FormEvent<HTMLFormElement>) {
    e.preventDefault(); const form = e.currentTarget, input = Object.fromEntries(new FormData(form));
    void act(async () => {
      if (input.kind === 'profile') await call('profiles', 'POST', { id: input.id, name: input.id, owner: input.owner });
      else await call('instances', 'POST', { id: input.id, profile: input.profile, owner: input.owner, isolation: 'job' });
      form.reset();
    });
  }
  const ownerName = (id: string) => users.find(u => u.id === id)?.username || id;
  return <div id="admin-page"><div className="shell"><header className="account-actions"><h1>Administration</h1><span className="account-spacer" /><button className="btn" onClick={close}>Back</button></header>
    {error && <p role="alert" className="manage-msg-error">{error}</p>}
    <fieldset disabled={busy} className="admin-controls"><section className="panel"><h2>Users</h2>
      {users.map(u => <div className="admin-row" key={u.id}><span>{u.username} <small className="muted">{u.role}</small></span><span className="account-spacer" />
        <button className="btn btn-small" onClick={() => { const password = prompt('New password for ' + u.username + ' (at least 8 characters)'); if (password) void act(() => call('users/' + u.id + '/password', 'POST', { password })); }}>Reset password</button>
        <button className="btn btn-small btn-danger" onClick={() => { if (confirm('Delete user ' + u.username + '? Remove their instances and profiles first.')) void act(() => call('users/' + u.id, 'DELETE')); }}>Delete</button></div>)}
      <form onSubmit={createUser}><h3>Create user</h3><div className="grid"><div className="field"><label>Username<input name="username" required autoComplete="off" /></label></div><div className="field"><label>Password<input name="password" type="password" required minLength={8} autoComplete="new-password" /></label></div></div><button className="btn">Create user</button></form>
    </section>
    <section className="panel"><h2>All instances</h2>{instances.map(i => <div className="admin-row" key={i.id}><span>{i.name}<small className="sub">{ownerName(i.owner)} · {i.profile}</small></span><span className={'badge ' + i.state}>{i.state}</span><span className="account-spacer" />{['start', 'stop', 'restart', 'delete'].map(action => <button key={action} className={'btn btn-small' + (action === 'delete' ? ' btn-danger' : '')} onClick={() => { if (action !== 'delete' || confirm('Delete instance ' + i.id + '?')) void act(() => call('instances/' + i.id + (action === 'delete' ? '' : '/' + action), action === 'delete' ? 'DELETE' : 'POST')); }}>{action}</button>)}<button className="btn btn-small" onClick={() => { const url = new URL(location.href); url.searchParams.set('instance', i.id); url.searchParams.set('connect', '1'); location.assign(url); }}>Connect</button></div>)}</section>
    <section className="panel"><h2>All profiles</h2>{profiles.map(p => <div className="admin-row" key={p.id}><span>{p.name}<small className="sub">{ownerName(p.owner)} · {p.instances.join(', ') || 'Unused'}</small></span><span className="account-spacer" /><button className="btn btn-small btn-danger" disabled={!!p.instances.length || !p.managed} onClick={() => { if (confirm('Delete profile ' + p.id + ' and its browser data?')) void act(() => call('profiles/' + p.id, 'DELETE')); }}>Delete profile</button></div>)}</section>
    <form className="panel" onSubmit={createResource}><h2>Create instance or profile</h2><div className="grid">
      <div className="field"><label>Kind<select name="kind"><option value="profile">Profile</option><option value="instance">Instance</option></select></label></div>
      <div className="field"><label>ID<input name="id" required pattern="[a-z0-9]([a-z0-9_]|-){0,63}" /></label></div>
      <div className="field"><label>Owner<select name="owner" defaultValue={login.user.id}>{users.map(u => <option key={u.id} value={u.id}>{u.username}</option>)}</select></label></div>
      <div className="field"><label>Profile (for an instance)<select name="profile">{profiles.map(p => <option key={p.id} value={p.id}>{p.id} · {ownerName(p.owner)}</option>)}</select></label></div>
    </div><button className="btn">Create</button></form>
    </fieldset>
  </div></div>;
}

createRoot(document.getElementById('root')!).render(<App />);
