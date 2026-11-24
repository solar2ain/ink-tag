import { describe, expect, it } from 'vitest'
import { renderToStaticMarkup } from 'react-dom/server'
import { EditorCore, drawPlan, parseDocument, type Document } from '../../../../packages/inktag-editor-core/src/index'
import { paletteForColor } from '../../image/processing'
import { colorModes, inkColor, inksFor } from './palette'
import { AppearancePanel } from './AppearancePanel'
import fixtures from './fixtures.json'
import { weatherDocument } from './weatherTemplate'

describe('lab screen palette and typography', () => {
  it.each(colorModes)('$label reuses the registered WebUI palette exactly', ({ value }) => {
    const inks = inksFor(value)
    expect(inks.map(ink => ink.name)).toEqual(paletteForColor(value).map(ink => ink.name))
    for (const ink of inks) expect(inkColor(ink.color, value)).toBe(ink.color)
    expect(inks.some(ink => ink.color === inkColor('#b32025', value))).toBe(true)
  })
  it('preserves source colors through switching, undo and saved drafts', () => {
    const core = new EditorCore(fixtures[0] as Document)
    core.select('schedule'); core.updateStyle({ color: '#137463', background: '#ebc219' })
    core.change(doc => { doc.colorMode = 10 })
    const original = JSON.stringify(core.doc.widgets)
    core.change(doc => { doc.colorMode = 1 })
    expect(JSON.stringify(core.doc.widgets)).toBe(original)
    core.undo(); expect(core.doc.colorMode).toBe(10)
    expect(parseDocument(JSON.stringify(core.doc))).toEqual(core.doc)
  })
  it('round-trips every device color mode, including gray, without remapping screen settings', () => {
    for (const { value } of colorModes) {
      const doc = { ...fixtures[0], colorMode: value } as Document
      expect(parseDocument(JSON.stringify(doc)).colorMode).toBe(value)
    }
  })
  it('lets header cell colors override the skin without changing adjacent cells', () => {
    const core = new EditorCore(fixtures[0] as Document)
    core.selection = { id: 'schedule', row: 0, col: 1 }
    core.updateStyle({ color: '#000000', background: '#ebc219', borderColor: '#2c61a5', fontSize: 17 })
    const cells = drawPlan(core.doc).filter(c => c.header)
    expect(cells[0].color).toBe('#ffffff')
    expect(cells[1]).toMatchObject({ color: '#000000', background: '#ebc219', borderColor: '#2c61a5', fontSize: 17 })
    core.resetCellStyle(); expect(drawPlan(core.doc).filter(c => c.header)[1].color).toBe('#ffffff')
  })
  it('rejects unsupported palette modes and malformed saved fills', () => {
    const doc = structuredClone(fixtures[0]) as Document
    expect(() => parseDocument(JSON.stringify({ ...doc, colorMode: 99 }))).toThrow()
    doc.widgets[0].style.background = 'url(https://example.com)'
    expect(() => parseDocument(JSON.stringify(doc))).toThrow()
  })
  it('defaults text to transparency and preserves explicit fills through save and undo',()=>{
    const core=new EditorCore(weatherDocument(400,300))
    const w=core.doc.widgets.find(w=>w.kind==='text')!
    delete w.style.background;core.select(w.id)
    expect(drawPlan(core.doc).find(c=>c.widgetId===w.id)?.background).toBe('transparent')
    expect(inkColor('transparent',2)).toBe('transparent')
    core.updateStyle({background:'#b32025'})
    expect(drawPlan(core.doc).find(c=>c.widgetId===w.id)?.background).toBe('#b32025')
    core.updateStyle({background:'transparent'})
    expect(parseDocument(JSON.stringify(core.doc))).toEqual(core.doc)
    const html=renderToStaticMarkup(<AppearancePanel core={core} act={fn=>fn()}/>)
    expect(html).toContain('aria-label="背景填充 · 透明" aria-pressed="true"')
    core.undo();expect(core.selected?.style.background).toBe('#b32025')
  })
  it('renders labelled six-color swatches, font sizing and alignment controls', () => {
    const core = new EditorCore(fixtures[0] as Document)
    core.doc.colorMode = 10; core.select('schedule')
    const html = renderToStaticMarkup(<AppearancePanel core={core} act={fn => fn()} />)
    expect(html).toContain('aria-label="文字颜色 · 蓝"')
    expect(html).toContain('aria-label="背景填充 · 黄"')
    expect(html).toContain('aria-label="表格线条 · 绿"')
    expect(html).toContain('aria-label="字号"')
    expect(html).toContain('aria-label="左对齐"')
  })
})
