import { build } from 'esbuild'
import { fileURLToPath } from 'node:url'
const entry = fileURLToPath(new URL('../../packages/inktag-editor-core/src/hostScene.ts', import.meta.url))
const outfile = fileURLToPath(new URL('../../cli/src/inktag_cli/_assets/hostScene.js', import.meta.url))
await build({ entryPoints: [entry], outfile, bundle: true, minify: true, format: 'iife', globalName: 'InkTagHostScene', platform: 'neutral', target: 'es2020', sourcemap: false })
console.log('Built shared host scene validator/render plan')
