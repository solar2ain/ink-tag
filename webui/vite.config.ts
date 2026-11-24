import { defineConfig } from 'vite'
import { fileURLToPath } from 'node:url'
import { readFileSync } from 'node:fs'
import react from '@vitejs/plugin-react'
import { viteSingleFile } from 'vite-plugin-singlefile'
import { optionalAssetsPlugin } from './tools/optional-assets-plugin.ts'
import { otaAdaptersPlugin } from './tools/ota-adapters-plugin.ts'
import { clientBuildId } from './tools/client-build.mjs'
const protocolVersion = JSON.parse(readFileSync(new URL('../protocol/release-baseline-v1.json', import.meta.url), 'utf8')).framing.protocolByte as number

const clientBuild = { buildId: clientBuildId(import.meta.dirname), buildNumber: Number(process.env.INKTAG_BUILD_NUMBER || 0) }
if (!Number.isSafeInteger(clientBuild.buildNumber) || clientBuild.buildNumber < 0) throw new Error('Invalid INKTAG_BUILD_NUMBER')

export default defineConfig(({ mode }) => {
  const standalone = mode === 'standalone'

  return {
    base: './',
    define: { __INKTAG_CLIENT_BUILD__: JSON.stringify(clientBuild) },
    plugins: [react(), optionalAssetsPlugin(import.meta.dirname), otaAdaptersPlugin(import.meta.dirname), {
      name: 'development-refresh-policy',
      transformIndexHtml(html, context) {
        // Vite injects its own inline React/HMR preamble in development only.
        // Production admission requires the exact CSP and rejects dev output.
        if (context.server) return html.replace(/<meta http-equiv="Content-Security-Policy"[^>]*>/, '')
        return { html, tags: [
          { tag: 'meta', attrs: { name: 'inktag-protocol-version', content: String(protocolVersion) }, injectTo: 'head' },
          { tag: 'meta', attrs: { name: 'inktag-client-build', content: clientBuild.buildId }, injectTo: 'head' },
          { tag: 'meta', attrs: { name: 'inktag-build-number', content: String(clientBuild.buildNumber) }, injectTo: 'head' },
        ] }
      },
    }, {
      name: 'watch-shared-client-core',
      configureServer(server) {
        // These shared sources live outside Vite's root; syncs must invalidate
        // their module cache as well as the WebUI re-export wrappers.
        server.watcher.add(fileURLToPath(new URL('../packages/inktag-client-core/src', import.meta.url)))
      },
    }, ...(standalone ? [viteSingleFile()] : [])],
    test: {
      exclude: ['**/e2e/**', '**/node_modules/**', '**/dist/**'],
    },
    build: {
      outDir: standalone ? 'dist/standalone' : 'dist/static',
      sourcemap: false,
    },
  }
})
