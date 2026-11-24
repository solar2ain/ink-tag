import { createHash } from 'node:crypto'
import { describe, expect, it } from 'vitest'
import golden from './layoutV1CandidateGolden.generated.json'
import { layoutDefaultScaleQ8, layoutScreensV1 } from '../design/layoutPreviewModel'
import { calendarRowExample, previewReferenceTimestamp } from '../design/layoutPreviewTime'
import { applyNativeEdition, createNativeReference, nativeReferenceFrame, nativeReferencePresets, nativeReferenceScene, orderedVisibleWidgets, refreshNativeElements } from './nativeReference'
import { renderDeviceGuiDocument } from './GUI_custom'
import { normalizeDeviceGuiDocument, type DeviceGuiDocument, type DeviceGuiSampleData } from './model'
import { textWidgetDelivery } from './textDelivery'
import { validateDeviceGuiDocument } from './space'

function renderCanvas(document: DeviceGuiDocument) {
  const pixels = new Uint8Array(document.width * document.height)
  const context = {
    imageSmoothingEnabled: false, fillStyle: '', fillRect: () => pixels.fill(0),
    createImageData: (width: number, height: number) => ({ width, height, data: new Uint8ClampedArray(width * height * 4) }),
    putImageData: (image: { width: number; height: number; data: Uint8ClampedArray }, left: number, top: number) => {
      for (let y = 0; y < image.height; y++) for (let x = 0; x < image.width; x++) {
        if (x + left < 0 || y + top < 0 || x + left >= document.width || y + top >= document.height) continue
        const red = image.data[(y * image.width + x) * 4]
        pixels[(y + top) * document.width + x + left] = red === 255 ? 0 : red === 23 ? 1 : 2
      }
    },
  }
  renderDeviceGuiDocument({ getContext: () => context } as unknown as HTMLCanvasElement, document, {} as DeviceGuiSampleData)
  return pixels
}

describe('base v1 editor / actual canvas composition', () => {
  for (const screen of layoutScreensV1) for (const preset of nativeReferencePresets) {
    it(`${screen.width}x${screen.height} ${preset.page}: canvas matches the C golden`, () => {
      const document = createNativeReference(preset, screen.width, screen.height, 3)
      document.nativeReference!.timestamp = previewReferenceTimestamp.v1
      const scalePercent = layoutDefaultScaleQ8(screen, 'v1') * 100 / 256
      const oracle = golden.renders.find((item) => item.width === screen.width && item.height === screen.height
        && item.page === preset.page && item.edition === 'base' && item.scalePercent === scalePercent)!
      expect(oracle).toBeDefined()
      expect(createHash('sha256').update(renderCanvas(document)).digest('hex')).toBe(oracle.pixelSha256)
      expect(validateDeviceGuiDocument(document).filter((issue) => issue.level === 'error')).toEqual([])
    })
    it(`${screen.id} ${preset.page}: 4/5/6-row sample changes preserve the region composition`, () => {
      const document = createNativeReference(preset, screen.width, screen.height, 3)
      for (const rows of [4, 5, 6] as const) for (const weekStart of [0, 1] as const) {
        document.nativeReference = { ...document.nativeReference!, weekStart, timestamp: calendarRowExample(rows, weekStart, '23:59:59') }
        expect(createHash('sha256').update(renderCanvas(document)).digest('hex'))
          .toBe(createHash('sha256').update(nativeReferenceFrame(document).pixels).digest('hex'))
      }
    })
  }

  it('never hides the protected base footer and paints it last', () => {
    const document = createNativeReference(nativeReferencePresets[0], 400, 300, 3)
    const footer = document.widgets.at(-1)!
    footer.hidden = true
    document.widgets.reverse()
    expect(orderedVisibleWidgets(document).at(-1)?.id).toBe(footer.id)
    expect(renderCanvas(document)).toEqual(nativeReferenceFrame(document).pixels)
  })

  it('fixes base branding across every native page and restores it after Pro movement or removal', () => {
    for (const screen of layoutScreensV1) for (const preset of nativeReferencePresets) {
      const document = createNativeReference(preset, screen.width, screen.height, 3)
      const mark = document.widgets.find(widget => widget.nativeElement?.protected)!
      expect(mark).toBeDefined()
      const frame = { ...mark.frame }
      const pro = applyNativeEdition(document, 'pro')
      const proMark = pro.widgets.find(widget => widget.nativeElement?.key === mark.nativeElement!.key)!
      expect(proMark.nativeElement?.protected).toBe(false)
      proMark.frame = { ...proMark.frame, x: 0, y: 0 }; proMark.hidden = true
      const restored = applyNativeEdition(pro, 'base').widgets.find(widget => widget.id === mark.id)!
      expect(restored.frame).toEqual(frame); expect(restored.hidden).not.toBe(true)
      pro.widgets = pro.widgets.filter(widget => widget.id !== proMark.id)
      const recreated = applyNativeEdition(pro, 'base').widgets.find(widget => widget.nativeElement?.protected)!
      expect(recreated.frame).toEqual(frame)
    }
  })

  it.each(nativeReferencePresets.slice(0, 2))('$page decomposes both bars, preserving only calendar/clock as a large control', (preset) => {
    const document = createNativeReference(preset, 400, 300, 3)
    const texts = document.widgets.filter((widget) => widget.nativeElement?.type === 'text')
    for (const role of ['date', 'weekday', 'lunar-date', 'bluetooth', 'temperature', 'voltage', 'week-number', 'countdown']) {
      expect(texts.some((widget) => widget.nativeElement!.key === `${role}:0`), role).toBe(true)
    }
    expect(document.widgets.filter((widget) => widget.nativeElement?.type === preset.page)).toHaveLength(1)
    const restored = normalizeDeviceGuiDocument(JSON.parse(JSON.stringify(document)))
    expect(renderCanvas(restored)).toEqual(renderCanvas(document))
  })

  it('moves only the selected text pixels, never the rest of its old bar', () => {
    const document = createNativeReference(nativeReferencePresets[0], 400, 300, 3)
    const before = renderCanvas(document)
    const voltage = document.widgets.find((widget) => widget.nativeElement?.key === 'voltage:0')!
    const old = { ...voltage.frame }
    voltage.frame.x -= 3
    const moved = renderCanvas(document)
    let different = 0
    for (let i = 0; i < moved.length; i++) if (moved[i] !== before[i]) {
      different++
      const x = i % document.width, y = Math.floor(i / document.width)
      expect(x >= old.x - 3 && x < old.x + old.width && y >= old.y && y < old.y + old.height).toBe(true)
    }
    expect(different).toBeGreaterThan(0)
    voltage.frame.x += 3
    expect(renderCanvas(document)).toEqual(before)
  })

  it('preserves edits/deletions across month changes and supports calendar parameters', () => {
    let document = createNativeReference(nativeReferencePresets[0], 400, 300, 3)
    const weekday = document.widgets.find((widget) => widget.nativeElement?.key === 'weekday:0')!
    weekday.frame.x += 4
    document.widgets = document.widgets.filter((widget) => widget.nativeElement?.key !== 'temperature:0')
    document.nativeReference!.timestamp = calendarRowExample(6, 1, '14:28:00')
    document = refreshNativeElements(document)
    expect(document.widgets.find((widget) => widget.id === weekday.id)!.frame.x - weekday.nativeElement!.source.x).toBe(4)
    expect(document.widgets.some((widget) => widget.nativeElement?.key === 'temperature:0')).toBe(false)
    const before = renderCanvas(document)
    const calendar = document.widgets.find((widget) => widget.nativeElement?.type === 'calendar')!
    calendar.nativeElement!.settings = { showLunar: false, gridLines: false }
    expect(renderCanvas(document)).not.toEqual(before)
  })

  it('keeps a chosen WQY font and reports missing text; custom metrics follow real font size', () => {
    let document = createNativeReference(nativeReferencePresets[1], 400, 300, 3)
    const widget = document.widgets.find((item) => item.nativeElement?.key === 'bluetooth:0')!
    const oldHeight = widget.frame.height
    widget.nativeElement = { ...widget.nativeElement!, fontOverride: 'wqy12-lunar', fixedText: true }
    widget.data.text = '星期江花'
    document = refreshNativeElements(document)
    const edited = document.widgets.find((item) => item.id === widget.id)!
    expect(edited.frame.height).toBeGreaterThan(oldHeight)
    expect(edited.nativeElement!.fontOverride).toBe('wqy12-lunar')
    expect(textWidgetDelivery(edited)?.missingGlyphs).toEqual(['江', '花'])
    expect(edited.kind).toBe('text')
    expect(edited.nativeElement!.fixedText).toBe(true)
  })

  it('splits device information into independent labels, values, status and QR controls', () => {
    const document = createNativeReference(nativeReferencePresets[2], 400, 300, 3)
    for (const key of ['brand-title:0', 'info-key-BLE:0', 'info-value-BLE:0', 'info-value-MCU:0',
      'info-value-FW:0', 'temperature:0', 'voltage:0', 'battery:0', 'device-caption:0']) {
      expect(document.widgets.some((widget) => widget.nativeElement?.key === key), key).toBe(true)
    }
    expect(document.widgets.filter((widget) => widget.kind === 'qr-code')).toHaveLength(1)
    expect(document.widgets.some(widget=>widget.nativeElement?.key.includes('PCB'))).toBe(false)
  })

  it('changes only the selected control sample, not the rest of the page', () => {
    let document = createNativeReference(nativeReferencePresets[1], 400, 300, 3)
    const before = nativeReferenceScene(document)
    const clock = document.widgets.find((widget) => widget.nativeElement?.key === 'clock:0')!
    clock.nativeElement!.sample = { timestamp: calendarRowExample(6, 1, '23:59:59') }
    document = refreshNativeElements(document)
    const after = nativeReferenceScene(document)
    expect(after.parts.find((part) => part.element.key === 'clock:0')!.commands)
      .not.toEqual(before.parts.find((part) => part.element.key === 'clock:0')!.commands)
    for (const part of before.parts.filter((part) => part.element.key !== 'clock:0')) {
      expect(after.parts.find((item) => item.element.key === part.element.key)!.commands).toEqual(part.commands)
    }
  })

  it('resizes the calendar spacing without scaling its original text', () => {
    let document = createNativeReference(nativeReferencePresets[0], 400, 300, 3)
    const calendar = document.widgets.find((widget) => widget.nativeElement?.type === 'calendar')!
    const before = nativeReferenceScene(document).parts.find((part) => part.element.type === 'calendar')!
    document.widgets = document.widgets.map((widget) => widget === calendar
      ? { ...widget, frame: { ...widget.frame, width: widget.frame.width - 21, height: widget.frame.height - 6 }, nativeElement: { ...widget.nativeElement!, resized: true } } : widget)
    document = refreshNativeElements(document)
    const after = nativeReferenceScene(document).parts.find((part) => part.element.type === 'calendar')!
    expect(after.element.source.width).toBe(before.element.source.width - 21)
    expect(after.element.source.height).toBe(before.element.source.height - 6)
    const fonts = (part: typeof before) => part.commands.filter((command) => command.type === 'text').map((command) => [command.text, command.font, command.scale])
    expect(fonts(after)).toEqual(fonts(before))
  })

})
