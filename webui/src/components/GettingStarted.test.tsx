// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { AboutInkTag, FirstVisitGuide, GettingStartedDialog, GUIDE_STORAGE_KEY, PROJECT_URL, WEBUI_URL } from './GettingStarted'
import { setPreferences } from '../preferences'
vi.mock('./EpaperColorGuide',()=>({EpaperColorGuide:()=> <div>图片色板与抖动示例</div>}))

describe('getting started and public links', () => {
  let host: HTMLDivElement
  let root: Root
  let storage: { getItem: (key: string) => string | null; setItem: (key: string, value: string) => void }
  beforeEach(() => {
    vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
    // This Node test runtime has no browser Storage. Actual persistence is
    // separately exercised in fresh Chromium desktop/mobile contexts.
    const values = new Map<string, string>()
    storage = { getItem: (key) => values.get(key) ?? null, setItem: (key, value) => { values.set(key, value) } }
    vi.stubGlobal('localStorage', storage)
    setPreferences({ language: 'zh-CN' })
    host = document.createElement('div')
    document.body.append(host)
    root = createRoot(host)
  })
  afterEach(async () => { await act(async () => root.unmount()); host.remove(); vi.restoreAllMocks(); vi.unstubAllGlobals() })
  const click = async (text: string) => act(async () => { Array.from(host.querySelectorAll('button')).find((b) => b.textContent === text)!.click() })
  it('shows four topics once and does not connect or write a device', async () => {
    await act(async () => root.render(<FirstVisitGuide />))
    expect(host.querySelector('h2')?.textContent).toBe('认识墨水屏')
    expect(host.querySelector('[role=dialog] a')).toBeNull()
    expect(host.textContent).not.toContain('Good Display')
    expect(host.textContent).toContain('利用环境光')
    expect(host.textContent).toContain('护眼不代表医疗保证')
    expect(host.textContent).toContain('设备的蓝牙、计时和刷新仍会消耗电量')
    expect(host.textContent).toContain('通常需要几秒到十几秒')
    expect(host.textContent).toContain('部分多色屏或低温环境会更久')
    expect(host.textContent).toContain('屏幕保持画面无需持续供电')
    expect(host.textContent).toContain('纽扣电池也能长时间使用')
    expect(host.textContent).toContain('并非整机完全不耗电')
    await click('下一页')
    expect(host.querySelector('h2')?.textContent).toBe('墨水屏的颜色')
    expect(host.querySelectorAll('[role=tab]')).toHaveLength(4)
    expect(host.querySelectorAll('[role=tab]')[1].textContent).toBe('墨水屏的颜色')
    expect(host.textContent).toContain('图片色板与抖动示例')
    await click('下一页')
    expect(host.querySelector('h2')?.textContent).toBe('连接与核对屏幕')
    expect(host.querySelectorAll('.guide-figures img')).toHaveLength(2)
    expect(host.querySelectorAll('.guide-figures img[alt]')).toHaveLength(2)
    await click('下一页')
    expect(host.querySelector('h2')?.textContent).toBe('选择内容与发送')
    expect(host.querySelectorAll('.guide-figures img')).toHaveLength(2)
    expect(host.textContent).toContain('选择课程表模板')
    expect(host.textContent).toContain('编辑课程并发送')
    expect(host.textContent).toContain('发送到设备')
    expect(host.textContent).toContain('再点击单元格修改课程和时间')
    expect(host.textContent).not.toContain('发图片')
    expect(Array.from(host.querySelectorAll('.guide-figures img')).map(img => img.getAttribute('src')))
      .toEqual(expect.arrayContaining([expect.stringContaining('schedule-template.png'), expect.stringContaining('schedule-editor.png')]))
    expect(host.textContent).toContain('设备动态页面暂不支持保存到设备')
    expect(host.textContent).not.toMatch(/\bFull\b|\bOTA\b|日常固件更新/i)
    await click('开始使用')
    expect(host.querySelector('[role=dialog]')).toBeNull()
    expect(window.localStorage.getItem(GUIDE_STORAGE_KEY)).toBe('seen')
    await act(async () => root.render(<FirstVisitGuide key="again" />))
    expect(host.querySelector('[role=dialog]')).toBeNull()
  })
  it('skip is explicit and records the local preference only', async () => {
    await act(async () => root.render(<FirstVisitGuide />))
    await click('暂时跳过')
    expect(window.localStorage.getItem(GUIDE_STORAGE_KEY)).toBe('seen')
    expect(host.querySelector('[role=dialog]')).toBeNull()
  })
  it('supports private browsers without usable local storage', async () => {
    vi.spyOn(storage, 'getItem').mockImplementation(() => { throw new Error('blocked') })
    vi.spyOn(storage, 'setItem').mockImplementation(() => { throw new Error('blocked') })
    await act(async () => root.render(<FirstVisitGuide />))
    await click('暂时跳过')
    expect(host.querySelector('[role=dialog]')).toBeNull()
  })
  it('about works offline and can reopen an already completed guide', async () => {
    window.localStorage.setItem(GUIDE_STORAGE_KEY, 'seen')
    await act(async () => root.render(<AboutInkTag />))
    const links = Array.from(host.querySelectorAll('a'))
    expect(links.map((a) => a.href)).toEqual([PROJECT_URL, WEBUI_URL, `${WEBUI_URL}manual/zh-CN/getting-started.html`])
    expect(host.textContent).not.toContain('墨水屏介绍')
    expect(links.every((a) => a.rel === 'noopener noreferrer')).toBe(true)
    await click('查看入门教程')
    expect(host.querySelector('[role=dialog]')).not.toBeNull()
    expect(host.querySelector('[role=dialog] a')).toBeNull()
    expect(host.textContent).not.toContain('Good Display')
    await click('暂时跳过')
    expect(host.querySelector('[role=dialog]')).toBeNull()
  })
  it('bounds arrow navigation and dismisses with Escape', async () => {
    const close = vi.fn()
    await act(async () => root.render(<GettingStartedDialog onClose={close} />))
    const dialog = host.querySelector('[role=dialog]')!
    for (let i = 0; i < 4; i++) await act(async () => dialog.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowRight', bubbles: true })))
    expect(host.querySelector('.guide-step')?.textContent).toContain('第 4 / 4 页')
    await act(async () => dialog.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true })))
    expect(close).toHaveBeenCalledOnce()
  })
  it('opens an accessible screenshot viewer and Escape closes the image, not the guide', async () => {
    const close = vi.fn()
    await act(async () => root.render(<GettingStartedDialog onClose={close} />))
    await click('下一页')
    await click('下一页')
    const trigger = host.querySelector<HTMLButtonElement>('.guide-screenshot')!
    await act(async () => { trigger.focus(); trigger.click() })
    const viewer = host.querySelector('.guide-image-zoom')!
    expect(viewer.getAttribute('aria-labelledby')).toBe('guide-image-title')
    expect(viewer.querySelector('img')?.getAttribute('alt')).toContain('框和箭头')
    await act(async () => viewer.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true })))
    expect(host.querySelector('.guide-image-zoom')).toBeNull()
    expect(close).not.toHaveBeenCalled()
    expect(document.activeElement).toBe(trigger)
  })
  it.each(['zh-CN', 'en'] as const)('keeps timetable frames and arrows in both thumbnail and zoom (%s)', async (language) => {
    setPreferences({ language })
    await act(async () => root.render(<GettingStartedDialog onClose={vi.fn()} />))
    const dialog = host.querySelector('[role=dialog]')!
    for (let i = 0; i < 3; i++) await act(async () => dialog.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowRight', bubbles: true })))
    expect(host.querySelectorAll('.guide-annotations')).toHaveLength(2)
    expect(host.querySelectorAll('.guide-mark-box')).toHaveLength(4)
    expect(host.querySelectorAll('.guide-mark-arrow[marker-end]')).toHaveLength(4)
    expect(Array.from(host.querySelectorAll('.guide-mark-text')).map(node => node.textContent)).toEqual(language === 'en'
      ? ['Filter by panel', 'Open timetable', 'Edit classes', 'Connect, then send']
      : ['按屏幕筛选', '打开课程表模板', '编辑课程内容', '连接后发送'])
    await act(async () => host.querySelectorAll<HTMLButtonElement>('.guide-screenshot')[1].click())
    expect(host.querySelectorAll('.guide-image-zoom .guide-mark-box')).toHaveLength(2)
    expect(host.querySelector('.guide-image-zoom .guide-annotations')?.getAttribute('viewBox')).toBe('0 0 592 746')
    const markerIds = Array.from(host.querySelectorAll('marker')).map(node => node.id)
    expect(new Set(markerIds).size).toBe(markerIds.length)
  })
})
