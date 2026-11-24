import { readFileSync, readdirSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs'
import { execFileSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import { resolve } from 'node:path'
import { verifyHtml } from './html-security.mjs'
import { clientBuildId } from './client-build.mjs'

const dist = resolve(process.argv[2] || resolve(import.meta.dirname, '..', 'dist'))
const staticBuild = resolve(dist, 'static')
const standalone = resolve(dist, 'standalone')

const staticEntries = readdirSync(staticBuild, { withFileTypes: true })
const staticNames = staticEntries.map((entry) => entry.name).sort()
if (JSON.stringify(staticNames) !== JSON.stringify(['LICENSING.txt', 'THIRD_PARTY_NOTICES.txt', 'assets', 'index.html']) ||
    staticEntries.some((entry) => entry.name === 'assets' ? !entry.isDirectory() : !entry.isFile())) {
  throw new Error(`WebUI static build must contain only index.html, assets/ and the two license notices, got: ${staticNames.join(', ')}`)
}
for (const name of ['LICENSING.txt', 'THIRD_PARTY_NOTICES.txt']) {
  if (!readFileSync(resolve(staticBuild, name), 'utf8').trim()) throw new Error(`Empty WebUI notice: ${name}`)
}

const staticHtml = readFileSync(resolve(staticBuild, 'index.html'), 'utf8')
function pairing(html, bodies) {
  const meta = Object.fromEntries([...html.matchAll(/<meta name="(inktag-[\w-]+)" content="([^"]*)"/g)].map(m => [m[1], m[2]]))
  const expected = clientBuildId(resolve(import.meta.dirname, '..'))
  if (meta['inktag-client-build'] !== expected || meta['inktag-protocol-version'] !== '16' ||
      !bodies.some(body => body.includes(expected)) ||
      !bodies.some(body => /Object\.freeze\(\{version:[^,]{1,32},major:\d+,minor:\d+,patch:\d+,protocolVersion:16,otaVersion:/.test(body))) {
    throw new Error('WebUI compiled assets, protocol or source build fingerprint do not match')
  }
  return JSON.stringify(meta)
}
const staticPair = pairing(staticHtml, [...staticHtml.matchAll(/<script\b[^>]*\bsrc="\.\/(assets\/[^"]+)"/g)].map(m => readFileSync(resolve(staticBuild, m[1]), 'utf8')))
verifyHtml(staticHtml)
const externalModule = /<script\b[^>]*\btype=["']module["'][^>]*\bsrc=["']\.\/assets\//i
const externalStylesheet = /<link\b[^>]*\brel=["']stylesheet["'][^>]*\bhref=["']\.\/assets\//i
if (!externalModule.test(staticHtml) || !externalStylesheet.test(staticHtml)) {
  throw new Error('WebUI static build must reference external relative module and stylesheet assets')
}

const assetNames = readdirSync(resolve(staticBuild, 'assets'))
if (!assetNames.some((name) => name.endsWith('.js')) || !assetNames.some((name) => name.endsWith('.css'))) {
  throw new Error('WebUI static build is missing JavaScript or CSS assets')
}

const standaloneEntries = readdirSync(standalone, { withFileTypes: true })
const standaloneNames = standaloneEntries.map((entry) => entry.name).sort()
if (!standaloneEntries.some(e => e.name === 'InkTag-WebUI.html' && e.isFile()) || standaloneEntries.some(e => e.name !== 'InkTag-WebUI.html' && !(e.name === 'assets' && e.isDirectory()))) {
  throw new Error(`WebUI standalone build must contain only InkTag-WebUI.html and optional assets/, got: ${standaloneNames.join(', ')}`)
}

const standaloneHtml = readFileSync(resolve(standalone, 'InkTag-WebUI.html'), 'utf8')
if (pairing(standaloneHtml, [standaloneHtml]) !== staticPair) throw new Error('WebUI variants do not match')
verifyHtml(standaloneHtml, true)
const syntaxDirectory = mkdtempSync(resolve(tmpdir(), 'inktag-standalone-syntax-'))
try {
  for (const [index, match] of [...standaloneHtml.matchAll(/<script\b([^>]*)>([\s\S]*?)<\/script>/gi)].entries()) {
    if (/type=["'](?:application\/json|application\/ld\+json)["']/i.test(match[1])) continue
    const path = resolve(syntaxDirectory, `script-${index}.mjs`)
    writeFileSync(path, match[2])
    execFileSync(process.execPath, ['--check', path], { stdio: 'pipe' })
  }
} finally { rmSync(syntaxDirectory, { recursive: true, force: true }) }
if (!standaloneHtml.includes('id="inktag-legal-notices"')) {
  throw new Error('WebUI standalone build is missing readable licensing and third-party notices')
}
const externalScript = /<script\b[^>]*\bsrc\s*=/i
const externalStandaloneStylesheet = /<link\b[^>]*\brel=["']stylesheet["'][^>]*\bhref\s*=/i
if (externalScript.test(standaloneHtml) || externalStandaloneStylesheet.test(standaloneHtml)) {
  throw new Error('WebUI standalone build still references an external script or stylesheet')
}

const inlineScript = /<script\b(?![^>]*\bsrc\s*=)[^>]*>/i
const inlineStyle = /<style\b[^>]*>/i
if (!inlineScript.test(standaloneHtml) || !standaloneHtml.includes('</script>') ||
    !inlineStyle.test(standaloneHtml) || !standaloneHtml.includes('</style>')) {
  throw new Error('WebUI standalone build is missing inlined JavaScript or CSS')
}

console.log(`Verified static WebUI: dist/static (${assetNames.length} assets)`)
console.log(`Verified standalone WebUI: dist/standalone/InkTag-WebUI.html (${Buffer.byteLength(standaloneHtml, 'utf8')} bytes)`)

const declaredFontFiles = ['wenquanyi-bitmap-song-source.tar.gz', 'creation-font-licenses.txt', 'generate-creation-fonts.py']
const standaloneAssets = readdirSync(resolve(standalone, 'assets'))
for (const file of declaredFontFiles) if (!standaloneAssets.includes(file)) throw new Error(`Missing creation font source or notice: ${file}`)
for (const file of standaloneAssets) {
  if (!declaredFontFiles.includes(file) && !/^optional-(calligraphy|colorPhoto|pixelCompact|pixelMini|pixelSong)-[a-f0-9]{16}\.js$/.test(file)) throw new Error(`Unexpected standalone resource: ${file}`)
  if (!readFileSync(resolve(standalone, 'assets', file)).equals(readFileSync(resolve(staticBuild, 'assets', file)))) throw new Error(`Mismatched optional resource: ${file}`)
}
