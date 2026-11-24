import { createServer } from 'node:http'
import { readFileSync, readdirSync } from 'node:fs'
import { resolve, sep, extname } from 'node:path'
import { chromium } from '@playwright/test'

// Real built modules under the shipped CSP; never connect to a device/service.
const directory = resolve(process.argv[2] || 'dist/static')
const chunk = readdirSync(resolve(directory, 'assets')).filter(name => /^ota-.*\.js$/.test(name))
if (chunk.length !== 1) throw Error('Expected one compiled OTA entry chunk')
const mime = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.png': 'image/png' }
const server = createServer((request, response) => {
  try {
    const pathname = decodeURIComponent(new URL(request.url, 'http://127.0.0.1').pathname)
    const path = resolve(directory, '.' + (pathname === '/' ? '/index.html' : pathname))
    if (!path.startsWith(directory + sep)) throw Error('Outside static directory')
    response.setHeader('Content-Type', mime[extname(path)] || 'application/octet-stream')
    response.end(readFileSync(path))
  } catch { response.writeHead(404).end() }
})
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve))
const origin = `http://127.0.0.1:${server.address().port}`
let browser
try {
  browser = await chromium.launch({ headless: true })
  const context = await browser.newContext()
  await context.route('**/*', route => new URL(route.request().url()).origin === origin ? route.continue() : route.abort())
  await context.addInitScript(() => {
    globalThis.otaQa = { csp: [], bleCalls: 0 }
    Object.defineProperty(navigator, 'bluetooth', { value: {
      requestDevice() { globalThis.otaQa.bleCalls++; throw Error('BLE forbidden in built OTA verification') },
      async getDevices() { return [] }, async getAvailability() { return false },
    } })
    document.addEventListener('securitypolicyviolation', e => globalThis.otaQa.csp.push(e.effectiveDirective))
  })
  const page = await context.newPage()
  await page.goto(origin + '/#/device?tab=ota', { waitUntil: 'networkidle' })
  const result = await page.evaluate(async chunk => {
    const module = await import('/assets/' + chunk)
    const entry = Object.values(module).filter(value => typeof value === 'function' && String(value).includes('.parsePackage('))
    if (entry.length !== 1) throw Error('Missing actual compiled package entry')
    const cases = []
    for (const [type, global] of [['nordic', 'NordicDfu'], ['goodix', 'GoodixOta'], ['onmicro', 'OnMicroOta'], ['telink', 'TelinkOta'], ['dialog', 'DialogSuota']]) {
      let error = null
      try { await entry[0](type, new File([], type === 'dialog' ? 'bad.img' : 'bad.zip')) }
      catch (failure) { error = String(failure) }
      if (typeof window[global]?.parsePackage !== 'function' || !error || /undefined|not a function/.test(error)) {
        throw Error(`Unregistered/broken ${type} package parser: ${error}`)
      }
      cases.push({ type, malformedPackageRejected: true })
    }
    await new Promise(resolve => setTimeout(resolve, 0))
    return { ...globalThis.otaQa, cases }
  }, chunk[0])
  if (result.csp.length || result.bleCalls) throw Error('CSP violation or device attempt: ' + JSON.stringify(result))
  console.log(JSON.stringify({ ...result, directory, bypassCSP: false, browser: browser.version() }))
} finally {
  if (browser) await browser.close()
  await new Promise(resolve => server.close(resolve))
}
