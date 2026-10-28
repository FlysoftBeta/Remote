import { defineConfig, type ProxyOptions } from 'vite';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('.', import.meta.url));
// Keep the latency/input scripts as classic scripts, with their original globals and load order.
function syncRuntime() {
  fs.mkdirSync(path.join(root, 'public'), { recursive: true });
  for (const file of fs.readdirSync(root)) if (/\.(js|css)$/.test(file) || file === 'probe.html')
    fs.copyFileSync(path.join(root, file), path.join(root, 'public', file));
}
syncRuntime();
const httpProxy: ProxyOptions = { target: 'http://127.0.0.1:8080', changeOrigin: true, configure(proxy) {
  // Requests originate at Vite during local development; retain the CP's production Origin check.
  proxy.on('proxyReq', request => request.setHeader('origin', 'http://127.0.0.1:8080'));
} };
export default defineConfig({
  build: { target: 'chrome89' },
  server: { proxy: {
    '/api': httpProxy, '/tools': httpProxy,
    '/field-config': httpProxy, '/field-report': httpProxy,
    '/signal': { target: 'ws://127.0.0.1:8080', ws: true }
  } },
  plugins: [{ name: 'classic-runtime', configureServer(server) {
    server.watcher.add(root);
    server.watcher.on('change', file => { if (path.dirname(file) === root && /\.(js|css)$/.test(file)) syncRuntime(); });
  } }]
});
