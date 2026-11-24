import { describe, expect, it } from 'vitest'
import { deviceGuiWidgetCatalog } from './catalog'
import { deviceFontData } from './fonts.generated'
import { DEFAULT_DEVICE_GUI_COLOR_COUNT, normalizeDeviceGuiDocument, rasterFontOptions } from './model'
import { createDefaultDeviceGuiDocument, createDeviceGuiPresetDocument, deviceGuiPresetOptions } from './presets'
import { deviceGuiCountdownDays, deviceGuiRoleColor } from './GUI_custom'
import { deviceGuiSizeClass, estimateDeviceGuiBytes, validateDeviceGuiDocument } from './space'
import { U8g2, U8g2Font } from '../vendor/u8g2-js/index.js'

describe('device GUI protocol model', () => {
  it('uses the same compact and large thresholds as the current firmware GUI', () => {
    expect(deviceGuiSizeClass(128, 250)).toBe('compact')
    expect(deviceGuiSizeClass(152, 296)).toBe('compact')
    expect(deviceGuiSizeClass(400, 300)).toBe('normal')
    expect(deviceGuiSizeClass(800, 480)).toBe('large')
  })

  it('defines compact, normal and large space for every widget', () => {
    expect(deviceGuiWidgetCatalog).toHaveLength(30)
    for (const widget of deviceGuiWidgetCatalog) {
      expect(widget.sizes.compact.width).toBeGreaterThan(0)
      expect(widget.sizes.normal.width).toBeGreaterThanOrEqual(widget.sizes.compact.width)
      expect(widget.sizes.large.width).toBeGreaterThanOrEqual(widget.sizes.normal.width)
      expect(widget.appearances.length).toBeGreaterThan(0)
    }
  })

  it('authors layouts in BWR and collapses red accents to black on BW panels', () => {
    expect(DEFAULT_DEVICE_GUI_COLOR_COUNT).toBe(3)
    expect(deviceGuiRoleColor('accent', DEFAULT_DEVICE_GUI_COLOR_COUNT)).toBe('#c82d28')
    expect(deviceGuiRoleColor('accent', 2)).toBe('#222222')
    expect(deviceGuiRoleColor('black', 2)).toBe('#171a18')
    expect(deviceGuiRoleColor('white', 2)).toBe('#ffffff')
  })

  it('rejects numeric-only fonts on text controls', () => {
    const document = createDeviceGuiPresetDocument('agenda-board', 400, 300, 3)
    const agenda = document.widgets.find((widget) => widget.kind === 'agenda-list')!
    agenda.style.fontId = 'helv-bold-18-num'
    expect(validateDeviceGuiDocument(document)).toContainEqual(expect.objectContaining({
      code: 'glyph-coverage',
      widgetId: agenda.id,
    }))
  })

  it.each([
    ['agenda-board', 128, 250],
    ['agenda-board', 152, 296],
    ['schedule-grid', 128, 250],
    ['schedule-grid', 400, 300],
  ] as const)('keeps reference preset %s inside %sx%s', (presetId, width, height) => {
    const document = createDeviceGuiPresetDocument(presetId, width, height, 3)
    expect(validateDeviceGuiDocument(document)).toEqual([])
    expect(estimateDeviceGuiBytes(document)).toBeLessThan(1024)
  })

  it('keeps one named reference preset for every device page kind', () => {
    expect(new Set(deviceGuiPresetOptions.map((preset) => preset.kind))).toEqual(new Set(['device-info', 'calendar', 'clock', 'agenda', 'schedule']))
  })

  it('keeps arbitrary fixed text and data-heavy structures out of native delivery', () => {
    expect(deviceGuiWidgetCatalog.filter((widget) => widget.delivery === 'raster').map((widget) => widget.kind)).toEqual([
      'agenda-list',
      'schedule-table',
      'raster-text',
    ])
  })

  it('uses only firmware fonts for dynamic controls and separate web fonts for raster text', () => {
    expect(rasterFontOptions.map((font) => font.id)).toEqual(expect.arrayContaining(['system-sans', 'yahei', 'simsun', 'kaiti']))
    const document = createDefaultDeviceGuiDocument('calendar', 400, 300, 3)
    expect(document.widgets.every((widget) => widget.style.fontScaleQ8 === 256)).toBe(true)
  })

  it('bounds Q8 font scaling and migrates drafts created before the field existed', () => {
    const document = createDefaultDeviceGuiDocument('calendar', 400, 300, 3)
    const widget = document.widgets[0]
    widget.style.fontScaleQ8 = 127
    expect(validateDeviceGuiDocument(document)).toContainEqual(expect.objectContaining({ code: 'limit', widgetId: widget.id }))
    delete (widget.style as Partial<typeof widget.style>).fontScaleQ8
    const migrated = normalizeDeviceGuiDocument(document)
    expect(migrated.widgets[0].style.fontScaleQ8).toBe(256)
    expect(validateDeviceGuiDocument(migrated)).toEqual([])
  })

  it('rejects oversized fixed agenda and table content', () => {
    const agenda = createDefaultDeviceGuiDocument('agenda', 128, 250, 3)
    agenda.widgets.find((widget) => widget.kind === 'agenda-list')!.data.text = Array.from({ length: 9 }, (_, index) => String(index)).join('\n')
    expect(validateDeviceGuiDocument(agenda)).toContainEqual(expect.objectContaining({ code: 'content-limit' }))

    const schedule = createDefaultDeviceGuiDocument('schedule', 400, 300, 3)
    schedule.widgets.find((widget) => widget.kind === 'schedule-table')!.data.text = '1|2|3|4|5|6|7|8|9'
    expect(validateDeviceGuiDocument(schedule)).toContainEqual(expect.objectContaining({ code: 'content-limit' }))
  })

  it('calculates date-only countdowns from local midnight', () => {
    expect(deviceGuiCountdownDays('2026-10-14', new Date(2026, 7, 25, 23, 59))).toBe(50)
  })
})

describe('firmware U8g2 fonts', () => {
  it('keeps generated byte lengths equal to the C declarations', () => {
    expect(deviceFontData['wqy9-lunar'].bytes).toBe(4315)
    expect(deviceFontData['wqy12-lunar'].bytes).toBe(4830)
    expect(deviceFontData['helv-bold-14-num'].bytes).toBe(287)
    expect(deviceFontData['helv-bold-18-num'].bytes).toBe(389)
  })

  it.each([
    ['wqy9-lunar', '年月日'],
    ['wqy12-lunar', '星期一'],
    ['helv-bold-14-num', '12:34'],
    ['helv-bold-18-num', '20:26'],
  ] as const)('renders %s without a browser font fallback', (id, text) => {
    const renderer = new U8g2({ width: 128, height: 32, layout: 'vertical' })
    renderer.setFont(U8g2Font.fromBase64(deviceFontData[id].base64))
    renderer.setFontPosTop()
    renderer.clearBuffer()
    renderer.drawUTF8(0, 0, text)
    expect(renderer.displayMemory.some((value) => value !== 0)).toBe(true)
  })
})
