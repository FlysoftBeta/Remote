export interface User { id: string; username: string; role: 'admin' | 'user' }
export interface Login { token: string; user: User; expiresAt: number }
export interface Instance { id: string; name: string; owner: string; state: string; profile: string; isolation: string; viewerConnected: boolean }
export interface Profile { id: string; name: string; owner: string; instances: string[]; managed: boolean }

// Only the opaque session is retained, never the user's password or server credential.
function database(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open('remotekit.auth', 1);
    request.onupgradeneeded = () => request.result.createObjectStore('sessions');
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}
export async function remembered(server: string, value?: Login | null): Promise<Login | null> {
  const db = await database();
  return new Promise((resolve, reject) => {
    const tx = db.transaction('sessions', value === undefined ? 'readonly' : 'readwrite');
    const store = tx.objectStore('sessions');
    const request = value === undefined ? store.get(server) : value === null ? store.delete(server) : store.put(value, server);
    tx.oncomplete = () => { db.close(); resolve(value === undefined ? request.result ?? null : value); };
    tx.onerror = tx.onabort = () => { db.close(); reject(tx.error); };
  });
}
export function api(server: string, token = '') {
  return async <T>(route: string, method = 'GET', body?: unknown): Promise<T> => {
    const response = await fetch(server + '/api/' + route, {
      method, headers: { ...(token ? { Authorization: 'Bearer ' + token } : {}), ...(body === undefined ? {} : { 'Content-Type': 'application/json' }) },
      body: body === undefined ? undefined : JSON.stringify(body), cache: 'no-store'
    });
    if (response.status === 204) return null as T;
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || 'HTTP ' + response.status);
    return data as T;
  };
}
