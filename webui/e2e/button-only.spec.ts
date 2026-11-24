import { expect, test } from '@playwright/test'

test.use({ locale: 'zh-CN' })
for (const width of [1280, 390, 320]) {
  test(`button-only hardware and lab layout at ${width}px`, async ({ page }) => {
    await page.setViewportSize({ width, height: 900 })
    await page.route('**/*', route => route.request().url().startsWith('http://127.0.0.1:47128/') ? route.continue() : route.abort())
    await page.addInitScript(() => {
      localStorage.setItem('inktag-getting-started-v1', 'seen')
      Object.defineProperty(navigator, 'bluetooth', { configurable: true, value: {
        getAvailability: async () => false,
        getDevices: async () => [],
        requestDevice: async () => { throw new Error('Hardware forbidden in button-only layout test') },
      } })
    })
    await page.goto('/#/device?tab=hardware')
    const skip = page.getByRole('button', { name: '暂时跳过', exact: true })
    if (await skip.isVisible()) await skip.click()
    const section = page.locator('.board-aux-section')
    await expect(section.getByRole('heading', { name: '实体按键', exact: true })).toHaveCount(1)
    await expect(section.getByLabel('按键 1 GPIO', { exact: true })).toBeDisabled()
    await expect(section.getByLabel('按键 2 GPIO', { exact: true })).toBeDisabled()
    await expect(section).not.toContainText('蜂鸣')
    const bounds = await section.evaluate(node => {
      const header = node.querySelector('.panel-title')!.getBoundingClientRect()
      const fields = node.querySelector('.aux-io-grid')!.getBoundingClientRect()
      return { gap: fields.top - header.bottom, right: fields.right }
    })
    expect(bounds.gap).toBeGreaterThanOrEqual(8)
    expect(bounds.right).toBeLessThanOrEqual(width)
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true)
    await page.screenshot({ path: test.info().outputPath(`button-pins-${width}.png`), fullPage: true })
    await page.goto('/#/lab')
    await expect(page.locator('.test-card').filter({ hasText: '按键模拟' })).toHaveCount(1)
    await expect(page.locator('.test-card').filter({ hasText: '蜂鸣器' })).toHaveCount(0)
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true)
    await page.screenshot({ path: test.info().outputPath(`button-lab-${width}.png`), fullPage: true })
  })
}

