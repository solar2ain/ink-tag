import { createHash } from 'node:crypto'
import { readFileSync, readdirSync, statSync } from 'node:fs'
import { extname, relative, resolve } from 'node:path'

export function clientBuildId(root) {
  const source = resolve(root, 'src'), hash = createHash('sha256')
  const inputs = JSON.parse(readFileSync(resolve(root, 'tools/client-build-inputs.json'), 'utf8'))
  // Match finalize-standalone: these C-oracle outputs are development evidence.
  const excluded = new Set((inputs.excludedDirectories || []).map(path => resolve(root, path)))
  const textExtensions = new Set(['.ts', '.tsx', '.js', '.mjs', '.mts', '.json', '.html', '.css', '.txt'])
  function inputBytes(path) {
    const raw = readFileSync(path)
    return inputs.normalizeTextLineEndings && textExtensions.has(extname(path).toLowerCase())
      ? Buffer.from(raw.toString('latin1').replaceAll('\r\n', '\n'), 'latin1') : raw
  }
  function tree(directory) {
    for (const name of readdirSync(directory).sort()) {
      const path = resolve(directory, name)
      if (excluded.has(path)) continue
      if (statSync(path).isDirectory()) tree(path)
      else { hash.update(relative(source, path).replaceAll('\\', '/')); hash.update(inputBytes(path)) }
    }
  }
  for (const directory of inputs.directories) tree(resolve(root, directory))
  for (const item of inputs.files) { hash.update(item.name); hash.update(inputBytes(resolve(root, item.path))) }
  return `web-${hash.digest('hex').slice(0, 20)}`
}
