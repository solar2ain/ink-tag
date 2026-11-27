import { expect, test } from '@playwright/test'

const fixture = {
  name: 'preview.png',
  mimeType: 'image/png',
  buffer: Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aZ1sAAAAASUVORK5CYII=', 'base64'),
}

test.use({
  locale: 'zh-CN',
  launchOptions: {
    ignoreDefaultArgs: ['--hide-scrollbars'],
    args: ['--disable-features=OverlayScrollbar,FluentOverlayScrollbar'],
  },
})

test('mobile image preview stays at the same scale near the scrollbar threshold and after returning', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 714 })
  await page.addInitScript(() => localStorage.setItem('inktag-getting-started-v1', 'seen'))
  await page.goto('/#/image')
  await page.addStyleTag({ content: '.image-viewport { width: 270px; } .image-viewport::-webkit-scrollbar { width: 16px; height: 16px; }' })
  await page.getByRole('combobox', { name: '分辨率', exact: true }).selectOption('240x240')
  await page.locator('input[type=file]').setInputFiles(fixture)
  const frame = page.locator('.image-device-frame')
  const sample = () => frame.evaluate(async element => {
    const widths = []
    for (let i = 0; i < 20; i++) {
      await new Promise<void>(resolve => requestAnimationFrame(() => resolve()))
      widths.push(element.getBoundingClientRect().width)
    }
    return [...new Set(widths)]
  })
  expect(await sample()).toEqual([240])
  await page.getByRole('button', { name: '放大预览', exact: true }).click()
  expect(await sample()).toEqual([300])
  await page.evaluate(() => { location.hash = '#/create' })
  await expect(page.getByRole('heading', { name: '创作', exact: true })).toBeVisible()
  await page.evaluate(() => { location.hash = '#/image' })
  expect(await sample()).toEqual([300])

  for (const width of [320, 1280]) {
    await page.setViewportSize({ width, height: 800 })
    const heavy = (await page.getByLabel('重色模式').boundingBox())!
    const warm = (await page.getByLabel('暖色模式').boundingBox())!
    expect(heavy.y).toBe(warm.y)
    expect(heavy.x + heavy.width).toBeLessThan(warm.x)
  }
})

test('desktop image controls fit without an inner scrollbar and the preview fills the row', async ({ page }) => {
  await page.setViewportSize({ width: 1280, height: 650 })
  await page.addInitScript(() => localStorage.setItem('inktag-getting-started-v1', 'seen'))
  await page.goto('/#/image')
  const controls = page.locator('.image-settings')
  const preview = page.locator('.image-preview-panel')
  const controlContentHeight = () => controls.evaluate(element =>
    element.lastElementChild!.getBoundingClientRect().bottom - element.getBoundingClientRect().top)
  const expectPanelFitsControls = async () => {
    expect((await preview.boundingBox())!.height).toBeCloseTo(await controlContentHeight(), 0)
  }

  await expect(preview.getByRole('button', { name: /选择或拖入图片/ })).toBeVisible()
  await expectPanelFitsControls()
  await page.getByRole('combobox', { name: '分辨率', exact: true }).selectOption('240x240')
  await page.locator('input[type=file]').setInputFiles(fixture)
  await expect(page.locator('.image-device-frame')).toBeVisible()
  expect(await controls.evaluate(element => element.scrollHeight - element.clientHeight)).toBeLessThanOrEqual(1)
  const height = (await preview.boundingBox())!.height
  expect(height).toBe((await controls.boundingBox())!.height)
  expect(height).toBeGreaterThan(500)
  expect((await preview.locator('.image-viewport').boundingBox())!.height).toBeGreaterThan(400)
  const zoom = page.getByRole('button', { name: '放大预览', exact: true })
  for (let i = 0; i < 8; i++) await zoom.click()
  expect((await preview.boundingBox())!.height).toBe(height)
  const viewport = preview.locator('.image-viewport')
  expect(await viewport.evaluate(element => element.scrollHeight > element.clientHeight)).toBe(true)

  await page.setViewportSize({ width: 1280, height: 1100 })
  await expectPanelFitsControls()
  expect((await preview.boundingBox())!.height).toBe(height)

  await page.setViewportSize({ width: 1280, height: 650 })
  await page.getByLabel('暖色模式').scrollIntoViewIfNeeded()
  expect(await page.evaluate(() => window.scrollY)).toBeGreaterThan(0)
  expect(await controls.evaluate(element => element.scrollTop)).toBe(0)
})

test('image loading errors keep the regular page gutters', async ({ page }) => {
  await page.addInitScript(() => localStorage.setItem('inktag-getting-started-v1', 'seen'))
  await page.route('**/assets/ImagePage-*.js', route => route.abort())
  await page.goto('/#/image')
  const alert = page.getByRole('alert').filter({ hasText: '图片工具未能打开' })
  await expect(alert).toBeVisible()
  for (const width of [320, 1280]) {
    await page.setViewportSize({ width, height: 800 })
    const main = (await page.getByRole('main').boundingBox())!
    const panel = (await alert.boundingBox())!
    expect(panel.x - main.x).toBeGreaterThanOrEqual(12)
    expect(main.x + main.width - panel.x - panel.width).toBeGreaterThanOrEqual(12)
  }
})
