import { createHash } from 'node:crypto'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'
const root = resolve(import.meta.dirname, '../..')
const assets = resolve(root, 'cli/src/inktag_cli/_assets')
const manifest = JSON.parse(readFileSync(resolve(assets, 'nativePreview.inputs.json'), 'utf8'))
const sha = (bytes) => createHash('sha256').update(bytes).digest('hex')
for (const [name, expected] of Object.entries(manifest.inputs)) {
  const actual = readFileSync(resolve(root, name), 'utf8').replace(/\r\n/g, '\n')
  if (sha(actual) !== expected) throw Error(`Native font preview is stale: ${name}. Rebuild tools/emulator/build-native-font-preview.py with the pinned Emscripten toolchain.`)
}
if (sha(readFileSync(resolve(assets, 'nativePreviewWasm.js'))) !== manifest.assetSha256)
  throw Error('Native font preview WebAssembly asset hash mismatch')
console.log('Verified native preview common inputs and WebAssembly asset')
