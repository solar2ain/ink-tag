import { expect, test } from '@playwright/test'

test.use({ locale: 'zh-CN' })

for (const width of [1280, 390]) {
  test(`E6 profiles and client settings remain available at ${width}px`, async ({ page }) => {
    await page.setViewportSize({ width, height: 900 })
    await page.addInitScript(() => {
      localStorage.setItem('inktag-getting-started-v1', 'seen')
      // Isolated browser fixture: never discover, connect or write a real device.
      Object.defineProperty(navigator, 'bluetooth', { configurable: true, value: {
        getAvailability: async () => false,
        getDevices: async () => [],
        requestDevice: async () => { throw new Error('Device access forbidden in frontend delivery test') },
      } })
    })
    await page.goto('/#/device?tab=screen')
    const skip = page.getByRole('button', { name: '暂时跳过', exact: true })
    if (await skip.isVisible()) await skip.click()
    await expect(page.getByRole('heading', { name: '客户端设置', exact: true })).toBeVisible()
    await expect(page.getByRole('heading', { name: '屏幕 BUSY 极性', exact: true })).toHaveCount(0)
    await page.getByRole('button', { name: '高级设置', exact: true }).click()
    const busy = page.getByLabel('BUSY 有效电平', { exact: true })
    await expect(busy).toBeVisible()
    await expect(busy.locator('xpath=../preceding-sibling::label[1]')).toContainText('屏幕驱动方案')
    await expect(page.getByRole('heading', { name: '当前有效接线', exact: true })).toHaveCount(0)
    for (const [ic, geometry, recipe] of [
      ['IST7513', '240x240', 'HT0154A10'],
      ['IST7601', '400x400', 'HT0169A06'],
      ['IST7601/JD7601', '720x480', 'HT037A02'],
    ]) {
      await page.locator('.profile-select-trigger').click()
      await expect(page.getByRole('heading', { name: 'IST 系列（E6）' })).toBeVisible()
      const profile = page.getByRole('option').filter({ hasText: geometry }).filter({ hasText: recipe })
      await expect(profile).toHaveCount(1)
      await expect(profile).toContainText(ic)
      await profile.click()
      await expect(page.locator('.profile-select-trigger')).toContainText(geometry)
      await expect(page.getByLabel('尺寸 / 分辨率', { exact: true })).toHaveValue(geometry)
      await expect(page.getByRole('button', { name: '测试清屏', exact: true })).toBeDisabled()
    }
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true)
    await page.screenshot({ path: test.info().outputPath(`e6-display-${width}.png`), fullPage: true })
    await page.goto('/#/device?tab=hardware')
    for (const name of ['屏幕引脚', 'LED 引脚', '扩展输入输出']) await expect(page.getByRole('heading', { name, exact: true })).toBeVisible()
    await expect(page.getByRole('heading', { name: '屏幕 BUSY 极性', exact: true })).toHaveCount(0)
    const fields = page.locator('.pin-grid input[type="number"]')
    await expect(fields).toHaveCount(10)
    for (const field of await fields.all()) { await expect(field).toHaveAttribute('placeholder', '--'); await expect(field).toHaveValue('') }
    await expect(page.getByRole('button', { name: '保存屏幕引脚', exact: true })).toBeDisabled()
    await expect(page.getByRole('button', { name: '保存LED 引脚', exact: true })).toBeDisabled()
    await expect(page.getByRole('button', { name: '添加按键', exact: true })).toBeVisible()
    await expect(page.getByRole('button', { name: '读取设备', exact: true })).toHaveCount(0)
    for (const checkbox of await page.locator('.pin-grid input[type="checkbox"], .board-aux-section input[type="checkbox"]').all()) {
      const size = await checkbox.boundingBox()
      expect(size?.width).toBe(16); expect(size?.height).toBe(16)
    }
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true)
    await page.screenshot({ path: test.info().outputPath(`board-routing-${width}.png`), fullPage: true })
    await page.goto('/#/device?tab=about')
    await expect(page.getByRole('button', { name: '查看入门教程', exact: true })).toBeVisible()
    await page.goto('/#/device?tab=preferences')
    await expect(page.getByLabel('语言', { exact: true })).toBeVisible()
    await expect(page.getByLabel('外观', { exact: true })).toBeVisible()
  })
}
