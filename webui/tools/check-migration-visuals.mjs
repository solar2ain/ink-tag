import { chromium } from '@playwright/test'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'

const baseline = process.argv.includes('--baseline')
const root = resolve(import.meta.dirname, '../../.local/client-migration-visuals')
await mkdir(root, { recursive: true })
const browser = await chromium.launch({ headless: true })
const results = []
try {
  for (const [size, viewport] of Object.entries({ desktop: { width: 1366, height: 900 }, mobile: { width: 390, height: 844 } })) {
    const context = await browser.newContext({ viewport, locale: 'zh-CN', colorScheme: 'light' })
    const page = await context.newPage()
    await page.clock.setFixedTime(new Date('2026-09-20T06:45:00+08:00'))
    for (const [name, route] of Object.entries({ overview: '/', image: '/image', create: '/create', library: '/library', device: '/device', screen: '/device?tab=screen', activation: '/device?tab=activation', ota: '/device?tab=ota' })) {
      await page.goto((process.env.INKTAG_VISUAL_ORIGIN || 'http://127.0.0.1:5174') + '/#' + route)
      await page.locator('main').waitFor()
      await page.evaluate(() => document.fonts.ready)
      await page.waitForTimeout(1200)
      const bytes = await page.screenshot({ animations: 'disabled' })
      const key = size + '-' + name
      if (baseline) await writeFile(resolve(root, key + '.png'), bytes)
      else {
        const equal = bytes.equals(await readFile(resolve(root, key + '.png')))
        results.push({ key, equal })
        if (!equal) await writeFile(resolve(root, key + '-after.png'), bytes)
      }
    }
    await context.close()
  }
} finally { await browser.close() }
if (!baseline) {
  await writeFile(resolve(root, 'result.json'), JSON.stringify(results, null, 2))
  console.log(results)
  if (results.some(r => !r.equal)) process.exitCode = 1
} else console.log('Saved 16 migration baseline screenshots to ' + root)
