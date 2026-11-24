import { readFileSync, renameSync, rmSync, writeFileSync } from 'node:fs'
import { resolve } from 'node:path'
import { sealStandalone } from './html-security.mjs'

const dist = resolve(process.argv[2] || resolve(import.meta.dirname, '..', 'dist'))
const standalone = resolve(dist, 'standalone')
const staticBuild = resolve(dist, 'static')
// C-oracle galleries are development evidence, not runtime dependencies. Do
// not export their internal input manifests through Vite's public-dir copy.
rmSync(resolve(staticBuild, '.local'), { recursive: true, force: true })
rmSync(resolve(staticBuild, 'layouts-generated'), { recursive: true, force: true })
const escapeHtml = (text) => text.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;')
const notices = [
  ['LICENSING.txt', resolve(standalone, 'LICENSING.txt')],
  ['THIRD_PARTY_NOTICES.txt', resolve(standalone, 'THIRD_PARTY_NOTICES.txt')],
  ['Z Labs Pixel CN — SIL Open Font License 1.1', resolve(import.meta.dirname, '../src/assets/fonts/OFL-ZLabsPixel.txt')],
  ['Fusion Pixel Font — SIL Open Font License 1.1 and upstream notices', resolve(import.meta.dirname, '../src/assets/fonts/FusionPixel-NOTICES.txt')],
].map(([name, path]) => {
  const text = readFileSync(path, 'utf8')
  if (!text.trim()) throw new Error(`Empty WebUI notice: ${name}`)
  return `<h3>${name}</h3><pre style="white-space:pre-wrap;overflow-wrap:anywhere;font:inherit">${escapeHtml(text)}</pre>`
}).join('')
const entry = resolve(standalone, 'index.html')
const html = readFileSync(entry, 'utf8')
const closingBody = /<\/body>(\s*<\/html>\s*)$/i
if (!closingBody.test(html) || html.includes('id="inktag-legal-notices"')) throw new Error('Unexpected standalone notice insertion point')
// Bundled renderers contain literal </body> strings. Only append at the actual
// document end, and use a callback so notice text cannot expand $& / $` tokens.
writeFileSync(entry, sealStandalone(html.replace(closingBody, (_match, tail) => `<details id="inktag-legal-notices" style="margin:16px;padding:12px;font:12px/1.6 sans-serif"><summary>许可与第三方声明 / Licensing and third-party notices</summary>${notices}</details></body>${tail}`)))
renameSync(resolve(standalone, 'index.html'), resolve(standalone, 'InkTag-WebUI.html'))
rmSync(resolve(standalone, 'LICENSING.txt'), { force: true })
rmSync(resolve(standalone, 'THIRD_PARTY_NOTICES.txt'), { force: true })
// Layout-design baselines are dev-only public assets. Vite copies public/ for
// every build, but the distributable standalone contract is deliberately one
// HTML core, with optional fonts/photos beside it in assets/.
rmSync(resolve(standalone, '.local'), { recursive: true, force: true })
rmSync(resolve(standalone, 'layouts-generated'), { recursive: true, force: true })
