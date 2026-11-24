import { createHash } from 'node:crypto'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'
import type { Plugin } from 'vite'

// Classic scripts also work beside an HTML opened with file://. Fetching a
// sibling WOFF2 from that origin is blocked by some browsers' font CORS rules.
export function optionalAssetsPlugin(root: string): Plugin {
  const defaultFontModule = 'virtual:inktag-default-creation-fonts'
  const defaultFonts = Object.fromEntries(Object.entries({ pixelBody: 'body', pixelFusion: 'fusion' }).map(([key, name]) => [
    key, `data:application/gzip;base64,${readFileSync(resolve(root, `src/assets/fonts/creation-${name}.bin.gz`)).toString('base64')}`,
  ]))
  const inputs = {
    calligraphy: ['src/assets/fonts/MaShanZheng-Regular.woff2', 'font/woff2'],
    pixelCompact: ['src/assets/fonts/creation-compact.bin.gz', 'application/gzip'],
    pixelMini: ['src/assets/fonts/creation-mini.bin.gz', 'application/gzip'],
    pixelSong: ['src/assets/fonts/creation-song.bin.gz', 'application/gzip'],
    colorPhoto: ['src/assets/onboarding/red-tulips.png', 'image/png'],
  }
  const resources = Object.entries(inputs).map(([key, [path, mime]]) => {
    const bytes = readFileSync(resolve(root, path))
    const hash = createHash('sha256').update(bytes).digest('hex')
    const id = `${key}-${hash.slice(0, 16)}`
    const fileName = `assets/optional-${id}.js`
    const source = `globalThis.__inktagOptionalAssets??={};globalThis.__inktagOptionalAssets[${JSON.stringify(id)}]=${JSON.stringify(`data:${mime};base64,${bytes.toString('base64')}`)};`
    return { key, id, fileName, source }
  })
  const fontLicenses = ['FusionPixel-NOTICES.txt', 'OFL-MuzaiPixel.txt', 'OFL-ZLabsPixel.txt', 'OFL-ChillBitmap.txt', 'ChillBitmap7x-LICENSE.txt']
    .map(name => `${name}\n\n${readFileSync(resolve(root, 'src/assets/fonts', name), 'utf8')}`).join('\n\n')
  return {
    name: 'inktag-optional-assets',
    resolveId(id) { if (id === 'virtual:inktag-optional-assets' || id === defaultFontModule) return '\0' + id },
    load(id) {
      if (id === '\0' + defaultFontModule) return `export default ${JSON.stringify(defaultFonts)};`
      if (id === '\0virtual:inktag-optional-assets') return `export default ${JSON.stringify(Object.fromEntries(resources.map(({ key, id, fileName }) => [key, { id, path: fileName }])))};`
    },
    generateBundle() {
      for (const { fileName, source } of resources) this.emitFile({ type: 'asset', fileName, source })
      this.emitFile({ type: 'asset', fileName: 'assets/creation-font-licenses.txt', source: fontLicenses })
      this.emitFile({ type: 'asset', fileName: 'assets/generate-creation-fonts.py', source: readFileSync(resolve(root, 'tools/generate-creation-fonts.py')) })
    },
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        const resource = resources.find(r => '/' + r.fileName === req.url?.split('?')[0])
        if (!resource) return next()
        res.setHeader('Content-Type', 'application/javascript; charset=utf-8')
        res.end(resource.source)
      })
    },
  }
}
