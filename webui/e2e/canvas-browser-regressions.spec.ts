import { expect, test, type Page } from '@playwright/test'
import { readFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'

test.use({ locale: 'zh-CN' })
test.beforeEach(async ({ page }) => {
  await page.addInitScript(() => localStorage.setItem('inktag-getting-started-v1', 'seen'))
})
async function open(page: Page, template: string, mode = 'static') {
  await page.goto(`/#/create/canvas/${template}?width=400&height=300&pageMode=${mode}`)
  await expect(page.getByLabel('创作画板')).toBeVisible({ timeout: 15000 })
}
async function editFirstCell(page: Page, width: number, name: string | RegExp) {
  if (width < 768) await page.getByRole('button', { name: '打开编辑面板', exact: true }).click()
  await page.locator('.layer-select').filter({ hasText: name }).first().click()
  if (width < 768) await page.getByRole('button', { name: '关闭编辑面板', exact: true }).click()
  await page.getByRole('button', { name: '编辑格子', exact: true }).click()
  if (width < 768) {
    await page.getByRole('button', { name: '打开编辑面板', exact: true }).click()
    await page.getByRole('navigation', { name: '编辑面板', exact: true }).getByRole('button', { name: '属性', exact: true }).click()
  }
}

for (const width of [1440, 390]) {
  test(`cell formatting survives the inline-input blur at ${width}px`, async ({ page }) => {
    await page.setViewportSize({ width, height: 1000 })
    await open(page, 'schedule-week')
    await editFirstCell(page, width, /^课表$/)
    const inspector = page.locator('.properties')
    await expect(inspector.getByRole('heading', { name: '内容 · 1 行 1 列', exact: true })).toBeVisible()
    await inspector.getByLabel('选中文字字号', { exact: true }).fill('18')
    await inspector.getByRole('button', { name: '文字颜色 · 红', exact: true }).click()
    await expect(inspector.getByLabel('选中文字字号', { exact: true })).toHaveValue('18')
    await expect(inspector.getByRole('button', { name: '文字颜色 · 红', exact: true })).toHaveAttribute('aria-pressed', 'true')
    await inspector.getByRole('button', { name: '选整个控件', exact: true }).click()
    await expect(inspector.getByLabel('字号', { exact: true })).toHaveValue('15')
  })
  test(`todo completion survives the inline-input blur at ${width}px`, async ({ page }) => {
    await page.setViewportSize({ width, height: 1000 })
    await open(page, 'blank')
    if (width < 768) await page.getByRole('button', { name: '打开编辑面板', exact: true }).click()
    await page.locator('.control-library').getByRole('button', { name: '待办事项', exact: true }).click()
    // A newly added widget is already selected; editing its first cell focuses the inline input.
    await page.getByRole('button', { name: '编辑格子', exact: true }).click()
    if (width < 768) {
      await page.getByRole('button', { name: '打开编辑面板', exact: true }).click()
      await page.getByRole('navigation', { name: '编辑面板', exact: true }).getByRole('button', { name: '属性', exact: true }).click()
    }
    await page.getByRole('button', { name: '标为完成', exact: true }).click()
    await expect(page.getByRole('button', { name: '标为未完成', exact: true })).toBeVisible()
    await page.getByRole('button', { name: '撤销', exact: true }).click()
    await expect(page.getByRole('button', { name: '标为完成', exact: true })).toBeVisible()
  })
}

for (const mode of ['static', 'dynamic', 'host-dynamic']) test(`320px ${mode} canvas fits the client width with a real scrollbar gutter`, async ({ page }) => {
  // Force a non-overlay scrollbar so comparing against innerWidth cannot hide the 15px regression.
  await page.setViewportSize({ width: 320, height: 812 })
  await open(page, 'blank', mode)
  await page.addStyleTag({ content: 'html { overflow-y: scroll !important } ::-webkit-scrollbar { width: 15px }' })
  const fits = () => page.evaluate(() => document.documentElement.scrollWidth <= document.documentElement.clientWidth)
  await expect.poll(fits).toBe(true)
  await page.getByRole('button', { name: '打开编辑面板', exact: true }).click()
  await expect.poll(fits).toBe(true)
  await page.getByRole('navigation', { name: '编辑面板', exact: true }).getByRole('button', { name: '属性', exact: true }).click()
  await expect.poll(fits).toBe(true)
})

for (const mode of ['static', 'host-dynamic']) test(`${mode} downloads real original-resolution PNG bytes`, async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 1000 })
  await open(page, 'blank', mode)
  const event = page.waitForEvent('download')
  await page.getByRole('button', { name: '下载 PNG', exact: true }).click()
  const file = await event, bytes = await readFile((await file.path())!)
  expect(await file.failure()).toBeNull()
  expect(bytes.subarray(0, 8)).toEqual(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]))
  expect(bytes.readUInt32BE(16)).toBe(400); expect(bytes.readUInt32BE(20)).toBe(300)
})

test('mobile disconnect remains accessible when only its icon is visible', async ({ page }) => {
  await page.setViewportSize({ width: 320, height: 812 })
  await page.addInitScript({ path: fileURLToPath(new URL('./virtual-bluetooth.js', import.meta.url)) })
  await page.goto('/?image-fixture=1#/create')
  await page.getByRole('button', { name: '选择设备', exact: true }).click()
  await expect(page.locator('.device-summary strong')).toHaveText('E2E Fixture')
  const button = page.getByRole('button', { name: '断开设备', exact: true })
  await expect(button).toBeVisible()
  await expect(button.locator('span')).toBeHidden()
  await button.click()
  await expect(page.locator('.device-summary strong')).toHaveText('未连接')
})
