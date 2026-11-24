import { expect, test } from '@playwright/test'

test.use({ locale: 'zh-CN' })
test('a first-visit BLE link opens a visible prompt and the native chooser only on click', async ({ page }) => {
  await page.route('**/*', route => route.request().url().startsWith('http://127.0.0.1:43127/') ? route.continue() : route.abort())
  await page.addInitScript(() => {
    Object.defineProperty(navigator, 'bluetooth', { configurable: true, value: {
      getAvailability: async () => true, getDevices: async () => [],
      requestDevice: async (options: unknown) => {
        Object.assign(window, { chooser: { options, active: navigator.userActivation.isActive } })
        throw new DOMException('No device selected', 'NotFoundError')
      },
    } })
  })
  await page.goto('/?ble=IKT_TL_1234#/')
  const dialog = page.getByRole('dialog', { name: '连接设备', exact: true })
  await expect(dialog).toBeVisible()
  await expect(dialog).toContainText('IKT_TL_1234')
  await expect(page.getByRole('dialog', { name: '认识墨水屏' })).toHaveCount(0)
  expect(await page.evaluate(() => localStorage.getItem('inktag-getting-started-v1'))).toBeNull()
  expect(await page.evaluate(() => 'chooser' in window)).toBe(false)
  await dialog.getByRole('button', { name: '选择蓝牙设备' }).click()
  expect(await page.evaluate(() => (window as unknown as { chooser: { active: boolean } }).chooser.active)).toBe(true)
  await expect(dialog).toHaveCount(0)
  await expect(page.getByRole('button', { name: '选择设备', exact: true })).toBeVisible()
})
