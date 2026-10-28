import { build } from 'esbuild';
await build({
  entryPoints: ['src/index.ts'], outfile: 'dist/server.mjs', bundle: true,
  platform: 'node', format: 'esm', target: 'node22',
  external: ['bufferutil', 'utf-8-validate'],
  banner: { js: "import { createRequire } from 'node:module'; const require = createRequire(import.meta.url);" }
});
