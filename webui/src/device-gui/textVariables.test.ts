import { describe, expect, it } from 'vitest'
import { defaultDeviceGuiStyle, type DeviceGuiWidget } from './model'
import { parseTextTemplate, resolveTextTemplate, widgetPreviewSample } from './textVariables'
import { textWidgetDelivery } from './textDelivery'
import { previewTimestamp } from '../design/layoutPreviewTime'

function text(text: string, date = '2026-09-25'): DeviceGuiWidget {
  return { id: 'text', kind: 'text', variant: 'compact', frame: { x: 0, y: 0, width: 180, height: 24 },
    style: { ...defaultDeviceGuiStyle }, data: { text, sample: { timestamp: previewTimestamp(date, '14:28')! } } }
}
describe('text controls with bounded insertable variables', () => {
  it('combines literals and variables without interpreting HTML or expressions', () => {
    const widget = text('{{year}} 年 {{month}} 月 {{day}} 日 · {{weekday}}')
    expect(resolveTextTemplate(widget.data.text!, widget)).toBe('2026 年 9 月 25 日 · 星期五')
    expect(resolveTextTemplate('<b>{{ time }}</b> {{unknown}} {{1+1}}', widget)).toBe('<b>14:28</b> {{unknown}} {{1+1}}')
    expect(parseTextTemplate('第{{ week }}周')).toMatchObject([
      { type: 'literal', text: '第' }, { type: 'variable', id: 'week', possibleCharacters: '0123456789' }, { type: 'literal', text: '周' },
    ])
  })
  it('resolves lunar dates, year/zodiac, week and independent power samples', () => {
    const widget = text('{{lunar}} {{lunarYear}} [{{week}}周] {{temperature}} {{voltage}}')
    widget.data.sample = { ...widget.data.sample, temperature: 19, voltage: 3300 }
    expect(resolveTextTemplate(widget.data.text!, widget)).toBe('八月十五 丙午年马 [39周] 19℃ 3.3V')
    expect(resolveTextTemplate('{{week}}', text('', '2021-01-01'))).toBe('53')
    expect(resolveTextTemplate('{{date}}', text('', '2026-10-31'))).toBe('2026年10月31日')
  })
  it('keeps local samples independent through JSON save/restore', () => {
    const first = text('{{date}}', '2026-08-31'), second = text('{{date}}', '2026-10-31')
    const restored = JSON.parse(JSON.stringify(first)) as DeviceGuiWidget
    expect(resolveTextTemplate(restored.data.text!, restored)).toBe('2026年8月31日')
    expect(resolveTextTemplate(second.data.text!, second)).toBe('2026年10月31日')
    expect(widgetPreviewSample(first).now.getMonth()).toBe(7)
    expect(resolveTextTemplate('{{temperature}} {{voltage}}', first, { ...widgetPreviewSample(first), temperature: null, voltage: null })).toBe('--℃ --V')
  })
  it('calculates countdown by civil dates, including leap days and expired dates', () => {
    const widget = text('{{countdown}}天', '2028-02-28')
    widget.data.countdownDate = '2028-03-01'
    expect(resolveTextTemplate(widget.data.text!, widget)).toBe('2天')
    widget.data.countdownDate = '2028-02-27'
    expect(resolveTextTemplate(widget.data.text!, widget)).toBe('-1天')
  })
  it('checks the full bounded repertoire and current unrestricted name without changing the font', () => {
    const widget = text('{{date}} {{weekday}} {{temperature}} {{voltage}}')
    expect(textWidgetDelivery(widget)).toMatchObject({ mode: 'device-text', missingGlyphs: [] })
    widget.data.text = '{{deviceName}}'
    widget.data.sample!.deviceName = '春江花月夜'
    expect(textWidgetDelivery(widget)).toMatchObject({ mode: 'blocked', reason: 'unbounded-variable', missingGlyphs: ['江', '花'] })
    expect(widget.kind).toBe('text')
    expect(widget.style.fontId).toBe('wqy9-lunar')
  })
})
