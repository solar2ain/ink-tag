import { describe, expect, it } from 'vitest'
import { EditorCore, cellFrame, clone, drawPlan, kindOf, zoomAt, parseDocument, strokePixelRuns, type Document, type Kind } from '../../../../packages/inktag-editor-core/src/index'
import fixtures from './fixtures.json'

const make = () => new EditorCore(fixtures[0] as Document)
describe('portable fixed-pixel editor core', () => {
  it.each(['nw','ne','sw','se'] as const)('resizes from %s while anchoring the opposite corner and keeping typography', corner => {
    const e=new EditorCore({version:1,width:400,height:300,footerInset:0,widgets:[]})
    e.add('image');e.setFrame({x:60,y:60,width:140,height:100})
    const before=clone(e.doc), w=e.selected!, west=corner.endsWith('w'),north=corner.startsWith('n')
    const p={x:west?60:200,y:north?60:160}
    expect(e.isBlank(p,6)).toBe(false)
    e.down(p);e.move({x:p.x+20,y:p.y+15});e.up({x:p.x+20,y:p.y+15})
    expect(e.selected).toMatchObject({x:west?80:60,y:north?75:60,width:west?120:160,height:north?85:115,style:w.style})
    expect(() => parseDocument(JSON.stringify(e.doc))).not.toThrow()
    e.undo();expect(e.doc).toEqual(before)
    e.redo();expect(e.selected!.width).toBe(west?120:160)
  })
  it('fills the complete canvas without deleting the image, scaling text or making the mark editable',()=>{
    const e=make();e.add('image');e.setImage('data:image/png;base64,AA==')
    const before=clone(e.doc), image=e.selected!.image
    e.fillCanvasWithImage()
    expect(e.doc.footerInset).toBe(0)
    expect(e.selected).toMatchObject({x:0,y:0,width:400,height:300,image,imageFit:'cover',imageDither:'auto'})
    expect(() => parseDocument(JSON.stringify(e.doc))).not.toThrow()
    e.undo();expect(e.doc).toEqual(before)
  })
  it('refuses invalid image algorithms while retaining the immutable original image source',()=>{
    const e=make();e.add('image');e.setImage('data:image/png;base64,AA==')
    const image=e.selected!.image
    e.change(()=>{e.selected!.imageDither='bayer'})
    expect(e.selected!.image).toBe(image)
    expect(parseDocument(JSON.stringify(e.doc)).widgets.at(-1)?.imageDither).toBe('bayer')
    e.selected!.imageDither='arbitrary' as never
    expect(()=>parseDocument(JSON.stringify(e.doc))).toThrow()
  })
  it('stores a freehand stroke as one bounded, undoable document object', () => {
    const e = make(), count = e.doc.widgets.length
    e.addStroke([{ x: 20, y: 30 }, { x: 30, y: 30 }, { x: 30, y: 40 }], '#cf262d', 2)
    const stroke = e.doc.widgets.at(-1)!
    expect(stroke.kind).toBe('stroke')
    expect(strokePixelRuns(stroke).length).toBeGreaterThan(0)
    expect(() => parseDocument(JSON.stringify(e.doc))).not.toThrow()
    e.undo(); expect(e.doc.widgets).toHaveLength(count)
    e.redo(); expect(e.doc.widgets.at(-1)?.stroke?.points).toHaveLength(3)
  })
  it('rejects malformed or oversized stroke data before loading a draft', () => {
    const e = make(); e.addStroke([{ x: 10, y: 10 }], '#000000', 1)
    e.doc.widgets.at(-1)!.stroke!.points[0].x = -1
    expect(() => parseDocument(JSON.stringify(e.doc))).toThrow('Invalid widget')
  })
  it('keeps even-width strokes on the same pixel row while drawing and after commit', () => {
    const e = make(); e.addStroke([{ x: 20, y: 30 }], '#000000', 2)
    const stroke = e.doc.widgets.at(-1)!
    const rows = strokePixelRuns(stroke).flatMap(run => Array.from({ length: run.width }, (_, i) => ({ x: run.x + i + stroke.x, y: run.y + stroke.y })))
    expect(rows).toEqual(expect.arrayContaining([{ x: 19, y: 29 }, { x: 20, y: 29 }, { x: 19, y: 30 }, { x: 20, y: 30 }]))
    expect(rows).toHaveLength(4)
  })
  it('keeps selection through history, falling back safely when the selected cell no longer exists',()=>{
    const e=make();e.select('schedule');e.nudge(1,0);e.undo()
    expect(e.selection).toEqual({id:'schedule'});e.redo();expect(e.selection).toEqual({id:'schedule'})
    const row=e.selected!.cells.length
    e.structure('row');e.selection={id:'schedule',row,col:0};e.undo()
    expect(e.selection).toEqual({id:'schedule'})
    e.copy();e.paste();e.undo();expect(e.selection).toBeUndefined()
  })
  it('first gesture only selects the entire table, even when dragged', () => {
    const e = make(), before = clone(e.doc)
    e.down({ x: 30, y: 80 }); e.move({ x: 45, y: 90 }); e.up({ x: 45, y: 90 })
    expect(e.selection).toEqual({ id: 'schedule' }); expect(e.doc).toEqual(before); expect(e.canUndo).toBe(false)
  })
  it('second click selects a cell', () => {
    const e = make(); e.select('schedule'); e.down({ x: 90, y: 100 }); e.up({ x: 90, y: 100 })
    expect(e.selection).toEqual({ id: 'schedule', row: 1, col: 1 })
    e.updateCell('物理'); expect(e.selected!.cells[1][1].text).toBe('物理')
    e.undo(); expect(e.doc.widgets[1].cells[1][1].text).toBe('语文'); e.redo(); expect(e.doc.widgets[1].cells[1][1].text).toBe('物理')
  })
  it('drag commits exactly one undo operation and is bounded by the mark strip', () => {
    const e = make(); e.select('schedule'); const before = clone(e.doc)
    e.down({ x: 30, y: 80 }); e.move({ x: 35, y: 84 }); e.move({ x: 40, y: 900 }); e.up({ x: 40, y: 900 })
    expect(e.selected!.y + e.selected!.height).toBe(282)
    e.undo(); expect(e.doc).toEqual(before); expect(e.canUndo).toBe(false)
  })
  it('resizes cell padding, never font sizes or glyph scale', () => {
    const e = make(); e.select('schedule')
    e.down({ x: 384, y: 272 }); e.move({ x: 280, y: 220 }); e.up({ x: 280, y: 220 })
    expect(e.selected!.width).toBe(264); expect(e.selected!.height).toBe(164)
    expect(drawPlan(e.doc).slice(1).every(c => c.fontSize === 14)).toBe(true)
  })
  it('keeps integer shared cell edges without cumulative rounding gaps', () => {
    const e = make(), w = e.doc.widgets[1]; w.width = 359; w.height = 191
    for (let r = 0; r < 6; r++) for (let c = 0; c < 5; c++) {
      const a = cellFrame(w, r, c), b = cellFrame(w, r, c + 1)
      expect(a.x + a.width).toBe(b.x)
    }
    const last = cellFrame(w, 5, 5); expect(last.y + last.height).toBe(w.y + w.height)
  })
  it('local styles override default styles and survive resize', () => {
    const e = make(); e.select('schedule'); e.down({ x: 90, y: 100 }); e.up({ x: 90, y: 100 })
    e.updateStyle({ color: '#b32025', fontSize: 18 }); e.select('schedule'); e.updateStyle({ fontSize: 12 })
    expect(drawPlan(e.doc).find(c => c.text === '语文' && c.fontSize === 18)?.color).toBe('#b32025')
    expect(e.selected!.cells[1][2].style).toBeUndefined()
  })
  it('blank deselects and cancelled drags restore data', () => {
    const e = make(); e.select('schedule'); const before = clone(e.doc)
    e.down({ x: 30, y: 80 }); e.move({ x: 40, y: 84 }); e.cancel(); expect(e.doc).toEqual(before)
    e.down({ x: 1, y: 1 }); expect(e.selection).toBeUndefined()
  })
  it('identifies empty space for hit testing without treating widget padding as empty', () => {
    const e = make()
    expect(e.isBlank({ x: 1, y: 1 })).toBe(true)
    expect(e.isBlank({ x: -50, y: 350 })).toBe(true)
    expect(e.isBlank({ x: 17, y: 57 })).toBe(false)
    expect(e.isBlank({ x: 90, y: 100 })).toBe(false)
    expect(e.selection).toBeUndefined(); expect(e.canUndo).toBe(false)
  })
  it('protects the selected resize handle including its zoom-adjusted hit area', () => {
    const e = make(); e.select('schedule')
    expect(e.isBlank({ x: 388, y: 276 }, 6)).toBe(false)
    expect(e.isBlank({ x: 388, y: 276 }, 3)).toBe(true)
    expect(e.isBlank({ x: 396, y: 284 }, 12)).toBe(false)
    e.deselect(); expect(e.isBlank({ x: 388, y: 276 }, 6)).toBe(true)
  })
  it('copy/paste uses independent values and unique IDs', () => {
    const e = make(); e.select('schedule'); e.copy(); e.paste(); e.paste()
    expect(new Set(e.doc.widgets.map(w => w.id)).size).toBe(4)
    e.selected!.cells[0][0].text = 'test'; expect(e.doc.widgets[1].cells[0][0].text).toBe('节次')
  })
  it('does not store editable base branding in templates', () => {
    for (const doc of fixtures) expect(JSON.stringify(doc)).not.toContain('InkTag base')
  })
  it('redo is invalidated by a new edit', () => {
    const e = make(); e.select('schedule'); e.nudge(1, 0); e.undo(); e.select('schedule'); e.nudge(-1, 0); expect(e.canRedo).toBe(false)
  })
  it.each(['text', 'table', 'todo', 'list', 'image', 'qr', 'line'] as Exclude<Kind,'native' | 'stroke'>[])('adds a bounded %s control and undoes it', kind => {
    const e = make(); e.add(kind); const w = e.selected!
    expect(kindOf(w)).toBe(kind); expect(w.x).toBeGreaterThanOrEqual(0); expect(w.y + w.height).toBeLessThanOrEqual(282)
    e.undo(); expect(e.doc.widgets).toHaveLength(2)
  })
  it('inserts and removes rows/columns without losing local styles', () => {
    const e = make(); e.select('schedule'); e.selection = { id: 'schedule', row: 1, col: 1 }
    e.updateStyle({ fontSize: 18 }); expect(e.structure('row')).toBeUndefined()
    expect(e.selected!.cells).toHaveLength(7); expect(e.selected!.cells[1][1].style!.fontSize).toBe(18)
    e.structure('col'); expect(e.selected!.cells.every(row => row.length === 7)).toBe(true)
    e.structure('col', true); e.structure('row', true); expect(e.selected!.cells).toHaveLength(6)
  })
  it('refuses impossible rows and preserves data and undo state', () => {
    const e = new EditorCore(fixtures[1] as Document); e.select('schedule'); e.structure('row')
    const before = clone(e.doc); expect(e.structure('row')).toContain('空间不足'); expect(e.doc).toEqual(before)
  })
  it('never removes the final row or column', () => {
    const e = make(); e.add('table')
    e.structure('row', true); e.structure('row', true); expect(e.structure('row', true)).toContain('至少')
    e.structure('col', true); e.structure('col', true); expect(e.structure('col', true)).toContain('至少')
  })
  it('checks a todo without toggling its first selection gesture', () => {
    const e = make(); e.add('todo'); const w = e.selected!, p = { x: w.x + 7, y: w.y + 7 }
    e.deselect(); e.down(p); e.up(p); expect(e.selected!.cells[0][0].checked).toBeUndefined()
    e.down(p); e.up(p); expect(e.selected!.cells[0][0].checked).toBe(true)
    e.undo(); expect(e.doc.widgets.at(-1)!.cells[0][0].checked).toBeUndefined()
  })
  it('image selection is not a text cell; numeric resizing leaves font sizes intact', () => {
    const e = make(); e.add('image'); const w = e.selected!
    e.down({ x: w.x + 10, y: w.y + 10 }); e.up({ x: w.x + 10, y: w.y + 10 }); expect(e.selection?.row).toBeUndefined()
    e.setFrame({ width: 80, height: 40 }); expect(e.selected!.style.fontSize).toBe(14)
    e.setImage('https://example.com/image.png'); expect(e.selected!.image).toBeUndefined()
  })
  it('zoom retains the document point under its anchor and clamps the range', () => {
    const before = { x: 30, y: 10, zoom: 1 }, anchor = { x: 100, y: 80 }
    const after = zoomAt(before, anchor, 2)
    expect((anchor.x - after.x) / after.zoom).toBe(anchor.x - before.x)
    expect((anchor.y - after.y) / after.zoom).toBe(anchor.y - before.y)
    expect(zoomAt(before, anchor, 100).zoom).toBe(8)
  })
  it('roundtrips valid drafts and rejects malformed cells or external images', () => {
    const e = make(); expect(parseDocument(JSON.stringify(e.doc))).toEqual(e.doc)
    e.doc.widgets[0].cells = []; expect(() => parseDocument(JSON.stringify(e.doc))).toThrow()
    const another = make(); another.add('image'); another.selected!.image = 'https://example.com/a.png'
    expect(() => parseDocument(JSON.stringify(another.doc))).toThrow()
  })
})
