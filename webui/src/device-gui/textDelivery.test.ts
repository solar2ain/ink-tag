import { describe, expect, it, vi } from 'vitest'
import { missingDeviceGlyphs, normalizeDeviceText } from './fontRenderer'
import { defaultDeviceGuiStyle, type DeviceGuiDocument, type DeviceGuiWidget } from './model'
import { planTextDelivery, selectTextWidgetFont, textDeliveryLabel, textWidgetDelivery } from './textDelivery'
import { renderDeviceGuiDocument } from './GUI_custom'
import { validateDeviceGuiDocument } from './space'

const widget = (text: string): DeviceGuiWidget => ({ id: 'text', kind: 'text', variant: 'normal',
  frame: { x: 0, y: 0, width: 200, height: 60 }, style: { ...defaultDeviceGuiStyle }, data: { text } })
const documentFor = (item: DeviceGuiWidget): DeviceGuiDocument => ({ schemaVersion: 1, id: 'test', name: 'Test',
  kind: 'calendar', width: 200, height: 200, colorCount: 3, sizeClass: 'normal', updatedAt: 0, widgets: [item] })

describe('actual firmware glyph coverage', () => {
  it('accepts built-in date text, ASCII, spaces and line breaks in WQY 9', () => {
    expect(missingDeviceGlyphs('wqy9-lunar', '2026年10月31日 星期六\r\n[GR_3A57]\t24℃')).toEqual([])
    expect(normalizeDeviceText('A\r\nB\rC\tD')).toBe('A\nB\nC    D')
  })
  it('checks the selected subset, not the WQY family name', () => {
    expect(missingDeviceGlyphs('wqy9-lunar', 'InkTag')).toEqual([])
    expect(missingDeviceGlyphs('wqy12-lunar', 'InkTag').length).toBeGreaterThan(0)
    expect(missingDeviceGlyphs('helv-bold-14-num', '年月日')).toEqual(['年', '月', '日'])
  })
  it('deduplicates missing CJK, punctuation and complete supplementary code points', () => {
    expect(missingDeviceGlyphs('wqy9-lunar', '龘龘𠮷🙂')).toEqual(['龘', '𠮷', '🙂'])
  })
})

describe('text delivery is derived and cannot silently freeze variables', () => {
  it('reports missing glyphs while preserving the chosen WQY font and authoring data', () => {
    const item = widget('2026年 龘')
    const before = JSON.stringify(item)
    expect(textWidgetDelivery(item)).toMatchObject({ mode: 'blocked', reason: 'missing-glyphs', missingGlyphs: ['龘'] })
    expect(textDeliveryLabel(textWidgetDelivery(item)!)).toBe('所选字库缺字')
    expect(JSON.stringify(item)).toBe(before)
    expect(textWidgetDelivery(JSON.parse(before))).toEqual(textWidgetDelivery(item))
    item.data.text = '2026年'
    expect(textWidgetDelivery(item)?.mode).toBe('device-text')
  })
  it('keeps explicit web fonts static even if the device has every glyph', () => {
    expect(textWidgetDelivery({ ...widget('2026'), kind: 'raster-text' })).toMatchObject({ mode: 'background', reason: 'web-font' })
    expect(textWidgetDelivery(widget(''))?.mode).toBe('device-text')
  })
  it('lets any fixed textbox explicitly choose WQY or a static font without changing its text or frame', () => {
    const original = widget('春江花月夜')
    const web = selectTextWidgetFont(original, 'ma-shan-zheng')
    expect(web.kind).toBe('raster-text')
    expect(web.data.rasterFontId).toBe('ma-shan-zheng')
    expect(textWidgetDelivery(web)).toMatchObject({ mode: 'background', missingGlyphs: [] })
    const wqy = selectTextWidgetFont(web, 'wqy9-lunar')
    expect(wqy.kind).toBe('text')
    expect(wqy.style.fontId).toBe('wqy9-lunar')
    expect(wqy.data.text).toBe(original.data.text)
    expect(wqy.frame).toEqual(original.frame)
    expect(textWidgetDelivery(wqy)).toMatchObject({ mode: 'blocked', missingGlyphs: ['江', '花'] })
    expect(original.kind).toBe('text')
    expect(original.data.rasterFontId).toBeUndefined()
    expect(selectTextWidgetFont(original, 'unknown-font')).toBe(original)
  })
  it('rechecks the selected size-specific subset immediately on a font change', () => {
    const item = widget('InkTag')
    expect(textWidgetDelivery(item)?.missingGlyphs).toEqual([])
    const wqy12 = selectTextWidgetFont(item, 'wqy12-lunar')
    expect(textWidgetDelivery(wqy12)?.missingGlyphs).toEqual(['I', 'n', 'k', 'T', 'a', 'g'])
    expect(textWidgetDelivery(selectTextWidgetFont(wqy12, 'wqy9-lunar'))?.missingGlyphs).toEqual([])
  })
  it('requires splitting an unsupported literal away from a valid dynamic value', () => {
    expect(planTextDelivery('wqy9-lunar', [{ type: 'literal', text: '龘' },
      { type: 'variable', id: 'day', possibleCharacters: '0123456789' }])).toMatchObject({ mode: 'split-required' })
  })
  it('checks the full variable repertoire, not a safe current sample', () => {
    expect(planTextDelivery('wqy9-lunar', [{ type: 'variable', id: 'date', possibleCharacters: '0123456789年月日' }]).mode).toBe('device-text')
    expect(planTextDelivery('wqy9-lunar', [{ type: 'variable', id: 'name', possibleCharacters: 'InkTag龘' }])).toMatchObject({ mode: 'blocked', reason: 'variable-glyphs' })
    expect(planTextDelivery('wqy9-lunar', [{ type: 'variable', id: 'unrestricted-name' }])).toMatchObject({ mode: 'blocked', reason: 'unbounded-variable' })
  })
})

describe('render respects the explicit font choice while validation reports missing glyphs', () => {
  const sample = { now: new Date(2026, 8, 25), voltage: 3.01, temperature: 24, deviceName: 'InkTag',
    bluetoothName: 'GR_3A57', batteryPercent: 80, boardName: 'zkc42v', screenInfo: '200x200', activated: false }
  function render(item: DeviceGuiWidget) {
    const context = { save: vi.fn(), restore: vi.fn(), beginPath: vi.fn(), rect: vi.fn(), clip: vi.fn(),
      fillRect: vi.fn(), fillText: vi.fn(), font: '', textBaseline: '', textAlign: '', fillStyle: '' }
    const canvas = { getContext: () => context } as unknown as HTMLCanvasElement
    renderDeviceGuiDocument(canvas, documentFor(item), sample)
    return context
  }
  it('does not substitute web glyphs or rasterize the box when WQY lacks a character', () => {
    const item = widget('2026年龘')
    // Even a previously selected web font must not override the current WQY choice.
    item.data.rasterFontId = 'ma-shan-zheng'
    const context = render(item)
    expect(context.fillText).not.toHaveBeenCalled()
    expect(context.fillRect.mock.calls).toEqual(render(widget('2026年')).fillRect.mock.calls)
    expect(item.style.fontId).toBe('wqy9-lunar')
    expect(item.kind).toBe('text')
  })
  it('keeps supported text on the bitmap-font path and empty boxes empty', () => {
    expect(render(widget('2026年')).fillText).not.toHaveBeenCalled()
    expect(render(widget('')).fillRect).toHaveBeenCalledTimes(1)
  })
  it('uses web rendering only after the user explicitly selects a static font', () => {
    const item = selectTextWidgetFont(widget('春江花月夜'), 'ma-shan-zheng')
    const context = render(item)
    expect(context.font).toContain('InkTag Ma Shan Zheng')
    expect(context.fillText.mock.calls[0][0]).toBe('春江花月夜')
  })
  it('reports a non-blocking missing-character warning even for imported drafts', () => {
    expect(validateDeviceGuiDocument(documentFor(widget('龘')))).toContainEqual(expect.objectContaining({
      level: 'warning', code: 'glyph-coverage', widgetId: 'text', message: expect.stringContaining('WQY 9 不包含「龘」'),
    }))
  })
})
