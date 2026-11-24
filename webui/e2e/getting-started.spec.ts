import { expect, test } from '@playwright/test'

test.use({ locale: 'zh-CN' })

for (const viewport of [{ width: 1280, height: 800 }, { width: 390, height: 844 }]) {
  test(`first visit guide and offline about at ${viewport.width}px`, async ({ page }) => {
    await page.setViewportSize(viewport)
    await page.goto('/#/device?tab=about')
    const dialog = page.getByRole('dialog', { name: '认识墨水屏' })
    await expect(dialog).toBeVisible()
    const initialBounds = (await page.locator('.getting-started').boundingBox())!
    for (const title of ['认识墨水屏', '墨水屏的颜色', '连接与核对屏幕', '选择内容与发送']) {
      await expect(page.getByRole('heading', { name: title, exact: true })).toBeVisible()
      expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true)
      const bounds = await page.locator('.getting-started').boundingBox()
      expect(bounds?.width).toBeLessThanOrEqual(viewport.width - 24)
      expect(bounds?.y).toBeGreaterThanOrEqual(0)
      expect(bounds!.y + bounds!.height).toBeLessThanOrEqual(viewport.height)
      expect(bounds!.height).toBeCloseTo(initialBounds.height, 1)
      expect(bounds!.y).toBeCloseTo(initialBounds.y, 1)
      expect(await page.locator('.guide-slide').evaluate(element => element.scrollTop)).toBe(0)
      await expect(page.getByRole('button', { name: title === '选择内容与发送' ? '开始使用' : '下一页', exact: true })).toBeInViewport()
      if (title === '认识墨水屏') {
        await expect(page.locator('.guide-facts')).toContainText('几秒到十几秒')
        await expect(page.locator('.guide-facts')).toContainText('屏幕保持画面无需持续供电')
        await expect(page.locator('.guide-note')).toContainText('纽扣电池')
        await expect(page.locator('.guide-note')).toContainText('并非整机完全不耗电')
        await page.locator('.guide-slide').evaluate((element) => element.scrollTop = element.scrollHeight)
        await expect(page.locator('.guide-note')).toBeInViewport()
      }
      if (title === '连接与核对屏幕' || title === '选择内容与发送') {
        await expect(page.locator('.guide-figures img')).toHaveCount(2)
        for (const img of await page.locator('.guide-figures img').all()) {
          expect(await img.evaluate((element: HTMLImageElement) => element.complete && element.naturalWidth > 300)).toBe(true)
        }
        if (title === '选择内容与发送') {
          await expect(page.locator('.guide-figures')).toContainText('选择课程表模板')
          await expect(page.locator('.guide-figures')).toContainText('发送到设备')
          await expect(page.locator('.guide-figures')).not.toContainText('设备信息模板')
          await expect(page.locator('.getting-started')).not.toContainText('Full')
          await expect(page.locator('.getting-started')).not.toContainText('日常固件更新')
          await expect(page.locator('.guide-figures img').nth(0)).toHaveAttribute('src', /schedule-template-/)
          await expect(page.locator('.guide-figures img').nth(1)).toHaveAttribute('src', /schedule-editor-/)
          for (const img of await page.locator('.guide-figures img').all()) await expect(img).toHaveAttribute('alt', /课程表.*红框和箭头/)
          await expect(page.locator('.guide-figures .guide-mark-box')).toHaveCount(4)
          for (const image of await page.locator('.guide-figures .guide-annotated').all()) {
            const photo = await image.locator('img').boundingBox()
            const overlay = await image.locator('.guide-annotations').boundingBox()
            expect(overlay!.width).toBeCloseTo(photo!.width, 0)
            expect(overlay!.height).toBeCloseTo(photo!.height, 0)
          }
        }
        const trigger = page.getByRole('button', { name: /放大截图/ }).first()
        await trigger.click()
        await expect(page.locator('.guide-image-zoom')).toBeVisible()
        await expect(page.getByRole('button', { name: '关闭大图' })).toBeFocused()
        if (title === '选择内容与发送') {
          await expect(page.locator('.guide-image-zoom .guide-mark-box')).toHaveCount(2)
          const photo = await page.locator('.guide-image-zoom img').boundingBox()
          const overlay = await page.locator('.guide-image-zoom .guide-annotations').boundingBox()
          expect(overlay!.width).toBeCloseTo(photo!.width, 0)
          expect(overlay!.height).toBeCloseTo(photo!.height, 0)
        }
        await page.keyboard.press('Escape')
        await expect(page.locator('.guide-image-zoom')).toHaveCount(0)
        await expect(trigger).toBeFocused()
        if (viewport.width === 390) {
          await page.locator('.guide-slide').evaluate((element) => element.scrollTop = element.scrollHeight)
          await expect(page.locator('.guide-example')).toBeInViewport()
        }
      }
      if (title === '选择内容与发送' && viewport.width === 1280) await page.locator('.guide-slide').evaluate(element => element.scrollTop = 24)
      await page.screenshot({ path: test.info().outputPath(`guide-${viewport.width}-${title}.png`) })
      if(title==='墨水屏的颜色') {
        await expect(page.getByRole('tab',{name:'墨水屏的颜色',exact:true})).toHaveAttribute('aria-selected','true')
        await expect(page.getByRole('tab').nth(1)).toHaveText('墨水屏的颜色')
        await expect(page.locator('.color-example-grid canvas')).toHaveCount(9)
        await expect(page.getByText('正在使用实际图片处理算法生成示例…')).toHaveCount(0)
        const pixels=await page.locator('.color-example-grid canvas').evaluateAll(nodes=>nodes.map(node=>(node as HTMLCanvasElement).toDataURL()))
        expect(new Set(pixels).size).toBe(9)
        const paletteCounts = await page.locator('.color-example-grid canvas').evaluateAll(nodes => nodes.map(node => {
          const canvas = node as HTMLCanvasElement, data = canvas.getContext('2d')!.getImageData(0, 0, canvas.width, canvas.height).data
          const colors = new Set<string>(); let red = 0
          for (let i = 0; i < data.length; i += 4) {
            colors.add(`${data[i]},${data[i+1]},${data[i+2]}`)
            if(data[i] > 150 && data[i] > data[i+1] * 1.5 && data[i] > data[i+2] * 1.5) red++
          }
          return {count: colors.size, red}
        }))
        expect(paletteCounts[0].red).toBeGreaterThan(1000)
        // With a charcoal vase this photo does not use the six-color palette's blue ink.
        expect(paletteCounts.map(item => item.count).slice(1)).toEqual([2, 2, 3, 3, 4, 4, 5, 7])
        expect(paletteCounts[3].red).toBeGreaterThan(1000)
        // The dark vase must remain visible against the light background,
        // including both black/white conversion examples.
        for (const index of [0, 1, 2, 3]) {
          const darkFraction = await page.locator('.color-example-grid canvas').nth(index).evaluate(node => {
            const canvas = node as HTMLCanvasElement
            const data = canvas.getContext('2d')!.getImageData(
              Math.round(canvas.width * .43), Math.round(canvas.height * .73),
              Math.round(canvas.width * .21), Math.round(canvas.height * .21),
            ).data
            let dark = 0
            for (let i = 0; i < data.length; i += 4) if (Math.max(data[i], data[i+1], data[i+2]) < 128) dark++
            return dark / (data.length / 4)
          })
          expect(darkFraction).toBeGreaterThan(.6)
        }
        await page.locator('.color-example-grid figure').first().scrollIntoViewIfNeeded()
        await page.screenshot({ path: test.info().outputPath(`dark-vase-${viewport.width}.png`) })
        const footerBefore = (await page.locator('.guide-footer').boundingBox())!
        await page.locator('.guide-slide').evaluate(element => element.scrollTop = element.scrollHeight)
        await expect(page.getByText(/单文件最多 20 MiB/)).toBeInViewport()
        expect(await page.locator('.guide-slide').evaluate(element => element.scrollTop)).toBeGreaterThan(0)
        expect((await page.locator('.guide-footer').boundingBox())!.y).toBeCloseTo(footerBefore.y, 1)
        await expect(page.locator('.epaper-color-guide')).toContainText('并不会增加屏幕真正支持的颜色')
        await expect(page.locator('.epaper-color-guide')).toContainText('20 MiB')
        await page.getByRole('tab', {name:'认识墨水屏', exact:true}).click()
        expect(await page.locator('.guide-slide').evaluate(element => element.scrollTop)).toBe(0)
        await page.getByRole('tab', {name:'墨水屏的颜色', exact:true}).click()
      }
      if (title !== '选择内容与发送') await page.getByRole('button', { name: '下一页', exact: true }).click()
    }
    await page.getByRole('button', { name: '开始使用', exact: true }).click()
    await expect(page.getByRole('dialog')).toHaveCount(0)
    await page.reload()
    await expect(page.getByRole('heading', { name: '关于 InkTag' })).toBeVisible()
    await expect(page.getByRole('dialog')).toHaveCount(0)
    await expect(page.locator('.about-inktag a').nth(0)).toHaveAttribute('href', 'https://github.com/solar2ain/ink-tag')
    await page.getByRole('button', { name: '查看入门教程' }).click()
    await expect(page.getByRole('dialog', { name: '认识墨水屏' })).toBeVisible()
    await page.keyboard.press('ArrowRight')
    await expect(page.getByRole('heading', { name: '墨水屏的颜色' })).toBeVisible()
    await page.keyboard.press('Escape')
    await expect(page.getByRole('dialog')).toHaveCount(0)
    await expect(page.getByRole('button', { name: '查看入门教程' })).toBeFocused()
  })
}

test('mobile guide supports horizontal swipes without treating a vertical gesture as a page change', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 })
  await page.goto('/#/device?tab=about')
  await expect(page.getByRole('heading', { name: '认识墨水屏', exact: true })).toBeVisible()
  const swipe = async (dx: number, dy: number) => page.locator('.guide-slide').evaluate((node, delta) => {
    const start = new Touch({ identifier: 1, target: node, clientX: 280, clientY: 320 })
    const end = new Touch({ identifier: 1, target: node, clientX: 280 + delta.dx, clientY: 320 + delta.dy })
    node.dispatchEvent(new TouchEvent('touchstart', { bubbles: true, touches: [start], changedTouches: [start] }))
    node.dispatchEvent(new TouchEvent('touchend', { bubbles: true, touches: [], changedTouches: [end] }))
  }, { dx, dy })
  await swipe(-80, 150)
  await expect(page.getByRole('heading', { name: '认识墨水屏', exact: true })).toBeVisible()
  await swipe(-120, 3)
  await expect(page.getByRole('heading', { name: '墨水屏的颜色', exact: true })).toBeVisible()
  await swipe(120, 3)
  await expect(page.getByRole('heading', { name: '认识墨水屏', exact: true })).toBeVisible()
})
