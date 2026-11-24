import { expect, test } from '@playwright/test'

test.use({ locale: 'zh-CN' })
const root = 'https://solar2ain.github.io/ink-tag/updates/'
for (const edition of ['static', 'standalone']) {
  test(`${edition}: stable-only anonymous updates ignore retired preferences and do not block settings`, async ({ page }) => {
    const requests: string[] = []
    await page.addInitScript(() => {
      localStorage.setItem('inktag-getting-started-v1', 'seen')
      localStorage.setItem('inktag-update-channel-v1', 'preview')
      Object.defineProperty(navigator, 'bluetooth', { configurable: true, value: {
        getAvailability: async () => false, getDevices: async () => [],
        requestDevice: async () => { throw new Error('Forbidden in offline update test') },
      } })
    })
    await page.route('**/*', async route => {
      const request = route.request(), url = request.url()
      if (url.startsWith(root)) {
        requests.push(url)
        expect(request.method()).toBe('GET')
        expect(request.postData()).toBeNull()
        expect(request.headers()['authorization']).toBeUndefined()
        expect(request.headers()['cookie']).toBeUndefined()
        expect(url).toBe(root + 'stable.json')
        if (requests.length === 1) return route.fulfill({ status: 503, contentType: 'application/json', body: '{}' })
        return route.fulfill({ contentType: 'application/json', body: JSON.stringify({
          schemaVersion: 3, channel: 'stable', candidateOnly: false, releaseReady: false,
          firmwareSelection: { published: false }, firmware: [],
          components: { webui: null, cli: null, localServer: null },
        }) })
      }
      if (url.startsWith('http://127.0.0.1:43127/')) return route.continue()
      return route.abort()
    })
    // Serve the same isolated build. Standalone is supplied by a fixture route,
    // never by a running user service, account or Bluetooth connection.
    if (edition === 'standalone') {
      const { readFileSync } = await import('node:fs')
      await page.route('http://127.0.0.1:43127/standalone.html*', route => route.fulfill({
        contentType: 'text/html', body: readFileSync('dist/standalone/InkTag-WebUI.html'),
      }))
    }
    await page.goto((edition === 'standalone' ? '/standalone.html' : '/') + '#/device?tab=about')
    await expect(page.getByRole('heading', { name: '客户端更新', exact: true })).toBeVisible()
    await expect(page.locator('.client-updates')).toContainText(/WebUI 1\.2\.0 · 构建 web-[0-9a-f]{20}/)
    expect(requests).toEqual([])
    await page.getByRole('button', { name: '检查客户端更新', exact: true }).click()
    await expect(page.locator('.client-updates')).toContainText('HTTP 503')
    await expect(page.locator('.client-updates')).toContainText('不影响设备连接与其他功能')
    expect(requests).toEqual([root + 'stable.json'])
    await expect(page.getByLabel('更新通道', { exact: true })).toHaveCount(0)
    await page.getByRole('button', { name: '检查客户端更新', exact: true }).click()
    await expect(page.locator('.client-updates')).toContainText('尚无已批准发布的客户端')
    expect(requests).toEqual([root + 'stable.json', root + 'stable.json'])
    await expect(page.getByRole('heading', { name: '客户端设置', exact: true })).toBeVisible()
    await page.reload()
    await expect(page.getByLabel('更新通道', { exact: true })).toHaveCount(0)
    await page.getByRole('button', { name: '检查客户端更新', exact: true }).click()
    await expect(page.locator('.client-updates')).toContainText('尚无已批准发布的客户端')
    expect(requests.every(url => url === root + 'stable.json')).toBe(true)
  })
}
