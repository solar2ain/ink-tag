import { execFileSync } from 'node:child_process'
import { mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { expect, it } from 'vitest'
import { webCsp, sealStandalone } from './html-security.mjs'
import { clientBuildId } from './client-build.mjs'

const build = clientBuildId(fileURLToPath(new URL('..', import.meta.url)))
const metadata = `<meta name="inktag-client-build" content="${build}"><meta name="inktag-protocol-version" content="16"><meta name="inktag-build-number" content="1">`
const compiled = `const build=${JSON.stringify(build)};const version=Object.freeze({version:'fixture',major:0,minor:2,patch:0,protocolVersion:16,otaVersion:1});`
const shell = (body: string) => `<!doctype html><html><head><meta charset="utf-8"><meta http-equiv="Content-Security-Policy" content="${webCsp}">${metadata}</head><body>${body}</body></html>`
const verifyTool = fileURLToPath(new URL('./verify-builds.mjs', import.meta.url))
const finalizerTool = fileURLToPath(new URL('./finalize-standalone.mjs', import.meta.url))

it.each(['valid', 'evidence', 'agent', 'missing-license', 'missing-third-party', 'bad-script',
  'inline', 'event', 'srcdoc', 'svg', 'refresh', 'data', 'standalone-added-script', 'missing-csp',
  'missing-metadata', 'wrong-build', 'wrong-protocol', 'variant-build-number', 'script-build-mismatch'])('checks actual distributable output: %s', (kind) => {
  const root = mkdtempSync(join(tmpdir(), 'inktag-build-check-'))
  try {
    const tools = join(root, 'tools'), dist = join(root, 'dist')
    mkdirSync(tools)
    mkdirSync(join(dist, 'static/assets'), { recursive: true })
    mkdirSync(join(dist, 'standalone/assets'), { recursive: true })
    for (const name of ['wenquanyi-bitmap-song-source.tar.gz', 'creation-font-licenses.txt', 'generate-creation-fonts.py']) {
      for (const variant of ['static', 'standalone']) writeFileSync(join(dist, variant, 'assets', name), 'fixture')
    }
    const staticHtml = shell('<script type="module" src="./assets/app.js"></script><link rel="stylesheet" href="./assets/app.css">')
    writeFileSync(join(dist, 'static/index.html'), staticHtml)
    writeFileSync(join(dist, 'static/assets/app.js'), `${compiled}export {}`)
    writeFileSync(join(dist, 'static/assets/app.css'), 'body{}')
    writeFileSync(join(dist, 'static/THIRD_PARTY_NOTICES.txt'), 'fixture notices')
    writeFileSync(join(dist, 'static/LICENSING.txt'), 'fixture license')
    const standaloneHtml = sealStandalone(shell(`<script>${compiled}void 0</script><style>body{}</style><details id="inktag-legal-notices">fixture notices</details>`))
    writeFileSync(join(dist, 'standalone/InkTag-WebUI.html'), standaloneHtml)
    if (kind === 'evidence') {
      mkdirSync(join(dist, 'static/layouts-generated'))
      writeFileSync(join(dist, 'static/layouts-generated/manifest.json'), '{"sourceSha256":{"private-input.c":"fixture"}}')
    }
    if (kind === 'agent') writeFileSync(join(dist, 'static/AGENTS.md'), 'internal instructions')
    if (kind === 'missing-license') rmSync(join(dist, 'static/LICENSING.txt'))
    if (kind === 'missing-third-party') rmSync(join(dist, 'static/THIRD_PARTY_NOTICES.txt'))
    if (kind === 'bad-script') writeFileSync(join(dist, 'standalone/InkTag-WebUI.html'), sealStandalone(shell(`<script>${compiled}const = ;</script><style>body{}</style><details id="inktag-legal-notices"></details>`)))
    const attacks: Record<string, string> = { inline: '<script>void 0</script>', event: '<img src="x" onerror="alert(1)">',
      srcdoc: '<iframe srcdoc="&lt;script&gt;alert(1)&lt;/script&gt;"></iframe>', svg: '<svg><script>void 0</script></svg>',
      refresh: '<meta http-equiv="refresh" content="0;url=https://example.invalid">', data: '<a href="data:text/html,hi">open</a>' }
    if (attacks[kind]) writeFileSync(join(dist, 'static/index.html'), staticHtml + attacks[kind])
    if (kind === 'missing-csp') writeFileSync(join(dist, 'static/index.html'), staticHtml.replace(/<meta http-equiv[^>]+>/, ''))
    if (kind === 'missing-metadata') writeFileSync(join(dist, 'static/index.html'), staticHtml.replace(metadata, ''))
    if (kind === 'wrong-build') writeFileSync(join(dist, 'static/index.html'), staticHtml.replace(build, 'web-00000000000000000000'))
    if (kind === 'wrong-protocol') writeFileSync(join(dist, 'static/assets/app.js'), compiled.replace('protocolVersion:16', 'protocolVersion:17'))
    if (kind === 'variant-build-number') writeFileSync(join(dist, 'standalone/InkTag-WebUI.html'), standaloneHtml.replace('inktag-build-number" content="1', 'inktag-build-number" content="2'))
    if (kind === 'script-build-mismatch') writeFileSync(join(dist, 'static/assets/app.js'), compiled.replace(build, 'web-00000000000000000000'))
    if (kind === 'standalone-added-script') writeFileSync(join(dist, 'standalone/InkTag-WebUI.html'), standaloneHtml + '<script>void 1</script>')
    const verify = () => execFileSync(process.execPath, [verifyTool, dist], { stdio: 'pipe' })
    if (kind !== 'valid') expect(verify).toThrow()
    else expect(verify().toString()).toContain('Verified static WebUI')
  } finally {
    rmSync(root, { recursive: true, force: true })
  }
}, 15_000)

it('keeps both notices readable and inert in the actual single-file finalizer', () => {
  const root = mkdtempSync(join(tmpdir(), 'inktag-standalone-notices-'))
  try {
    mkdirSync(join(root, 'tools'))
    mkdirSync(join(root, 'dist/static'), { recursive: true })
    const standalone = join(root, 'dist/standalone')
    mkdirSync(standalone)
    writeFileSync(join(standalone, 'index.html'), shell('<script>const exportedHtml="</body>";void exportedHtml</script><style>body{}</style>'))
    writeFileSync(join(standalone, 'LICENSING.txt'), 'CC BY-NC-SA 4.0\n<example> & text\n`Required Notice:` $& $`')
    writeFileSync(join(standalone, 'THIRD_PARTY_NOTICES.txt'), 'Copyright\n</pre><script>unexpected()</script>')
    execFileSync(process.execPath, [finalizerTool, join(root, 'dist')], { stdio: 'pipe' })
    expect(readdirSync(standalone)).toEqual(['InkTag-WebUI.html'])
    const html = readFileSync(join(standalone, 'InkTag-WebUI.html'), 'utf8')
    expect(html).toContain('<details id="inktag-legal-notices"')
    expect(html).toContain('CC BY-NC-SA 4.0\n&lt;example&gt; &amp; text')
    expect(html).toContain('Copyright\n&lt;/pre&gt;&lt;script&gt;unexpected()&lt;/script&gt;')
    expect(html).not.toContain('<script>unexpected()')
    expect(html).toContain('<script>const exportedHtml="</body>";void exportedHtml</script>')
    expect(html).toContain('`Required Notice:` $&amp; $`')
    expect(html.indexOf('id="inktag-legal-notices"')).toBeGreaterThan(html.indexOf('</style>'))
  } finally {
    rmSync(root, { recursive: true, force: true })
  }
})
