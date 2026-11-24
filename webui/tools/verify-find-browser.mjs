import { createServer } from 'node:http'
import { readFileSync } from 'node:fs'
import { resolve, sep, extname } from 'node:path'
import { chromium } from '@playwright/test'

// Actual compiled Find page, fresh isolated browser and fixture-only APIs.
// Never use the running local service, an Apple session or a Bluetooth device.
const directory = resolve(process.argv[2] || 'dist/static')
const types = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.png': 'image/png' }
const calls = [], clientErrors = []
const server = createServer((request, response) => {
  const path = new URL(request.url, 'http://127.0.0.1').pathname
  calls.push({ path, method: request.method })
  const json = value => { response.setHeader('Content-Type', 'application/json'); response.end(JSON.stringify(value)) }
  if (path === '/api/v1/capabilities') return json({ apiVersion: 1, serverVersion: '0.2.0', runtime: 'python',
    features: ['records', 'assets', 'find-query'], collections: [], limits: { jsonBytes: 1048576, assetBytes: 16777216 } })
  if (path === '/api/v1/find/status') return json({ ok: true, loggedIn: false, queryReady: false, needs2fa: false,
    appleAccountUsername: '', sessionStored: false, sessionRestored: false, sessionError: null,
    methods: [], activeMethodIndex: null, address: '', keyFingerprint: '', advertisementKey: '',
    selectedKeyFingerprint: '', identities: [], sessionProtection: 'windows-dpapi', trackingIntervalMinutes: 15 })
  if (path.startsWith('/api/')) return json({ items: [] })
  try {
    const file = resolve(directory, '.' + (path === '/' ? '/index.html' : path))
    if (!file.startsWith(directory + sep)) throw Error('Outside fixture')
    response.setHeader('Content-Type', types[extname(file)] || 'application/octet-stream')
    response.end(readFileSync(file))
  } catch { response.writeHead(404).end() }
})
server.on('clientError', (error, socket) => { clientErrors.push(error.code); socket.destroy() })
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve))
const origin = `http://127.0.0.1:${server.address().port}`
let browser
try {
  browser = await chromium.launch({ headless: true })
  const context = await browser.newContext()
  const requests = [], errors = []
  await context.route('**/*', route => {
    requests.push(route.request().url())
    return new URL(route.request().url()).origin === origin ? route.continue() : route.abort()
  })
  await context.addInitScript(() => {
    Object.defineProperty(navigator, 'bluetooth', { value: {
      requestDevice() { throw Error('Real Bluetooth forbidden') }, async getDevices() { return [] }, async getAvailability() { return false },
    } })
  })
  const page = await context.newPage()
  page.on('pageerror', error => errors.push(error.message))
  await Promise.all([
    page.waitForResponse(response => response.url() === origin + '/api/v1/find/status'),
    page.goto(origin + '/#/find', { waitUntil: 'networkidle' }),
  ])
  await page.getByRole('heading', { name: 'InkTag Find', exact: true }).waitFor()
  const find = calls.filter(call => call.path.startsWith('/api/v1/find/'))
  if (!find.length || find.some(call => call.path !== '/api/v1/find/status' || call.method !== 'GET')) throw Error('Unexpected Find mutation/query')
  if (requests.some(url => new URL(url).origin !== origin) || errors.length || clientErrors.length) {
    throw Error(JSON.stringify({ requests, errors, clientErrors }))
  }
  console.log(JSON.stringify({ actualCompiledPage: true, protocol: 'http:', find, allRequestsSameOrigin: true,
    requestCount: requests.length, browserErrors: errors, tlsOnHttpErrors: clientErrors, realDeviceCalls: 0, realAccountCalls: 0 }))
} finally {
  if (browser) await browser.close()
  await new Promise(resolve => server.close(resolve))
}
