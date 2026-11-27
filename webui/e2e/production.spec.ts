import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { expect, test } from '@playwright/test'

const here = path.dirname(fileURLToPath(import.meta.url))

test.use({ locale: 'zh-CN' })

test('acknowledged time refresh waits through automatic BUSY and ignores its old completion', async ({ page }) => {
  await page.goto('/?time-display-busy#/')
  await page.getByRole('button', { name: /^(打开浏览器记住的设备|连接其他设备)$/ }).click()
  await page.getByRole('button', { name: /选择附近设备/ }).click()
  await expect.poll(() => page.evaluate(() => (window as any).__inktagImageCommands.filter((op: number) => op === 0x06).length)).toBe(1)
  const before = await page.evaluate(() => {
    (window as any).__timeDisplayFixture.armed = true
    return (window as any).__inktagImageCommands.length
  })
  const calendar = page.getByRole('button', { name: '日历', exact: true })
  await calendar.click()
  await expect.poll(() => page.evaluate(() => (window as any).__timeDisplayFixture.attempts)).toBe(2)
  await expect(calendar).toBeDisabled()
  await expect.poll(() => page.evaluate(() => (window as any).__timeDisplayFixture.completed)).toBe(true)
  await expect(calendar).toBeEnabled()
  await expect(page.locator('.operation-dialog.failed')).toHaveCount(0)
  const commands = await page.evaluate((start) => (window as any).__inktagImageCommands.slice(start) as number[], before)
  expect(commands.filter((op) => op === 0x06)).toEqual([0x06])
  expect(commands.filter((op) => op === 0x09)).toEqual([0x09, 0x09])
  expect(commands.filter((op) => op === 0x0c)).toEqual([])
})

test('identity dialog explains English names and blocks Chinese input without writes', async ({ page }) => {
  await page.goto('/#/device?tab=overview')
  await page.getByRole('button', { name: /^(打开浏览器记住的设备|连接其他设备)$/ }).click()
  await page.getByRole('button', { name: /选择附近设备/ }).click()
  const before = await page.evaluate(() => (window as any).__inktagImageCommands.slice())
  await page.getByRole('button', { name: '修改设备名', exact: true }).click()
  const dialog = page.getByRole('dialog', { name: '修改设备名称' })
  const input = dialog.getByPlaceholder('例如：InkTag 3A57')
  await expect(input).toHaveValue('E2E Fixture')
  await expect(dialog.locator('.prefixed-name-input > span')).toHaveText('InkTag_')
  await expect(dialog.locator('.prefixed-name-input input')).toHaveValue('GR_E2E')
  await expect(dialog).toContainText('仅英文字符')
  await input.fill('客厅')
  await expect(input).toHaveAttribute('aria-invalid', 'true')
  await expect(dialog).toContainText('设备名只支持英文、数字、空格和英文标点')
  await expect(dialog.getByRole('button', { name: '保存修改', exact: true })).toBeDisabled()
  await input.fill('InkTag 3A57')
  await expect(input).toHaveAttribute('aria-invalid', 'false')
  for (const width of [1280, 390]) {
    await page.setViewportSize({ width, height: 900 })
    const bounds = await dialog.boundingBox()
    expect(bounds!.x).toBeGreaterThanOrEqual(0)
    expect(bounds!.x + bounds!.width).toBeLessThanOrEqual(width)
    await page.screenshot({ path: test.info().outputPath(`identity-final-${width}.png`) })
  }
  await dialog.getByRole('button', { name: '取消', exact: true }).click()
  expect(await page.evaluate(() => (window as any).__inktagImageCommands)).toEqual(before)
})

test('DA original horizontal profile is matched without changing device parameters', async ({page}) => {
  await page.goto('/?screen-fixture=da-original#/device?tab=screen')
  await page.getByRole('button', {name: /^(打开浏览器记住的设备|连接其他设备)$/}).click()
  await page.getByRole('button', {name: /选择附近设备/}).click()
  const title = 'SSD1619 2.9寸 128x296 三色 （适配asel29）'
  const trigger = page.locator('.profile-select-trigger')
  await expect(trigger.locator('strong')).toHaveText(title)
  await expect(page.locator('.profile-select-trigger')).not.toContainText('兼容')
  await expect(page.locator('.profile-select-trigger')).not.toContainText('IC 待确认')
  await expect(trigger.locator('small')).toHaveText('SSD1619 / 2.9寸 / 128x296 / BWR / STANDARD')
  await expect(trigger).not.toContainText('E029A33N8C380')
  await expect(trigger.locator('small')).toHaveAttribute('title', /E029A33N8C380 \/ HINK-E029A33-A0.*仅 da14585_asel29 原屏验证.*旋转 90°/)
  await expect(page.getByLabel('设备当前屏参')).toContainText('128×296 · BWR · 旋转 90° · 逻辑 296×128')
  await trigger.click()
  const option = page.getByRole('option').filter({has: page.getByText(title, {exact: true})})
  await expect(option.locator('strong')).toHaveText(title)
  await expect(option.locator('small')).toHaveText('SSD1619 / 2.9寸 / 128x296 / BWR / STANDARD')
  await page.keyboard.press('Escape')
  expect(await page.evaluate(() => (window as any).__screenRequests)).toEqual([])
  await page.screenshot({path: test.info().outputPath('da-profile-desktop.png'), fullPage: true})
  await page.setViewportSize({width: 390, height: 740})
  await expect(page.getByLabel('设备当前屏参')).toBeVisible()
  await expect(trigger.locator('strong')).toHaveText(title)
  expect(await trigger.locator('strong').evaluate((element) => element.scrollWidth <= element.clientWidth)).toBe(true)
  expect(await page.evaluate(() => (window as any).__screenRequests)).toEqual([])
  await page.screenshot({path: test.info().outputPath('da-profile-mobile.png'), fullPage: true})
})

test('DA post-display cached status updates the temperature without inventing a panel or MCU source', async ({page}) => {
  await page.goto('/?screen-fixture=da-original#/device?tab=overview')
  await page.getByRole('button', {name: /^(打开浏览器记住的设备|连接其他设备)$/}).click()
  await page.getByRole('button', {name: /选择附近设备/}).click()
  const fact = page.locator('.temperature-fact').first()
  await expect(fact.getByLabel('主温度')).toHaveText('--')
  const before = await page.evaluate(() => (window as any).__inktagImageCommands.length)
  await page.getByRole('button', {name: '测试', exact: true}).click()
  await page.getByRole('button', {name: '日期与时间', exact: true}).click()
  await page.getByRole('dialog').getByRole('button', {name: '+1 分钟', exact: true}).click()
  await expect(page.getByText('测试时间已更新，设备刷新完成。', {exact: true})).toBeVisible()
  await page.getByRole('dialog').getByRole('button', {name: '关闭', exact: true}).last().click()
  await page.getByRole('button', {name: '概览', exact: true}).click()
  await expect(fact.getByLabel('主温度')).toHaveText('23 °C')
  await fact.getByRole('button', {name: '了解温度来源'}).click()
  await expect(fact.getByRole('tooltip')).toContainText('设备仅返回主温度')
  await expect(fact.locator('.temperature-sources')).toHaveText('屏幕 -- / MCU 不支持')
  expect(await page.evaluate((start) => (window as any).__inktagImageCommands.slice(start), before)).toEqual([0x71, 0x04])
})

test('offline test cards explain connection prerequisites without reporting missing capabilities', async ({page}) => {
  await page.goto('/#/device?tab=test')
  await expect(page.locator('.test-card')).toHaveCount(8)
  for (const card of await page.locator('.test-card').all()) {
    await expect(card).toBeDisabled()
    await expect(card).toHaveAttribute('title', '请先连接设备')
    await expect(card).toHaveAttribute('aria-label', /请先连接设备/)
  }
  expect(await page.evaluate(() => (window as any).__inktagImageCommands)).toEqual([])
})

test('test menu groups screen choices and keeps button and buzzer separate with no automatic writes', async ({page}) => {
  await page.goto('/?image-fixture=empty#/device?tab=test')
  await page.getByRole('button', {name: /^(打开浏览器记住的设备|连接其他设备)$/}).click()
  await page.getByRole('button', {name: /选择附近设备/}).click()
  await expect(page.locator('.test-card h2')).toHaveText(['屏幕测试', '残影消除', '日期与时间', '轮播测试', 'NFC模拟', '按键模拟', '蜂鸣器测试', '灯效测试'])
  const before = await page.evaluate(() => (window as any).__inktagImageCommands.slice())
  await page.getByRole('button', {name: '屏幕测试', exact: true}).click()
  await expect(page.getByRole('dialog').getByRole('radio')).toHaveCount(5)
  await page.getByRole('dialog').getByRole('radio', {name: /像素网格/}).check()
  expect(await page.evaluate(() => (window as any).__inktagImageCommands)).toEqual(before)
  await page.getByRole('dialog').getByRole('button', {name: '关闭', exact: true}).last().click()
  await expect(page.getByRole('dialog')).toHaveCount(0)
  expect(await page.evaluate(() => (window as any).__inktagImageCommands)).toEqual(before)
})

test('log diagnostics show panel and MCU temperatures and clock read never synchronizes', async ({page}) => {
  await page.goto('/?compact-nrf-temperature=panel#/device?tab=logs')
  await page.getByRole('button', {name: /^(打开浏览器记住的设备|连接其他设备)$/}).click()
  await page.getByRole('button', {name: /选择附近设备/}).click()
  const diagnostics = page.locator('.device-diagnostics')
  await expect(diagnostics).toContainText('屏幕温度22.5 °C')
  await expect(diagnostics).toContainText('MCU 温度25 °C')
  const before = await page.evaluate(() => (window as any).__inktagImageCommands.length)
  await diagnostics.getByRole('button', {name: '走时诊断', exact: true}).click()
  await expect(diagnostics.locator('.clock-diagnostic')).toContainText('设备时间（设备时区）')
  const commands = await page.evaluate((start) => (window as any).__inktagImageCommands.slice(start), before)
  expect(commands).toEqual([0x05])
  await expect(diagnostics).toContainText('单次偏差不能证明长期精度')
  await page.screenshot({path: '../.local/webui-test-layout/desktop-diagnostics.png'})
  await page.setViewportSize({width: 390, height: 740})
  for (const value of await diagnostics.locator('.clock-diagnostic dd').all()) {
    expect(await value.evaluate((element) => element.scrollWidth <= element.clientWidth)).toBe(true)
  }
  await page.screenshot({path: '../.local/webui-test-layout/mobile-diagnostics.png'})
})

test('compact diagnostics use dashes and retain values in desktop/mobile light/dark layouts', async ({page}) => {
  await page.goto('/?compact-nrf-temperature=panel#/device?tab=logs')
  const diagnostics = page.locator('.device-diagnostics')
  const field = (name: string) => diagnostics.locator('.diagnostic-grid > div').filter({has: page.locator('dt', {hasText: new RegExp(`^${name}$`)})}).locator('dd')
  for (const label of ['主控', '固件', '协议', '地址', '电压', '屏幕温度', '上电遥测']) await expect(field(label)).toHaveText('--')
  await expect(diagnostics.locator('.notice')).toHaveCount(0)
  expect(await page.evaluate(() => (window as any).__inktagImageCommands)).toEqual([])
  await page.getByRole('button', {name: /^(打开浏览器记住的设备|连接其他设备)$/}).click()
  await page.getByRole('button', {name: /选择附近设备/}).click()
  await expect(field('屏幕温度')).toHaveText('22.5 °C')
  await expect(field('MCU 温度')).toHaveText('25 °C')
  const before = await page.evaluate(() => (window as any).__inktagImageCommands.slice())
  for (const theme of ['light', 'dark'] as const) {
    await page.emulateMedia({colorScheme: theme})
    await expect.poll(() => page.locator('html').getAttribute('data-resolved-theme')).toBe(theme)
    for (const width of [1280, 390]) {
      await page.setViewportSize({width, height: 900})
      const columns = width === 1280 ? 4 : 2
      await expect.poll(() => diagnostics.locator('.diagnostic-grid').evaluate(element => getComputedStyle(element).gridTemplateColumns.split(' ').length)).toBe(columns)
      for (const value of await diagnostics.locator('.diagnostic-grid dd').all()) {
        expect(await value.evaluate(element => element.scrollWidth <= element.clientWidth)).toBe(true)
      }
      expect(await diagnostics.locator('.diagnostic-grid dd').first().evaluate(element => getComputedStyle(element).fontSize)).toBe('12px')
      await page.screenshot({path: `../.local/ota-failure/diagnostics-${theme}-${width}.png`})
    }
  }
  expect(await page.evaluate(() => (window as any).__inktagImageCommands)).toEqual(before)
})

test('mobile screen selector and diagnostic fields remain within the viewport', async ({page}) => {
  await page.setViewportSize({width: 390, height: 740})
  await page.goto('/?image-fixture=empty#/device?tab=test')
  await page.getByRole('button', {name: /^(打开浏览器记住的设备|连接其他设备)$/}).click()
  await page.getByRole('button', {name: /选择附近设备/}).click()
  await page.getByRole('button', {name: '屏幕测试', exact: true}).click()
  const box = await page.getByRole('dialog').boundingBox()
  expect(box).not.toBeNull(); expect(box!.x).toBeGreaterThanOrEqual(0); expect(box!.x + box!.width).toBeLessThanOrEqual(390)
  await page.screenshot({path: '../.local/webui-test-layout/mobile-screen.png'})
})

test('stored images have no standalone name or type editor, even on metadata-capable firmware', async ({ page }) => {
  await page.goto('/?image-fixture=1#/image')
  await page.getByRole('button', { name: /^(打开浏览器记住的设备|连接其他设备)$/ }).click()
  await page.getByRole('button', { name: /选择附近设备/ }).click()
  await page.getByRole('option', { name: /课程表/ }).click()
  const preview = page.locator('.slot-preview-pane')
  await expect(preview).toBeVisible()
  await expect(preview).toContainText('课程表')
  await expect(preview.locator('input, select, form')).toHaveCount(0)
  await expect(preview.getByRole('button', { name: '导出 PNG' })).toBeEnabled()
  await expect(preview.getByRole('button', { name: '在设备上预览' })).toBeEnabled()
  await page.locator('input[type=file]').setInputFiles(path.join(here, '../../assets/image-slots/sources/bamboo-integrity.png'))
  await page.getByRole('button', { name: '蓝牙传图', exact: true }).click()
  await page.getByRole('radio', { name: /保存到图槽/ }).check()
  await expect(page.locator('.send-image-name input')).toBeVisible()
  for (const theme of ['light', 'dark'] as const) {
    await page.emulateMedia({colorScheme: theme})
    await expect.poll(() => page.locator('html').getAttribute('data-resolved-theme')).toBe(theme)
    const input = page.locator('.send-image-name input')
    expect(await input.evaluate((element) => {
      const probe = document.createElement('span')
      probe.style.color = 'var(--ink)'
      element.parentElement!.appendChild(probe)
      const matches = getComputedStyle(element).color === getComputedStyle(probe).color
      probe.remove()
      return matches
    })).toBe(true)
    expect(await input.evaluate((element) => getComputedStyle(element).fontSize)).toBe('12px')
  }
  await page.screenshot({path: '../.local/client-ux-polish/image-name-dark.png', fullPage: true})
  expect(await page.evaluate(() => (window as unknown as { __inktagImageCommands: number[] }).__inktagImageCommands)).not.toContain(0x1e)
})

test.beforeEach(async ({ page }) => {
  await page.addInitScript({ path: path.join(here, 'virtual-bluetooth.js') })
  // These flows start after the introduction; fresh-browser onboarding has
  // its own desktop/mobile coverage in getting-started.spec.ts.
  await page.addInitScript(() => localStorage.setItem('inktag-getting-started-v1', 'seen'))
})

test('activation status labels unsupported devices without confusing an offline page', async ({page}) => {
  await page.goto('/#/device?tab=activation')
  const status = page.locator('.activation-status > div').filter({has: page.getByText('激活状态', {exact: true})}).locator('dd')
  await expect(status).toHaveText('连接设备后读取')
  await page.getByRole('button', {name: /^(打开浏览器记住的设备|连接其他设备)$/}).click()
  await page.getByRole('button', {name: /选择附近设备/}).click()
  await expect(status).toHaveText('该设备不支持激活')
  await expect(page.getByText('当前固件未提供激活协议', {exact: true})).toHaveCount(0)
  await expect(page.getByRole('button', {name: '刷新状态', exact: true})).toBeDisabled()
  await expect(page.getByRole('button', {name: '重新生成', exact: true})).toBeDisabled()
  await expect(page.getByRole('button', {name: '激活设备', exact: true})).toBeDisabled()
  const commands = await page.evaluate(() => (window as any).__inktagImageCommands as number[])
  for (const command of [0x40, 0x42, 0x43]) expect(commands).not.toContain(command)
})

for (const width of [1440, 1024, 320]) test(`activation fields keep bottom spacing and aligned actions at ${width}px`, async ({page}) => {
  await page.setViewportSize({width, height: 900})
  await page.goto('/?activation-fixture=free#/device?tab=activation')
  await page.getByRole('button', {name: /^(打开浏览器记住的设备|连接其他设备)$/}).click()
  await page.getByRole('button', {name: /选择附近设备/}).click()
  await expect(page.getByLabel('设备请求码', {exact: true})).toHaveValue('020000000000EE02:0101010101010101')
  await expect(page.getByRole('checkbox', {name: '使用已保存的原激活码恢复授权'})).toBeVisible()
  const rows = page.locator('.activation-actions')
  await expect(rows).toHaveCount(2)
  for (const row of await rows.all()) {
    const bounds = (await row.boundingBox())!
    expect(await row.evaluate((element) => parseFloat(getComputedStyle(element).paddingBottom))).toBeGreaterThanOrEqual(12)
    for (const control of await row.locator('input:not([type=checkbox]), textarea, button').all()) {
      const box = (await control.boundingBox())!
      expect(bounds.y + bounds.height - box.y - box.height).toBeGreaterThanOrEqual(12)
      expect(box.x).toBeGreaterThanOrEqual(bounds.x)
      expect(box.x + box.width).toBeLessThanOrEqual(bounds.x + bounds.width + 1)
    }
  }
  const request = (await rows.nth(0).boundingBox())!
  const code = (await rows.nth(1).boundingBox())!
  expect(code.y).toBeGreaterThanOrEqual(request.y + request.height)
  const buttons = rows.nth(0).locator('.button-row')
  expect(await buttons.evaluate((element) => parseFloat(getComputedStyle(element).gap))).toBeGreaterThanOrEqual(7)
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBeLessThanOrEqual(width)
  await page.screenshot({path: `../.local/test-results/activation-fields-${width}.png`, fullPage: true})
  const commands = await page.evaluate(() => (window as any).__inktagImageCommands as number[])
  for (const write of [0x42, 0x10, 0x11, 0x12, 0x13]) expect(commands).not.toContain(write)
})

test('device operations open password management without weakening unavailable controls', async ({page}) => {
  await page.goto('/#/device')
  await expect(page.getByRole('navigation', {name: '设备设置'}).getByRole('button', {name: '密码', exact: true})).toHaveCount(0)
  await page.locator('.device-lifecycle-actions').getByRole('button', {name: '设备密码', exact: true}).click()
  const settings = page.locator('.device-password-settings')
  await expect(settings.getByRole('heading', {name: '设备密码'})).toBeVisible()
  await expect(settings).toContainText('连接设备后可查看及设置密码')
  await expect(settings.getByRole('button', {name: '设置密码', exact: true})).toBeDisabled()
  await settings.getByRole('button', {name: '关闭', exact: true}).click()
  await expect(settings).toHaveCount(0)
  await page.goto('/?activation-fixture=free#/device')
  await page.getByRole('button', {name: /^(打开浏览器记住的设备|连接其他设备)$/}).click()
  await page.getByRole('button', {name: /选择附近设备/}).click()
  await page.getByRole('button', {name: '设备密码', exact: true}).click()
  await expect(settings).toContainText('当前固件未提供设备密码能力')
  await expect(settings.getByRole('button', {name: '设置密码', exact: true})).toBeDisabled()
  await page.goto('/?activation-fixture=free&password-fixture=off#/device')
  await page.getByRole('button', {name: /^(打开浏览器记住的设备|连接其他设备)$/}).click()
  await page.getByRole('button', {name: /选择附近设备/}).click()
  await page.getByRole('button', {name: '设备密码', exact: true}).click()
  await expect(settings).toContainText('开启设备密码需要已激活的 Pro 设备')
  await expect(settings.getByRole('button', {name: '设置密码', exact: true})).toBeDisabled()
  const commands = await page.evaluate(() => (window as any).__inktagImageCommands as number[])
  for (const write of [0x10, 0x11, 0x12, 0x13, 0x48]) expect(commands).not.toContain(write)
})

test('restoring the official QR URL keeps the initial canonical slash and does not write a device', async ({page}) => {
  await page.goto('/#/device?tab=behavior')
  const field = page.getByLabel('设备信息 URL', {exact: true})
  const initial = await field.inputValue()
  await page.getByRole('button', {name: '恢复官方地址', exact: true}).click()
  await expect(field).toHaveValue(initial)
  await expect(field).toHaveValue('https://solar2ain.github.io/ink-tag/')
  await expect(page.getByRole('button', {name: '写入并预览', exact: true})).toBeDisabled()
  await expect(page.locator('.library-message')).toContainText('尚未写入设备')
  await page.getByRole('button', {name: /^(打开浏览器记住的设备|连接其他设备)$/}).click()
  await page.getByRole('button', {name: /选择附近设备/}).click()
  const official = 'https://solar2ain.github.io/ink-tag/?ble=InkTag_GR_E2E'
  await expect(field).toHaveValue(official)
  await field.fill('https://example.com/custom')
  await page.getByRole('button', {name: '恢复官方地址', exact: true}).click()
  await expect(field).toHaveValue(official)
  const commands = await page.evaluate(() => (window as any).__inktagImageCommands as number[])
  for (const write of [0x10, 0x11, 0x12, 0x13]) expect(commands).not.toContain(write)
})

for (const remember of [true, false]) test(`password remembering follows the user's choice (${remember})`, async ({page}) => {
  const connect = async () => {
    await page.getByRole('button', {name: /^(打开浏览器记住的设备|连接其他设备)$/}).click()
    await page.getByRole('button', {name: /选择附近设备/}).click()
  }
  await page.goto('/?password-fixture=locked#/device')
  await connect()
  const prompt = page.getByRole('dialog', {name: '输入设备密码', exact: true})
  const checkbox = prompt.getByRole('checkbox', {name: '记住密码（仅此浏览器）'})
  await expect(checkbox).not.toBeChecked()
  if (remember) await checkbox.check()
  await prompt.getByLabel('密码', {exact: true}).fill('Fixture8')
  await prompt.getByRole('button', {name: '验证', exact: true}).click()
  await expect(prompt).toHaveCount(0)
  const stored = await page.evaluate(() => localStorage.getItem('inktag-device-password-v1:020000000000EE02'))
  if (remember) { expect(stored).toMatch(/^[0-9a-f]{64}$/); expect(stored).not.toContain('Fixture8') }
  else expect(stored).toBeNull()
  await page.reload()
  await connect()
  if (remember) {
    await page.getByRole('button', {name: '设备密码', exact: true}).click()
    await expect(page.getByRole('button', {name: '修改密码', exact: true})).toBeEnabled()
    await expect(prompt).toHaveCount(0)
  } else await expect(prompt).toBeVisible()
  const commands = await page.evaluate(() => (window as any).__inktagImageCommands as number[])
  for (const write of [0x10, 0x11, 0x12, 0x13]) expect(commands).not.toContain(write)
})

test('device names stay in the header and remembered list, while device details show only the MCU', async ({page}) => {
  await page.goto('/?board-fixture=hs_nowa154#/')
  await page.getByRole('button', {name: '打开浏览器记住的设备'}).click()
  await page.getByRole('button', {name: /选择附近设备/}).click()
  await expect(page.locator('.device-summary strong')).toHaveText('E2E Fixture')
  await expect(page.locator('.device-summary > div > span')).toHaveText('InkTag_GR_E2E')
  const current = page.locator('.device-overview')
  await expect(current.locator('dl > div').filter({has: page.locator('dt', {hasText: /^主控$/})})).toContainText('TLSR8359')
  await expect(current.locator('dt', {hasText: /^主板$/})).toHaveCount(0)
  await page.getByRole('button', {name: '连接其他设备', exact: true}).first().click()
  await expect(page.locator('.known-device-list')).toContainText('E2E Fixture')
  await expect(page.locator('.known-device-list')).toContainText('InkTag_GR_E2E')
  await expect(page.locator('.known-device-list strong')).toHaveText('InkTag_GR_E2E')
  await expect(page.locator('.known-device-name')).toHaveText('E2E Fixture')
  const titleBox = await page.locator('.known-device-list strong').boundingBox()
  const nameBox = await page.locator('.known-device-name').boundingBox()
  expect(nameBox!.y).toBeGreaterThanOrEqual(titleBox!.y + titleBox!.height)
  const fontSizes = await page.locator('.known-device-row').evaluate((row) => ({
    title: Number.parseFloat(getComputedStyle(row.querySelector('strong')!).fontSize),
    name: Number.parseFloat(getComputedStyle(row.querySelector('.known-device-name')!).fontSize),
  }))
  expect(fontSizes.name).toBeLessThan(fontSizes.title)
  await expect(page.locator('.known-device-list')).not.toContainText('hs_nowa154')
  await expect(page.locator('.known-device-list')).not.toContainText('tlsr8359_nowa154r')
  await page.getByRole('button', {name: '连接其他设备', exact: true}).first().click()
  await current.getByRole('link', {name: '设备设置', exact: true}).click()
  await expect(page.locator('.settings-list > div').filter({has: page.locator('dt', {hasText: /^主控$/})}).locator('dd')).toHaveText('TLSR8359')
  await expect(page.locator('.settings-list')).not.toContainText('tlsr8359_nowa154r')
})

test('connection history persists the device-reported name after disconnect and reload', async ({page}) => {
  await page.setViewportSize({width: 320, height: 900})
  await page.goto('/?remembered-fixture=1#/')
  await page.getByRole('button', {name: '选择设备', exact: true}).click()
  await expect(page.locator('.device-summary strong')).toHaveText('E2E Fixture')
  await expect.poll(async () => page.evaluate(() => new Promise<Array<{lastConnectedName?: string; localAlias: string}>>((resolve, reject) => {
    const request = indexedDB.open('inktag-webui')
    request.onerror = () => reject(request.error)
    request.onsuccess = () => {
      const database = request.result
      const records = database.transaction('devicePreferences', 'readonly').objectStore('devicePreferences').getAll()
      records.onerror = () => { database.close(); reject(records.error) }
      records.onsuccess = () => { database.close(); resolve(records.result.map(({lastConnectedName, localAlias}) => ({lastConnectedName, localAlias}))) }
    }
  }))).toContainEqual({lastConnectedName: 'InkTag_GR_E2E', localAlias: 'E2E Fixture'})
  // The compact header hides the text caption, but keeps this disconnect button.
  await page.locator('.connection-direct').click()
  await page.reload()
  await page.getByRole('button', {name: '打开浏览器记住的设备', exact: true}).click()
  const history = page.locator('.known-device-list')
  await expect(history.locator('strong')).toHaveText('InkTag_GR_E2E')
  await expect(history.locator('.known-device-name')).toHaveText('E2E Fixture')
  await history.getByRole('button', {name: /InkTag_GR_E2E.*E2E Fixture/}).click()
  await expect(page.locator('.device-summary strong')).toHaveText('E2E Fixture')
})

for (const {width, theme} of [{width: 1440, theme: 'light'}, {width: 320, theme: 'light'}, {width: 320, theme: 'dark'}]) {
  for (const mode of ['off', 'locked']) test(`password dialogs have consistent fields and spacing (${mode}, ${width}px, ${theme})`, async ({page}) => {
    await page.setViewportSize({width, height: 900})
    await page.goto(`/?password-fixture=${mode}#/device`)
    await page.evaluate((theme) => document.documentElement.setAttribute('data-resolved-theme', theme), theme)
    await page.getByRole('button', {name: /^(打开浏览器记住的设备|连接其他设备)$/}).click()
    await page.getByRole('button', {name: /选择附近设备/}).click()
    const checkLayout = async (name: string, fields: number) => {
      const dialog = page.getByRole('dialog', {name, exact: true})
      await expect(dialog).toBeVisible()
      const inputs = dialog.locator('input[type=password]')
      await expect(inputs).toHaveCount(fields)
      const form = dialog.locator('form')
      expect(await form.evaluate((element) => parseFloat(getComputedStyle(element).rowGap))).toBeGreaterThanOrEqual(12)
      expect(await form.evaluate((element) => parseFloat(getComputedStyle(element).marginTop))).toBeGreaterThanOrEqual(14)
      for (let index = 0; index < fields; index++) {
        const input = inputs.nth(index)
        expect(await input.evaluate((element) => getComputedStyle(element).fontSize)).toBe('13px')
        expect(await input.evaluate((element) => getComputedStyle(element).fontFamily)).toBe(await dialog.evaluate((element) => getComputedStyle(element).fontFamily))
        const label = input.locator('..')
        expect(await label.evaluate((element) => getComputedStyle(element).display)).toBe('flex')
        expect(await label.evaluate((element) => parseFloat(getComputedStyle(element).rowGap))).toBeGreaterThanOrEqual(6)
        if (index > 0) {
          const previous = (await inputs.nth(index - 1).boundingBox())!
          const current = (await label.boundingBox())!
          expect(current.y - previous.y - previous.height).toBeGreaterThanOrEqual(12)
        }
      }
      const footer = dialog.locator('footer')
      expect(await footer.evaluate((element) => parseFloat(getComputedStyle(element).gap))).toBeGreaterThanOrEqual(7)
      const bounds = (await dialog.boundingBox())!
      expect(bounds.x).toBeGreaterThanOrEqual(0)
      expect(bounds.x + bounds.width).toBeLessThanOrEqual(width)
      for (const button of await dialog.locator('button').all()) {
        const buttonBounds = (await button.boundingBox())!
        expect(buttonBounds.x).toBeGreaterThanOrEqual(bounds.x)
        expect(buttonBounds.x + buttonBounds.width).toBeLessThanOrEqual(bounds.x + bounds.width)
      }
      await page.screenshot({path: `../.local/test-results/password-${name}-${width}-${theme}.png`})
    }
    if (mode === 'locked') {
      await checkLayout('输入设备密码', 1)
      await page.getByLabel('密码', {exact: true}).fill('Fixture8')
      await page.getByRole('button', {name: '验证', exact: true}).click()
      await expect(page.getByRole('dialog', {name: '输入设备密码'})).toHaveCount(0)
    }
    await page.getByRole('button', {name: '设备密码', exact: true}).click()
    const settings = page.locator('.device-password-settings')
    await expect(settings.getByRole('heading', {name: '设备密码'})).toBeVisible()
    await settings.getByRole('button', {name: mode === 'locked' ? '修改密码' : '设置密码', exact: true}).click()
    const editTitle = mode === 'locked' ? '修改设备密码' : '设置设备密码'
    await checkLayout(editTitle, mode === 'locked' ? 3 : 2)
    await page.getByRole('dialog', {name: editTitle}).getByRole('button', {name: '取消', exact: true}).click()
    if (mode === 'locked') {
      await settings.getByRole('button', {name: '删除密码', exact: true}).click()
      await checkLayout('删除设备密码', 1)
      const original = settings.getByLabel('原密码', {exact: true})
      await expect(original).toHaveAttribute('required', '')
      await settings.getByRole('button', {name: '确认', exact: true}).click()
      await expect(settings.getByRole('heading', {name: '删除设备密码'})).toBeVisible()
      await page.getByRole('dialog', {name: '删除设备密码'}).getByRole('button', {name: '取消', exact: true}).click()
    }
    expect(await settings.locator('.button-row').evaluate((element) => parseFloat(getComputedStyle(element).gap))).toBeGreaterThanOrEqual(7)
    const commands = await page.evaluate(() => (window as any).__inktagImageCommands as number[])
    for (const write of [0x10, 0x11, 0x12, 0x13]) expect(commands).not.toContain(write)
    await page.keyboard.press('Escape')
    await expect(settings).toHaveCount(0)
  })
}

test('send stays disabled from decoded image until its first processed frame', async ({ page }) => {
  await page.goto('/?image-fixture=1#/image')
  await page.getByRole('button', { name: /^(打开浏览器记住的设备|连接其他设备)$/ }).click()
  await page.getByRole('button', { name: /选择附近设备/ }).click()
  await expect(page.locator('.slot-preview-pane')).toBeVisible()
  await page.evaluate(() => {
    const request = window.requestAnimationFrame.bind(window)
    const cancel = window.cancelAnimationFrame.bind(window)
    const pending = new Map<number, FrameRequestCallback>()
    let id = -1
    window.requestAnimationFrame = (callback) => { pending.set(id, callback); return id-- }
    window.cancelAnimationFrame = (token) => { if (!pending.delete(token)) cancel(token) }
    ;(window as unknown as { __releaseImageFrame: () => void }).__releaseImageFrame = () => {
      window.requestAnimationFrame = request
      window.cancelAnimationFrame = cancel
      for (const callback of pending.values()) request(callback)
      pending.clear()
    }
  })
  await page.locator('input[type=file]').setInputFiles(path.join(here, '../../assets/image-slots/sources/bamboo-integrity.png'))
  const previewHeading = page.locator('.image-preview-heading')
  await expect(previewHeading.getByText('bamboo-integrity.png', { exact: true })).toBeVisible()
  await expect(previewHeading.getByText('400 × 300', { exact: true })).toBeVisible()
  const send = page.getByRole('button', { name: '蓝牙传图', exact: true })
  await expect(send).toBeDisabled()
  await page.evaluate(() => (window as unknown as { __releaseImageFrame: () => void }).__releaseImageFrame())
  await expect(send).toBeEnabled()
  await send.click()
  await expect(page.getByRole('dialog', { name: '发送图片' })).toBeVisible()
  expect(await page.evaluate(() => (window as unknown as { __inktagImageCommands: number[] }).__inktagImageCommands)).not.toContain(0x10)
})

for (const width of [1440, 320]) test(`upload targets show every sparse slot and keep the filename at ${width}px`, async ({ page }) => {
  await page.setViewportSize({width, height: 900})
  await page.goto('/?image-fixture=1#/image')
  await page.getByRole('button', { name: /^(打开浏览器记住的设备|连接其他设备)$/ }).click()
  await page.getByRole('button', { name: /选择附近设备/ }).click()
  await page.locator('input[type=file]').setInputFiles(path.join(here, '../../assets/image-slots/sources/bamboo-integrity.png'))
  await page.getByRole('button', { name: '蓝牙传图', exact: true }).click()
  await page.getByRole('radio', { name: /保存到图槽/ }).check()
  const picker = page.getByRole('listbox', {name: '选择保存图槽'})
  await expect(picker.getByRole('option')).toHaveCount(15)
  const empty = picker.getByRole('option', {name: '槽 1，空槽', exact: true})
  const used = picker.getByRole('option', {name: '槽 8，已使用，课程表', exact: true})
  await expect(empty).toBeEnabled()
  await expect(used).toBeEnabled()
  await expect(picker.getByRole('option', {name: '槽 15，空槽', exact: true})).toBeVisible()
  const input = page.locator('.send-image-name input')
  await expect(input).toHaveValue('bamboo-integrity.png')
  await used.click()
  await expect(page.locator('.image-slot-target-section')).toContainText('将替换槽 8')
  await expect(input).toHaveValue('bamboo-integrity.png')
  await input.fill('用户名称')
  await empty.click()
  await expect(input).toHaveValue('用户名称')
  expect(await empty.evaluate((element) => getComputedStyle(element).backgroundColor)).not.toBe(
    await used.evaluate((element) => getComputedStyle(element).backgroundColor))
  const firstBounds = (await empty.boundingBox())!
  const secondBounds = (await picker.getByRole('option').nth(1).boundingBox())!
  expect(secondBounds.x).toBeGreaterThan(firstBounds.x + firstBounds.width)
  expect(Math.abs(secondBounds.y - firstBounds.y)).toBeLessThan(2)
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBeLessThanOrEqual(width)
  await page.screenshot({path: `../.local/test-results/upload-slot-picker-${width}.png`})
  expect(await page.evaluate(() => (window as any).__inktagImageCommands)).not.toContain(0x10)
})

for (const count of [15, 255]) test(`fixed-slot firmware permits a far empty target in a ${count}-slot store`, async ({page}) => {
  await page.setViewportSize({width: 320, height: 900})
  await page.goto(`/?image-fixture=1&fixed-slots=${count}#/image`)
  await page.getByRole('button', {name: /^(打开浏览器记住的设备|连接其他设备)$/}).click()
  await page.getByRole('button', {name: /选择附近设备/}).click()
  await page.locator('input[type=file]').setInputFiles(path.join(here, '../../assets/image-slots/sources/bamboo-integrity.png'))
  await page.getByRole('button', {name: '蓝牙传图', exact: true}).click()
  await page.getByRole('radio', {name: /保存到图槽/}).check()
  const picker = page.getByRole('listbox', {name: '选择保存图槽'})
  await expect(picker.getByRole('option')).toHaveCount(count)
  const last = picker.getByRole('option', {name: `槽 ${count}，空槽`, exact: true})
  await expect(last).toBeEnabled()
  await last.click()
  await expect(last).toHaveAttribute('aria-selected', 'true')
  await expect(page.locator('.image-slot-target-section')).toContainText(`将保存到槽 ${count}（空槽）`)
  await expect(page.locator('.send-image-name input')).toHaveValue('bamboo-integrity.png')
  expect(await page.evaluate(() => (window as any).__inktagImageCommands)).not.toContain(0x10)
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBeLessThanOrEqual(320)
})

for (const viewport of [{ width: 1440, height: 900 }, { width: 390, height: 844 }, { width: 320, height: 568 }]) {
  test(`stored image readback keeps preview and controls proportional at ${viewport.width}px`, async ({ page }) => {
    await page.setViewportSize(viewport)
    await page.goto('/?image-fixture=1#/image')
    await page.getByRole('button', { name: /^(打开浏览器记住的设备|连接其他设备)$/ }).click()
    await page.getByRole('button', { name: /选择附近设备/ }).click()
    const preview = page.locator('.slot-preview-pane')
    await expect(preview).toBeVisible()
    const media = preview.locator('.slot-preview-media')
    const actions = preview.locator('.slot-preview-actions')
    const buttons = actions.getByRole('button')
    await expect(buttons).toHaveCount(4)
    expect(await page.evaluate(() => (window as any).__inktagImageCommands)).not.toContain(0x1a)
    for (const populated of [false, true]) {
      if (populated) {
        await preview.getByRole('button', { name: '回读预览', exact: true }).click()
        await expect(media.getByRole('img')).toBeVisible()
      }
      await actions.scrollIntoViewIfNeeded()
      await page.evaluate(() => window.scrollTo(0, document.documentElement.scrollHeight))
      const mediaBounds = (await media.boundingBox())!
      const actionBounds = (await actions.boundingBox())!
      expect(mediaBounds.height).toBeGreaterThanOrEqual(160)
      expect(mediaBounds.height).toBeLessThanOrEqual(390)
      expect(actionBounds.y).toBeGreaterThanOrEqual(mediaBounds.y + mediaBounds.height + 8)
      expect(actionBounds.height).toBeLessThanOrEqual(92)
      const navigation = await page.locator('.bottom-nav').boundingBox()
      if (navigation) expect(actionBounds.y + actionBounds.height).toBeLessThanOrEqual(navigation.y)
      for (const button of await buttons.all()) {
        const bounds = (await button.boundingBox())!
        expect(bounds.height).toBe(viewport.width < 600 ? 42 : 38)
        expect(bounds.x).toBeGreaterThanOrEqual(0)
        expect(bounds.x + bounds.width).toBeLessThanOrEqual(viewport.width)
        expect(await button.evaluate((element) => element.scrollWidth <= element.clientWidth)).toBe(true)
      }
      if (viewport.width < 600) {
        const bounds = await Promise.all((await buttons.all()).map((button) => button.boundingBox()))
        expect(bounds[0]!.width).toBe(bounds[1]!.width)
        expect(bounds[2]!.width).toBe(bounds[3]!.width)
        expect(bounds[0]!.y).toBe(bounds[1]!.y)
        expect(bounds[2]!.y).toBeGreaterThan(bounds[0]!.y)
      }
      await page.screenshot({ path: `../.local/test-results/image-readback-${viewport.width}-${populated ? 'populated' : 'empty'}.png` })
    }
    const commands = await page.evaluate(() => (window as any).__inktagImageCommands as number[])
    expect(commands).not.toContain(0x0a) // Layout tests never refresh a physical display.
    expect(commands).not.toContain(0x1b)
    expect(commands).not.toContain(0x1e)
  })
}

test('production bundle connects to a virtual V2 Bluetooth device', async ({ page }) => {
  await page.goto('/')
  await expect(page.getByRole('heading', { name: '工作台' })).toBeVisible()

  await page.getByRole('button', { name: /^(打开浏览器记住的设备|连接其他设备)$/ }).click()
  await page.getByRole('button', { name: /选择附近设备/ }).click()

  await expect(page.locator('.device-summary strong')).toHaveText('E2E Fixture')
  await expect(page.locator('.device-summary > div > span')).toHaveText('InkTag_GR_E2E')
  await expect(page.locator('.device-summary')).not.toContainText('gr551x_zkc42vm')
  await expect(page.locator('.device-metrics').getByText('3.01 V', { exact: true })).toBeVisible()
})

test('remembered device reconnects after reload and a delayed advertisement without another chooser', async ({ page }) => {
  await page.goto('/?remembered-fixture=1')
  await page.getByRole('button', { name: '打开浏览器记住的设备', exact: true }).click()
  await page.getByRole('button', { name: '选择附近设备', exact: true }).click()
  await expect(page.locator('.device-summary strong')).toHaveText('E2E Fixture')
  await page.reload()
  await page.getByRole('button', { name: '打开浏览器记住的设备', exact: true }).click()
  await page.locator('.known-device-row button[title="直接连接"]').click()
  await expect(page.locator('.device-summary strong')).toHaveText('E2E Fixture')
  expect(await page.evaluate(() => (window as any).__bluetoothFixture)).toEqual({
    chooserCalls: 0, connectCalls: 2, watchCalls: 1, watchStopped: true,
  })
})

async function connectRecovery(page: import('@playwright/test').Page, fixture = 'gr-recovery') {
  await page.goto(`/?fixture=${fixture}`)
  await page.getByRole('button', { name: /^(打开浏览器记住的设备|连接其他设备)$/ }).click()
  await page.getByRole('button', { name: /选择附近设备/ }).click()
}

function nativeNordicBoundaryPackage() {
  // Stored ZIP exercises the shipping parser. Synthetic bytes are never signed/installed.
  const entries = [
    ['manifest.json', Buffer.from(JSON.stringify({manifest: {application: {dat_file: 'app.dat', bin_file: 'app.bin'}}}))],
    ['app.dat', Buffer.from([1, 2, 3, 4])], ['app.bin', Buffer.from([5, 6, 7, 8])],
  ] as const
  const locals: Buffer[] = [], central: Buffer[] = []
  let offset = 0
  for (const [name, data] of entries) {
    const nameBytes = Buffer.from(name)
    let crc = 0xffffffff
    for (const byte of data) {
      crc ^= byte
      for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0)
    }
    const local = Buffer.alloc(30), directory = Buffer.alloc(46)
    local.writeUInt32LE(0x04034b50, 0); local.writeUInt16LE(20, 4)
    local.writeUInt32LE((crc ^ 0xffffffff) >>> 0, 14)
    local.writeUInt32LE(data.length, 18); local.writeUInt32LE(data.length, 22)
    local.writeUInt16LE(nameBytes.length, 26)
    directory.writeUInt32LE(0x02014b50, 0); directory.writeUInt16LE(20, 4); directory.writeUInt16LE(20, 6)
    directory.writeUInt32LE((crc ^ 0xffffffff) >>> 0, 16)
    directory.writeUInt32LE(data.length, 20); directory.writeUInt32LE(data.length, 24)
    directory.writeUInt16LE(nameBytes.length, 28); directory.writeUInt32LE(offset, 42)
    locals.push(local, nameBytes, data); central.push(directory, nameBytes)
    offset += local.length + nameBytes.length + data.length
  }
  const directory = Buffer.concat(central), end = Buffer.alloc(22)
  end.writeUInt32LE(0x06054b50, 0); end.writeUInt16LE(entries.length, 8); end.writeUInt16LE(entries.length, 10)
  end.writeUInt32LE(directory.length, 12); end.writeUInt32LE(offset, 16)
  return {name: 'native-boundary.zip', mimeType: 'application/zip', buffer: Buffer.concat([...locals, directory, end])}
}

for (const fixture of ['nrf-app-same-mac', 'nrf-app-same-mac-missing-packet']) test(`Nordic same-MAC OTA uses the verified grant, not cached App name: ${fixture}`, async ({page}) => {
  await page.goto(`/?fixture=${fixture}#/device?tab=ota`)
  await page.getByRole('button', {name: /^(打开浏览器记住的设备|连接其他设备)$/}).click()
  await page.getByRole('button', {name: /选择附近设备/}).click()
  await page.locator('.drop-zone input[type=file]').setInputFiles(nativeNordicBoundaryPackage())
  await expect(page.locator('.drop-zone')).toContainText('升级包已通过校验')
  await page.getByRole('button', {name: '开始 OTA', exact: true}).click()
  const dropZone = page.locator('.drop-zone')
  const failure = page.getByRole('alert')
  await expect(failure).toContainText(fixture.endsWith('missing-packet') ? 'missing Secure DFU packet' : 'Secure DFU 0x2 失败', {timeout: 15000})
  await expect(failure).not.toContainText('广播名无效')
  await expect(dropZone.locator('strong')).toHaveText('OTA 更新失败')
  await expect(dropZone).not.toHaveClass(/ready/)
  await expect(dropZone).toContainText('升级包已通过校验')
  const calls = await page.evaluate(() => ({commands: (window as any).__inktagImageCommands as number[], recovery: (window as any).__recoveryCommands as {bytes: number[]}[], bluetooth: (window as any).__bluetoothFixture}))
  expect(calls.commands.filter(command => command === 0x0e)).toHaveLength(1)
  expect(calls.recovery).toEqual(fixture.endsWith('missing-packet') ? [] : [{bytes: [2, 4, 0]}])
  expect(calls.bluetooth.chooserCalls).toBe(fixture.endsWith('missing-packet') ? 2 : 1)
})

test('supported empty images explain the disabled slideshow without a command', async ({page}) => {
  await page.goto('/?image-fixture=empty#/device?tab=test')
  await page.getByRole('button', {name: /^(打开浏览器记住的设备|连接其他设备)$/}).click()
  await page.getByRole('button', {name: /选择附近设备/}).click()
  const card = page.getByRole('button', {name: /轮播测试/})
  await expect(card).toBeDisabled()
  await expect(card.locator('.test-card-hint')).toHaveText('请先上传当前屏幕可播放的图片。')
  await expect(card).not.toContainText('未上报此能力')
})

test('formal Recovery entry cancels without a write, then proves the same cached grant and guarded return', async ({page}) => {
  await page.goto('/?fixture=nrf-app-same-mac-maintenance#/device?tab=ota')
  await page.getByRole('button', {name: /^(打开浏览器记住的设备|连接其他设备)$/}).click()
  await page.getByRole('button', {name: /选择附近设备/}).click()
  await page.getByRole('button', {name: '进入 Recovery', exact: true}).click()
  await page.getByRole('dialog').getByRole('button', {name: '取消', exact: true}).click()
  expect(await page.evaluate(() => (window as any).__inktagImageCommands.filter((op: number) => op === 0x0e))).toEqual([])
  await page.getByRole('button', {name: '进入 Recovery', exact: true}).click()
  await page.getByRole('button', {name: '确认进入', exact: true}).click()
  await expect(page.locator('.recovery-notice')).toContainText('是否允许退出由设备检查决定')
  await expect(page.locator('.recovery-notice')).not.toContainText('未提供安全退出协议')
  // Dismiss the global choice without writing; use the formal OTA-page exit.
  await page.getByRole('dialog').getByRole('button', {name: '进入 OTA 页面'}).click()
  await page.locator('.recovery-notice').getByRole('button', {name: '退出 Recovery'}).click()
  await expect(page.locator('.recovery-notice')).toHaveCount(0)
  await expect(page.getByRole('button', {name: '进入 Recovery', exact: true})).toBeEnabled()
  const calls = await page.evaluate(() => ({ app: (window as any).__inktagImageCommands,
    native: (window as any).__recoveryCommands, bluetooth: (window as any).__bluetoothFixture }))
  expect(calls.app.filter((op: number) => op === 0x0e)).toHaveLength(1)
  expect(calls.native.filter(({bytes}: {bytes: number[]}) => bytes[0] === 12)).toEqual([{bytes: [12]}])
  expect(calls.native.every(({bytes}: {bytes: number[]}) => [0, 12].includes(bytes[0]))).toBe(true)
  expect(calls.bluetooth.chooserCalls).toBe(1)
})

for (const result of ['match', 'size-mismatch', 'crc-mismatch', 'lost-final-match', 'lost-final-crc-mismatch']) test(`Nordic running-image attestation gates completion and sync: ${result}`, async ({page}) => {
  if (result.startsWith('lost-final')) await page.clock.install()
  await page.goto(`/?fixture=nrf-app-same-mac-attest-${result}#/device?tab=ota`)
  await page.getByRole('button', {name: /^(打开浏览器记住的设备|连接其他设备)$/}).click()
  await page.getByRole('button', {name: /选择附近设备/}).click()
  await page.locator('.drop-zone input[type=file]').setInputFiles(nativeNordicBoundaryPackage())
  await expect(page.locator('.drop-zone')).toContainText('升级包已通过校验')
  await page.getByRole('button', {name: '开始 OTA', exact: true}).click()
  if (result.startsWith('lost-final')) {
    await expect.poll(() => page.evaluate(() => (window as any).__recoveryCommands.filter(({bytes}: {bytes:number[]}) => bytes[0] === 4).length)).toBe(2)
    // Only the synthetic peer's missing native response uses virtual time.
    // Shipping 30 s budget remains unchanged; never resend the final EXECUTE.
    await page.clock.fastForward(31000)
    // The progress bar contains only the percentage. The phase text may have
    // already advanced to attestation; assert the real terminal state below.
    await page.clock.fastForward(10000)
  }
  if (result === 'match' || result === 'lost-final-match') {
    await expect(page.locator('.drop-zone')).toContainText('OTA 完成，应用已验身、重连并校时', {timeout: 15000})
    await expect(page.locator('.drop-zone strong')).toHaveText('OTA 已完成')
    await expect(page.getByRole('alert')).toHaveCount(0)
  } else {
    await expect(page.getByRole('alert')).toContainText('nRF 应用核验不一致', {timeout: 15000})
    await expect(page.locator('.drop-zone strong')).toHaveText('OTA 更新失败')
    await expect(page.locator('.drop-zone')).not.toHaveClass(/ready/)
    await expect(page.locator('.drop-zone')).not.toContainText('验身完成')
    await expect(page.locator('.ota-progress')).toHaveClass(/failed/)
  }
  const reads = await page.evaluate(() => (window as any).__postNativeImageCommands as number[])
  expect(reads.filter(op => op === 3).length).toBeGreaterThanOrEqual(2)
  expect(reads.filter(op => op === 6)).toHaveLength(result === 'match' || result === 'lost-final-match' ? 1 : 0)
  if (result.startsWith('lost-final')) {
    const writes = await page.evaluate(() => (window as any).__recoveryCommands.filter(({bytes}: {bytes:number[]}) => bytes[0] === 4))
    expect(writes).toHaveLength(2) // signed INIT and one final DATA EXECUTE only
  }
})

for (const theme of ['light', 'dark']) for (const width of [1280, 390]) test(`OTA failure remains prominent at 100 percent: ${theme} ${width}`, async ({page}) => {
  await page.setViewportSize({width, height: 820})
  await page.goto('/?fixture=nrf-app-same-mac-attest-crc-mismatch#/device?tab=ota')
  await page.evaluate((value) => document.documentElement.setAttribute('data-resolved-theme', value), theme)
  await page.getByRole('button', {name: /^(打开浏览器记住的设备|连接其他设备)$/}).click()
  await page.getByRole('button', {name: /选择附近设备/}).click()
  await page.locator('.drop-zone input[type=file]').setInputFiles(nativeNordicBoundaryPackage())
  await page.getByRole('button', {name: '开始 OTA', exact: true}).click()
  const failure = page.getByRole('alert')
  await expect(failure).toBeVisible({timeout: 15000})
  await expect(failure).toContainText('OTA 更新失败')
  await expect(failure).toContainText('nRF 应用核验不一致')
  await expect(failure).toContainText('不要盲目重试')
  await expect(page.locator('.ota-progress')).toContainText('100%')
  await expect(page.locator('.drop-zone .lucide-circle-check-big')).toHaveCount(0)
  const box = (await failure.boundingBox())!
  expect(box.x).toBeGreaterThanOrEqual(0)
  expect(box.x + box.width).toBeLessThanOrEqual(width)
  expect(await failure.evaluate(el => el.scrollWidth <= el.clientWidth)).toBe(true)
  const colors = await failure.evaluate(el => ({color:getComputedStyle(el).color,border:getComputedStyle(el).borderColor,font:getComputedStyle(el.querySelector('strong')!).fontSize}))
  expect(colors.color).toBe(theme === 'dark' ? 'rgb(255, 180, 171)' : 'rgb(152, 40, 32)')
  expect(colors.border).toBe(theme === 'dark' ? 'rgb(225, 107, 97)' : 'rgb(191, 64, 57)')
  expect(colors.font).toBe('16px')
  const commands = await page.evaluate(() => (window as any).__recoveryCommands as {bytes: number[]}[])
  expect(commands.filter(({bytes}) => bytes[0] === 4)).toHaveLength(2) // signed INIT and DATA, each once
  await page.screenshot({path: `../.local/ota-failure/${theme}-${width}.png`, fullPage:true})
})

for (const fixture of ['om-recovery', 'da-recovery', 'nrf-recovery']) test(`all MCU families show verified Recovery choices: ${fixture}`, async ({ page }) => {
  await connectRecovery(page, fixture)
  const dialog = page.getByRole('dialog', { name: '设备处于 Recovery' })
  await expect(dialog).toBeVisible()
  await expect(dialog.getByRole('button', { name: '退出 Recovery' })).toHaveCount(fixture === 'om-recovery' ? 1 : 0)
  if (fixture !== 'om-recovery') await expect(dialog).toContainText('需完成固件更新')
  const capabilityReads = fixture === 'nrf-recovery' ? [{ bytes: [0] }] : []
  expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual(capabilityReads)
  await dialog.getByRole('button', { name: '进入 OTA 页面' }).click()
  await expect(page.locator('.recovery-notice')).toBeVisible()
  await expect(page.getByRole('button', { name: '退出 Recovery' })).toHaveCount(fixture === 'om-recovery' ? 1 : 0)
  expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual(capabilityReads)
})
test('OM exits with one existing guarded command and verifies an App return', async ({ page }) => {
  await connectRecovery(page, 'om-recovery')
  await page.getByRole('dialog').getByRole('button', { name: '退出 Recovery' }).click()
  await expect(page.locator('.device-summary')).toContainText('E2E Fixture')
  await expect(page.getByRole('dialog', { name: '设备处于 Recovery' })).toBeHidden()
  expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([{ bytes: [7] }])
})
test('DA capability proof offers safe exit and verifies App after a single FC and disconnect', async ({page}) => {
  await connectRecovery(page, 'da-recovery-safe')
  const dialog = page.getByRole('dialog', {name:'设备处于 Recovery'})
  await expect(dialog.getByRole('button', {name:'退出 Recovery'})).toBeEnabled()
  expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([])
  await dialog.getByRole('button', {name:'退出 Recovery'}).click()
  await expect(page.locator('.device-summary')).toContainText('E2E Fixture')
  await expect(dialog).toBeHidden()
  expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([{bytes:[0,0,0,252]}])
})
for (const fixture of ['da-recovery-safe-reject', 'da-recovery-safe-unknown']) test(`DA safe exit keeps refusal/unknown and never replays: ${fixture}`, async ({page}) => {
  await connectRecovery(page, fixture)
  const dialog = page.getByRole('dialog', {name:'设备处于 Recovery'})
  await dialog.getByRole('button', {name:'退出 Recovery'}).click()
  await expect(dialog.getByRole('alert')).toContainText(fixture.endsWith('unknown') ? '结果未知' : 'status 25')
  await expect(dialog).toBeVisible()
  expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([{bytes:[0,0,0,252]}])
  if (fixture.endsWith('unknown')) {
    await expect(dialog.getByRole('button', {name:'退出 Recovery'})).toBeDisabled()
    await dialog.getByRole('button', {name:'进入 OTA 页面'}).click()
    await page.getByRole('button', {name:'退出 Recovery'}).click()
    await expect(page.getByRole('alert')).toContainText('结果未知')
    expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([{bytes:[0,0,0,252]}])
  }
})
test('Nordic guarded exit queries capability and confirms same-device App after one ABORT', async ({page}) => {
  await connectRecovery(page, 'nrf-recovery-safe')
  await page.getByRole('dialog', {name:'设备处于 Recovery'}).getByRole('button', {name:'退出 Recovery'}).click()
  await expect(page.locator('.device-summary strong')).toHaveText('E2E Fixture')
  expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([{bytes:[0]},{bytes:[0]},{bytes:[12]}])
})
for (const fixture of ['nrf-recovery-safe-reject', 'nrf-recovery-safe-unknown']) test(`Nordic exit never retries refusal/unknown: ${fixture}`, async ({page}) => {
  await connectRecovery(page, fixture)
  const dialog = page.getByRole('dialog', {name:'设备处于 Recovery'})
  await dialog.getByRole('button', {name:'退出 Recovery'}).click()
  await expect(dialog.getByRole('alert')).toContainText(fixture.endsWith('unknown') ? '结果未知' : 'not permitted')
  expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([{bytes:[0]},{bytes:[0]},{bytes:[12]}])
  if (fixture.endsWith('unknown')) {
    await dialog.getByRole('button', {name:'进入 OTA 页面'}).click()
    await page.getByRole('button', {name:'退出 Recovery'}).click()
    await expect(page.getByRole('alert')).toContainText('结果未知')
    expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([{bytes:[0]},{bytes:[0]},{bytes:[12]}])
  }
})
for (const fixture of ['om-recovery-reject', 'om-recovery-unknown']) test(`OM exit preserves refusal/unknown result: ${fixture}`, async ({ page }) => {
  await connectRecovery(page, fixture)
  const dialog = page.getByRole('dialog', { name: '设备处于 Recovery' })
  await dialog.getByRole('button', { name: '退出 Recovery' }).click()
  await expect(dialog.getByRole('alert')).toContainText(fixture.endsWith('unknown') ? '结果未知' : 'VOLTAGE_BELOW_MIN')
  if (fixture.endsWith('unknown')) await expect(dialog.getByRole('button', { name: '退出 Recovery' })).toBeDisabled()
  expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([{ bytes: [7] }])
  if (fixture.endsWith('unknown')) {
    await dialog.getByRole('button', { name: '进入 OTA 页面' }).click()
    await page.getByRole('button', { name: '退出 Recovery' }).click()
    await expect(page.getByRole('alert')).toContainText('结果未知')
    expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([{ bytes: [7] }])
  }
})
for (const fixture of ['om-recovery-wrong-role', 'nrf-recovery-wrong-role']) test(`unverified Recovery role cannot expose maintenance: ${fixture}`, async ({ page }) => {
  await connectRecovery(page, fixture)
  await expect(page.getByRole('dialog', { name: '设备处于 Recovery' })).toBeHidden()
  await expect(page.locator('.device-summary')).not.toContainText('Recovery')
  expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([])
})

for (const fixture of ['tl-recovery', 'tl-recovery-low-power']) test(`TLSR Recovery connects and offers maintenance without writing: ${fixture}`, async ({ page }) => {
  await connectRecovery(page, fixture)
  const dialog = page.getByRole('dialog', { name: '设备处于 Recovery' })
  await expect(dialog).toContainText('IKT_TL_EE02_OTA')
  await expect(dialog.getByRole('button', { name: '退出 Recovery' })).toBeEnabled()
  expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([])
  await dialog.getByRole('button', { name: '进入 OTA 页面' }).click()
  await expect(page.getByRole('button', { name: '退出 Recovery', exact: true })).toBeVisible()
  expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([])
})

test('TLSR safe resume sends only 03FF and verifies App return', async ({ page }) => {
  await connectRecovery(page, 'tl-recovery')
  await page.getByRole('dialog', { name: '设备处于 Recovery' }).getByRole('button', { name: '退出 Recovery' }).click()
  await expect(page.locator('.device-summary strong')).toHaveText('E2E Fixture')
  expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([{ bytes: [3, 255] }])
})

for (const [fixture, message] of [['tl-recovery-low-power', 'LOW_POWER'], ['tl-recovery-partial', 'UNSAFE_RESUME'], ['tl-recovery-unknown', '结果未知']]) test(`TLSR safe exit preserves refusal and never replays: ${fixture}`, async ({ page }) => {
  await connectRecovery(page, fixture)
  const dialog = page.getByRole('dialog', { name: '设备处于 Recovery' })
  await dialog.getByRole('button', { name: '退出 Recovery' }).click()
  await expect(dialog.getByRole('alert')).toContainText(message)
  await expect(dialog).toBeVisible()
  if (fixture === 'tl-recovery-unknown') await expect(dialog.getByRole('button', { name: '退出 Recovery' })).toBeDisabled()
  expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([{ bytes: [3, 255] }])
})

test('TLSR name/service without the fixed Recovery status is rejected without writes', async ({ page }) => {
  await connectRecovery(page, 'tl-recovery-wrong-role')
  await expect(page.getByRole('dialog', { name: '设备处于 Recovery' })).toBeHidden()
  await page.getByRole('button', { name: /打开设备连接日志/ }).click()
  await expect(page.locator('.log-popover')).toContainText('Recovery 状态/版本无效')
  expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([])
})

test('GR Recovery modal navigates to OTA without starting a firmware write', async ({ page }) => {
  await page.setViewportSize({ width: 320, height: 568 })
  await connectRecovery(page)
  const dialog = page.getByRole('dialog', { name: '设备处于 Recovery' })
  await expect(dialog).toBeVisible()
  await expect(dialog.getByRole('button')).toHaveCount(2)
  await expect(dialog).toContainText('IKT_GR_EE02_OTA')
  await expect(page.locator('.device-metrics .recovery-mode')).toHaveText('Recovery')
  await expect(page.locator('.device-summary')).toContainText('Recovery 模式')
  await expect(page.locator('.device-metrics')).not.toContainText('免费版')
  const bounds = await dialog.boundingBox()
  expect(bounds!.x).toBeGreaterThanOrEqual(0)
  expect(bounds!.x + bounds!.width).toBeLessThanOrEqual(320)
  await page.screenshot({ path: '../.local/test-results/gr-recovery-mobile.png' })
  await dialog.getByRole('button', { name: '进入 OTA 页面' }).click()
  await expect(page).toHaveURL(/#\/device\?tab=ota$/)
  await expect(dialog).toBeHidden()
  await expect(page.getByRole('button', { name: '退出 Recovery', exact: true })).toBeVisible()
  await expect(page.getByRole('region', { name: '设备字体' })).toHaveCount(0)
  expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([
    { opcode: 1, payload: [] }, { opcode: 0x42, payload: [] },
  ])
})

test('GR Recovery safe exit returns to the App without flash or reset commands', async ({ page }) => {
  await connectRecovery(page)
  const dialog = page.getByRole('dialog', { name: '设备处于 Recovery' })
  await dialog.getByRole('button', { name: '退出 Recovery' }).click()
  await expect(page.locator('.device-summary strong')).toHaveText('E2E Fixture')
  await expect(dialog).toBeHidden()
  await expect(page.locator('.device-metrics .recovery-mode')).toHaveCount(0)
  expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([
    { opcode: 1, payload: [] }, { opcode: 0x42, payload: [] },
    { opcode: 1, payload: [] }, { opcode: 0x42, payload: [] },
    { opcode: 0x41, payload: [] },
  ])
})

test('GR firmware page safe exit uses the same guarded resume as the modal', async ({ page }) => {
  await connectRecovery(page)
  await page.getByRole('dialog', { name: '设备处于 Recovery' }).getByRole('button', { name: '进入 OTA 页面' }).click()
  await page.getByRole('button', { name: '退出 Recovery', exact: true }).click()
  await expect(page.locator('.device-summary strong')).toHaveText('E2E Fixture')
  await expect(page.getByRole('button', { name: '退出 Recovery', exact: true })).toHaveCount(0)
  expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([
    { opcode: 1, payload: [] }, { opcode: 0x42, payload: [] },
    { opcode: 1, payload: [] }, { opcode: 0x42, payload: [] },
    { opcode: 0x41, payload: [] },
  ])
})

test('GR firmware page shows a safe-exit rejection without issuing other commands', async ({ page }) => {
  await page.setViewportSize({ width: 320, height: 568 })
  await connectRecovery(page, 'gr-recovery-reject')
  await page.getByRole('dialog', { name: '设备处于 Recovery' }).getByRole('button', { name: '进入 OTA 页面' }).click()
  await page.getByRole('button', { name: '退出 Recovery', exact: true }).click()
  await expect(page.getByRole('alert')).toContainText('VOLTAGE_BELOW_MIN')
  await expect(page.getByRole('button', { name: '退出 Recovery', exact: true })).toBeEnabled()
  await expect(page.locator('.device-metrics .recovery-mode')).toBeVisible()
  expect(await page.evaluate(() => (window as any).__recoveryCommands.map((value: any) => value.opcode))).toEqual([1, 0x42, 1, 0x42, 0x41])
  await page.screenshot({ path: '../.local/test-results/gr-recovery-firmware-mobile.png', fullPage: true })
})

test('display tab retains screen links and moves device fonts out of firmware', async ({ page }) => {
  await page.setViewportSize({ width: 320, height: 568 })
  await page.goto('/#/device?tab=screen')
  const navigation = page.getByRole('navigation', { name: '设置', exact: true })
  await expect(navigation.getByRole('button', { name: '显示', exact: true })).toHaveClass('active')
  await expect(navigation.getByRole('button', { name: '屏幕', exact: true })).toHaveCount(0)
  await expect(page.getByRole('heading', { name: '屏幕配置' })).toBeVisible()
  const fonts = page.getByRole('region', { name: '设备字体' })
  await expect(fonts).toBeVisible()
  const screenBounds = await page.getByRole('heading', { name: '屏幕配置' }).boundingBox()
  const fontBounds = await fonts.boundingBox()
  expect(fontBounds!.y).toBeGreaterThan(screenBounds!.y)
  expect(fontBounds!.x + fontBounds!.width).toBeLessThanOrEqual(320)
  await fonts.scrollIntoViewIfNeeded()
  await expect(fonts.getByRole('button', { name: '生成默认包' })).toBeVisible()
  await page.screenshot({ path: '../.local/test-results/display-fonts-mobile.png', fullPage: true })
  await navigation.getByRole('button', { name: '固件', exact: true }).click()
  await expect(page.getByRole('heading', { name: '固件更新' })).toBeVisible()
  await expect(fonts).toHaveCount(0)
  await expect(page.getByRole('button', { name: '退出 Recovery', exact: true })).toHaveCount(0)
})

for (const result of ['success', 'cancel', 'clear-fail', 'screen-fail']) test(`screen geometry change with old images: ${result}`, async ({ page }) => {
  await page.goto(`/?image-fixture=1&fixed-slots=1&screen-save=${result}#/device?tab=screen`)
  await page.getByRole('button', { name: /^(打开浏览器记住的设备|连接其他设备)$/ }).click()
  await page.getByRole('button', { name: /选择附近设备/ }).click()
  await expect(page.getByLabel('尺寸 / 分辨率')).toHaveValue('400x300')
  await expect.poll(() => page.evaluate(() => (window as any).__inktagImageCommands.filter((id: number) => id === 0x19).length)).toBeGreaterThanOrEqual(2)
  await page.getByLabel('尺寸 / 分辨率').selectOption('128x296')
  await page.getByRole('button', { name: '保存到设备', exact: true }).click()
  const dialog = page.getByRole('dialog', { name: '更换屏幕配置并清空图片槽' })
  await expect(dialog).toBeVisible()
  await dialog.getByRole('button', { name: result === 'cancel' ? '取消' : '保存并清空', exact: true }).click()
  if (result === 'success') await expect(page.getByText('屏幕配置已保存；原有设备图片已清空，容量已按新尺寸重新计算。', { exact: true })).toBeVisible()
  if (result === 'clear-fail') await expect(page.locator('.library-message')).toContainText('图片清理未确认完成，未发送新屏幕配置')
  if (result === 'screen-fail') await expect(page.locator('.library-message')).toContainText('原有设备图片已清空，但屏幕配置未确认保存')
  const writes = await page.evaluate(() => (window as any).__inktagImageCommands.filter((id: number) => id === 0x1c || id === 0x08))
  expect(writes).toEqual(result === 'cancel' ? [] : result === 'clear-fail' ? [0x1c] : [0x1c, 0x08])
})

for (const width of [1440, 320]) test(`device glyph input has a compact placeholder at ${width}px`, async ({ page }) => {
  await page.setViewportSize({ width, height: 900 })
  await page.goto('/?font-fixture=1#/device?tab=screen')
  await page.getByRole('button', { name: /^(打开浏览器记住的设备|连接其他设备)$/ }).click()
  await page.getByRole('button', { name: /选择附近设备/ }).click()
  const input = page.getByPlaceholder('例如：鹏程万里')
  await input.scrollIntoViewIfNeeded()
  await expect(input).toBeVisible()
  expect(await input.evaluate((node) => getComputedStyle(node, '::placeholder').fontSize)).toBe('12px')
  await expect(input).toHaveCSS('font-size', '13px')
  const bounds = await input.boundingBox()
  expect(bounds!.x + bounds!.width).toBeLessThanOrEqual(width)
  await page.screenshot({ path: `../.local/test-results/device-glyph-input-${width}.png` })
})

test('GR Recovery rejected safe exit keeps the modal open and never forces reset', async ({ page }) => {
  await connectRecovery(page, 'gr-recovery-reject')
  const dialog = page.getByRole('dialog', { name: '设备处于 Recovery' })
  await dialog.getByRole('button', { name: '退出 Recovery' }).click()
  await expect(dialog.getByRole('alert')).toContainText('VOLTAGE_BELOW_MIN')
  await expect(dialog.getByRole('button', { name: '进入 OTA 页面' })).toBeEnabled()
  const commands = await page.evaluate(() => (window as any).__recoveryCommands)
  expect(commands.map((value: any) => value.opcode)).toEqual([1, 0x42, 1, 0x42, 0x41])
  expect(commands.at(-1).payload).toEqual([])
})

test('GR Recovery name with an App role is rejected before showing the modal', async ({ page }) => {
  await connectRecovery(page, 'gr-recovery-wrong-role')
  await page.getByRole('button', { name: /打开设备连接日志/ }).click()
  await expect(page.locator('.log-popover')).toContainText('当前设备仍是 GR 应用，不是固定 Recovery')
  await expect(page.getByRole('dialog', { name: '设备处于 Recovery' })).toBeHidden()
  await expect(page.locator('.device-summary')).not.toContainText('Recovery')
  expect(await page.evaluate(() => (window as any).__recoveryCommands)).toEqual([
    { opcode: 1, payload: [] }, { opcode: 0x42, payload: [] },
  ])
})

test('Bluetooth device menu remains inside a narrow mobile viewport', async ({ page }) => {
  await page.setViewportSize({ width: 320, height: 568 })
  await page.goto('/')

  await page.getByRole('button', { name: /^(打开浏览器记住的设备|连接其他设备)$/ }).click()
  const menu = page.locator('.connection-popover')
  await expect(menu).toBeVisible()

  const bounds = await menu.boundingBox()
  expect(bounds).not.toBeNull()
  expect(bounds!.x).toBeGreaterThanOrEqual(0)
  expect(bounds!.x + bounds!.width).toBeLessThanOrEqual(320)
  expect(bounds!.y + bounds!.height).toBeLessThanOrEqual(568)
  await expect(menu.getByRole('button', { name: '选择附近设备', exact: true })).toBeVisible()
  await expect(menu.getByPlaceholder('输入筛选的蓝牙名称')).toBeVisible()
})

test('connection log uses the same narrow mobile viewport margins', async ({ page }) => {
  await page.setViewportSize({ width: 320, height: 568 })
  await page.goto('/')

  await page.getByRole('button', { name: /打开设备连接日志/ }).click()
  const menu = page.locator('.log-popover')
  await expect(menu).toBeVisible()

  const bounds = await menu.boundingBox()
  expect(bounds).not.toBeNull()
  expect(bounds!.x).toBe(8)
  expect(bounds!.x + bounds!.width).toBe(312)
  expect(bounds!.y + bounds!.height).toBeLessThanOrEqual(568)
})

test('production routes survive direct navigation and reload', async ({ page }) => {
  for (const route of ['/#/image', '/#/create', '/#/find', '/#/device?tab=logs']) {
    await page.goto(route)
    await expect(page.locator('main')).toBeVisible()
    await page.reload()
    await expect(page.locator('main')).toBeVisible()
  }
})

for (const validity of ['valid', 'invalid', 'panel', 'expired']) test(`compact nRF52811 temperature sources remain consistent when ${validity}`, async ({ page }) => {
  await page.goto(`/?compact-nrf-temperature=${validity}`)
  await page.getByRole('button', { name: /^(打开浏览器记住的设备|连接其他设备)$/ }).click()
  await page.getByRole('button', { name: /选择附近设备/ }).click()
  await expect(page.locator('.device-summary > div > span')).toHaveText('IKT_NRF_37DE')
  await expect(page.locator('.device-summary')).not.toContainText('aesl22_nrf')
  await expect(page.locator('.device-overview')).toContainText('NRF52811')
  await expect(page.locator('.device-overview')).not.toContainText('nrf52811_asel22')
  const fact = page.locator('.temperature-fact').first()
  await expect(fact.getByLabel('主温度')).toHaveText(validity === 'invalid' ? '--' : validity === 'expired' ? '25 °C' : '22 °C')
  await fact.getByRole('button', { name: '了解温度来源' }).hover()
  await expect(fact.locator('.temperature-sources')).toHaveText(validity === 'panel' ? '屏幕 22.5 °C / MCU 25 °C' : validity === 'expired' ? '屏幕 -- / MCU 25 °C' : validity === 'valid' ? '屏幕 -- / MCU 22 °C' : '屏幕 -- / MCU --')
  await page.screenshot({ path: `../.local/test-results/nrf52811-temperature-${validity}.png` })
  await page.getByRole('button', { name: '断开设备', exact: true }).click()
  await expect(fact.getByLabel('主温度')).toHaveText('--')
  await fact.getByRole('button', { name: '了解温度来源' }).hover()
  await expect(fact.locator('.temperature-sources')).toHaveText('屏幕 -- / MCU --')
})

test('nRF52811 screen picker excludes SES while retaining original and Huawei profiles', async ({ page }) => {
  await page.goto('/?compact-nrf-temperature=panel#/device?tab=screen')
  await page.getByRole('button', { name: /^(打开浏览器记住的设备|连接其他设备)$/ }).click()
  await page.getByRole('button', { name: /选择附近设备/ }).click()
  await page.locator('.profile-select-trigger').click()
  const choices = page.getByRole('listbox')
  await expect(choices.getByRole('option', { name: /SES_SE22(13|66)FS093/ })).toHaveCount(0)
  await expect(choices.getByRole('option', { name: /SSD1619 \/ 2.13寸 \/ 128x250 \/ BWR/ })).toHaveCount(1)
  await expect(choices.getByRole('option', { name: /HUAWEI_398_A0/ })).toHaveCount(1)
  await expect(choices.getByRole('option', { name: /HUAWEI_398_A1/ })).toHaveCount(1)
  await page.getByRole('button', { name: '断开设备', exact: true }).click()
  await page.locator('.profile-select-trigger').click()
  await expect(page.getByRole('listbox').getByRole('option', { name: /SES_SE22(13|66)FS093/ })).toHaveCount(2)
})

test('screen profiles name real adaptations without changing the selected wire recipe', async ({ page }) => {
  await page.goto('/#/create')
  const profiles = page.getByRole('combobox', { name: '屏幕 Profile', exact: true })
  await expect(profiles.locator('option[value="zkc42vm_original"]')).toHaveText('SSD1683 4.2寸 400x300 三色')
  await expect(profiles.locator('option[value="hs_nebular420"]')).toHaveText('JD79668AA 4.2寸 400x300 四色 （适配nebular420r）')
  await expect(profiles.locator('option[value="hs_nebular213"]')).toHaveText('JD79676 2.13寸 128x250 四色 （适配nebular213r）')
  await expect(profiles.locator('option[value="uc8276_standard"]')).toHaveText('UC8276 4.2寸 400x300 三色')
  await expect(profiles.locator('option[value="zkc42v_original"], option[value="nrf_aesl_420_test"]')).toHaveCount(0)
  await expect(profiles.locator('option[value="lenmory_h364f067_e6"]')).toHaveText('JD79665CA 3.64寸 760x568 KWRYGB （适配lenmory_h364f067_e6）')
  await profiles.selectOption('hs_nebular213')
  await expect(page.getByRole('combobox', { name: '模板分辨率' })).toHaveValue('250x128')
  await page.goto('/#/device?tab=screen')
  await page.locator('.profile-select-trigger').click()
  const choices = page.getByRole('listbox')
  await expect(choices.getByRole('option', { name: /三色.*ZKC42VM_FACTORY/ })).toHaveCount(1)
  await expect(choices.getByRole('option', { name: /（适配huawei_a[01]）/ })).toHaveCount(2)
  await page.screenshot({ path: '../.local/test-results/screen-profile-defaults.png', fullPage: true })
})

test('merged UC8276 default preserves installed ZKC42V recipe and stored images on save', async ({ page }) => {
  await page.goto('/?image-fixture=1&fixed-slots=1&screen-fixture=zkc42v&screen-save=success#/device?tab=screen')
  await page.getByRole('button', { name: /^(打开浏览器记住的设备|连接其他设备)$/ }).click()
  await page.getByRole('button', { name: /选择附近设备/ }).click()
  await expect(page.getByLabel('尺寸 / 分辨率')).toHaveValue('400x300')
  await expect.poll(() => page.evaluate(() => (window as any).__inktagImageCommands.filter((id: number) => id === 0x19).length)).toBeGreaterThanOrEqual(2)
  await expect(page.locator('.profile-select-trigger')).toContainText('UC8276 4.2寸 400x300 三色')
  const before = await page.evaluate(() => (window as any).__inktagImageCommands.slice())
  await page.locator('.profile-select-trigger').click()
  const choices = page.getByRole('listbox')
  const standard = choices.getByRole('option', { name: /UC8276 4.2寸 400x300 三色/ })
  await expect(standard).toHaveCount(1)
  await expect(choices.getByRole('option', { name: /ZKC42V_HE042AD7A2/ })).toHaveCount(0)
  await standard.click()
  expect(await page.evaluate(() => (window as any).__inktagImageCommands)).toEqual(before)
  await page.getByRole('button', { name: '保存到设备', exact: true }).click()
  await expect(page.getByText('屏幕配置已保存。', { exact: true })).toBeVisible()
  await expect(page.getByRole('dialog', { name: '更换屏幕配置并清空图片槽' })).toHaveCount(0)
  expect(await page.evaluate(() => (window as any).__inktagImageCommands.filter((id: number) => id === 0x1c || id === 0x08))).toEqual([0x08])
  expect(await page.evaluate(() => (window as any).__screenRequests)).toEqual([[1, 0x15, 1, 2, 0, 0x90, 1, 0x2c, 1, 0, 0, 0, 0, 0x90, 1, 0x2c, 1]])
})

for (const fixture of ['huawei', 'huawei-rotated', 'huawei-custom']) test(`screen selection matches current ${fixture} without writes`, async ({ page }) => {
  await page.goto(`/?screen-fixture=${fixture}#/device?tab=screen`)
  await page.getByRole('button', { name: /^(打开浏览器记住的设备|连接其他设备)$/ }).click()
  await page.getByRole('button', { name: /选择附近设备/ }).click()
  await expect(page.getByLabel('尺寸 / 分辨率')).toHaveValue('768x552')
  const trigger = page.locator('.profile-select-trigger')
  await expect(trigger).toContainText(fixture === 'huawei-custom' ? '自定义配置' : 'JD79665AA 3.98寸 768x552 四色 （适配huawei_a0）')
  const summary = page.getByLabel('设备当前屏参')
  await expect(summary).toContainText('已读取：768×552 · BWRY')
  await expect(summary).toContainText(fixture === 'huawei-rotated' ? '旋转 90°' : '旋转 180°')
  await expect(summary).toContainText('UI scale 2 / schema 1')
  for (const width of [1280, 390]) for (const theme of ['light', 'dark']) {
    await page.setViewportSize({ width, height: 844 })
    await page.evaluate((value) => document.documentElement.setAttribute('data-resolved-theme', value), theme)
    await expect(summary).toHaveCSS('font-size', '12px')
    await expect(summary).toHaveCSS('font-weight', '400')
    expect(await summary.evaluate((node) => node.scrollWidth <= node.clientWidth)).toBe(true)
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true)
  }
  await trigger.click()
  await expect(page.getByRole('listbox').locator('[aria-selected="true"]')).toHaveCount(fixture === 'huawei-custom' ? 0 : 1)
  if (fixture !== 'huawei-custom') await page.getByRole('listbox').getByRole('option', { name: /（适配huawei_a0）/ }).click()
  else await page.keyboard.press('Escape')
  await expect(summary).toContainText(fixture === 'huawei-rotated' ? '旋转 90°' : '旋转 180°')
  expect(await page.evaluate(() => (window as any).__screenRequests)).toEqual([])
  await page.screenshot({ path: `../.local/release-user-preview/screen-${fixture}.png`, fullPage: true })
})

test('temperature help anchor does not inherit fact-card borders or spacing', async ({ page }) => {
  for (const theme of ['light', 'dark']) {
    await page.goto('/')
    await page.evaluate((value) => document.documentElement.setAttribute('data-resolved-theme', value), theme)
    const fact = page.locator('.temperature-fact').first()
    const anchor = fact.locator('.temperature-help-anchor')
    const help = fact.getByRole('button', { name: '了解温度来源' })
    await expect(fact.getByLabel('主温度')).toHaveText('--')
    await expect(anchor).toHaveCSS('border-right-width', '0px')
    await expect(anchor).toHaveCSS('border-bottom-width', '0px')
    await expect(anchor).toHaveCSS('padding', '0px')
    await expect(anchor).not.toHaveCSS('min-height', '54px')
    const bounds = await anchor.boundingBox()
    expect(bounds!.width).toBe(22)
    expect(bounds!.height).toBe(22)
    await expect(fact).toHaveCSS('border-right-width', '1px')
    await help.hover()
    await expect(fact.getByRole('tooltip')).toBeVisible()
    await expect(fact.locator('.temperature-sources')).toHaveText('屏幕 -- / MCU --')
    await expect(fact).not.toContainText('未连接')
    await page.mouse.move(0, 0)
    await expect(fact.getByRole('tooltip')).toBeHidden()
  }
})
