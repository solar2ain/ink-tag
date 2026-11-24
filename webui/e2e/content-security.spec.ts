import { expect, test } from '@playwright/test'
import { readFileSync } from 'node:fs'
import { pathToFileURL } from 'node:url'
import { resolve } from 'node:path'

const origin = 'http://127.0.0.1:43127'
const staticHtml = () => readFileSync('dist/static/index.html', 'utf8')
const standaloneHtml = () => readFileSync('dist/standalone/InkTag-WebUI.html', 'utf8')
test.use({ locale: 'zh-CN' })

test('control: the same harmless HTML payload executes without CSP', async ({ page }) => {
  await page.route(`${origin}/unsafe-control.html`, route => route.fulfill({ contentType: 'text/html', body:
    '<!doctype html><html><head><meta charset="utf-8"></head><body><script>window.auditControl=["inline"]</script>'+
    '<img src="/missing-control.png" onerror="auditControl.push(\'event\')">'+
    '<iframe srcdoc="&lt;script&gt;parent.auditControl.push(\'srcdoc\')&lt;/script&gt;"></iframe>'+
    '<svg onload="auditControl.push(\'svg\')"></svg></body></html>' }))
  await page.goto('/unsafe-control.html')
  expect(await page.evaluate(() => (window as any).auditControl.sort())).toEqual(['event', 'inline', 'srcdoc', 'svg'])
})

for (const edition of ['static', 'standalone', 'header']) {
  test(`${edition}: browser blocks injected script, event, srcdoc, SVG and eval`, async ({ page }) => {
    await page.addInitScript(() => {
      localStorage.setItem('inktag-getting-started-v1', 'seen')
      Object.defineProperty(navigator, 'bluetooth', { configurable: true, value: {
        getAvailability: async () => false, getDevices: async () => [],
        requestDevice: async () => { throw new Error('No hardware in CSP regression') },
      } })
      ;(window as any).auditExecuted = []
      ;(window as any).auditViolations = []
      document.addEventListener('securitypolicyviolation', event => {
        ;(window as any).auditViolations.push(event.effectiveDirective)
      })
    })
    let html = edition === 'standalone' ? standaloneHtml() : staticHtml()
    const headers: Record<string, string> = {}
    if (edition === 'header') {
      const policy = html.match(/<meta http-equiv="Content-Security-Policy" content="([^"]+)"/)![1]
      headers['Content-Security-Policy'] = policy + "; frame-ancestors 'none'"
      html = html.replace(/<meta http-equiv="Content-Security-Policy"[^>]*>/, '')
    }
    html += `<script>auditExecuted.push('inline')</script>
      <img src="/not-a-real-image.png" onerror="auditExecuted.push('event')">
      <iframe srcdoc="&lt;script&gt;parent.auditExecuted.push('srcdoc')&lt;/script&gt;"></iframe>
      <iframe src="https://example.invalid/frame.html"></iframe>
      <svg onload="auditExecuted.push('svg-event')"><script>auditExecuted.push('svg-script')</script></svg>
      <script src="https://example.invalid/injected.js"></script>`
    await page.route('**/*', route => route.request().url().startsWith(origin + '/') ? route.continue() : route.abort())
    await page.route(`${origin}/csp-fixture.html*`, route => route.fulfill({ contentType: 'text/html', headers, body: html }))
    await page.goto('/csp-fixture.html#/device?tab=about')
    await expect(page.getByRole('heading', { name: '客户端设置', exact: true })).toBeVisible()
    // Event and load dispatch have occurred by the completed page navigation.
    expect(await page.evaluate(() => (window as any).auditExecuted)).toEqual([])
    const violations = await page.evaluate(() => (window as any).auditViolations as string[])
    expect(violations).toContain('script-src-elem')
    expect(violations).toContain('script-src-attr')
    expect(violations).toContain('frame-src')
    // Execute from an unprivileged same-origin script (Playwright evaluate bypasses CSP).
    await page.route(`${origin}/assets/csp-eval.js`, route => route.fulfill({ contentType: 'application/javascript', body:
      `try { (0,eval)("auditExecuted.push('eval')") } catch (_) { globalThis.auditEvalBlocked = true }` }))
    await page.evaluate(() => { const script = document.createElement('script'); script.src = './assets/csp-eval.js'; document.body.append(script) })
    await expect.poll(() => page.evaluate(() => (window as any).auditEvalBlocked)).toBe(true)
    expect(await page.evaluate(() => (window as any).auditExecuted)).toEqual([])
    // WASM compilation is intentionally allowed without JavaScript unsafe-eval.
    expect(await page.evaluate(async () => !!await WebAssembly.compile(new Uint8Array([0,97,115,109,1,0,0,0])))).toBe(true)
    await page.route('https://api.open-meteo.com/**', route => route.fulfill({ contentType: 'application/json', body: '{"fixture":true}' }))
    expect(await page.evaluate(async () => (await fetch('https://api.open-meteo.com/v1/forecast')).ok)).toBe(true)
    expect(await page.evaluate(async () => { try { await fetch('https://example.invalid/exfil'); return false } catch { return true } })).toBe(true)
  })
}

for (const edition of ['static', 'standalone', 'file']) {
  test(`${edition}: editor, native preview and optional assets work under CSP`, async ({ page }) => {
    await page.addInitScript(() => {
      localStorage.setItem('inktag-getting-started-v1', 'seen')
      ;(window as any).auditViolations = []
      document.addEventListener('securitypolicyviolation', event => { (window as any).auditViolations.push(event.effectiveDirective) })
    })
    const errors: string[] = []
    page.on('pageerror', error => errors.push(error.message))
    await page.route('**/*', route => route.request().url().startsWith(origin + '/') || route.request().url().startsWith('file:') ? route.continue() : route.abort())
    if (edition === 'standalone') await page.route(`${origin}/standalone.html*`, route => route.fulfill({ contentType: 'text/html', body: standaloneHtml() }))
    const entry = edition === 'file' ? pathToFileURL(resolve('dist/standalone/InkTag-WebUI.html')).href : edition === 'standalone' ? '/standalone.html' : '/'
    await page.goto(entry + '#/create/canvas/blank?width=400&height=300&pageMode=static')
    await expect(page.getByLabel('创作画板')).toBeVisible()
    await page.getByRole('button', { name: '设备效果预览', exact: true }).click()
    await expect(page.getByRole('dialog', { name: '设备效果预览' }).getByRole('status')).toContainText('按画板色板模拟')
    const { readdirSync } = await import('node:fs')
    const asset = readdirSync('dist/static/assets').find(name => name.startsWith('optional-calligraphy-'))!
    await page.evaluate(async asset => {
      await new Promise<void>((resolve, reject) => { const script = document.createElement('script'); script.src = './assets/' + asset; script.onload = () => resolve(); script.onerror = () => reject(new Error('optional script blocked')); document.body.append(script) })
      const value = Object.values((window as any).__inktagOptionalAssets).find(value => typeof value === 'string' && value.startsWith('data:font/woff2')) as string
      await new FontFace('CSPFixture', `url(${value})`).load()
    }, asset)
    expect(errors).toEqual([])
    expect(await page.evaluate(() => (window as any).auditViolations)).toEqual([])
  })
}
