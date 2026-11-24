import { describe, expect, it } from 'vitest'
import { labTemplates, templateDocument } from './templateLibrary'
import { nativeDocument, nativeRaster, refreshNativeWidget } from './nativeBridge'
import { nativeReferenceFrame, createNativeReference, nativeReferencePresets } from '../../device-gui/nativeReference'
import { EditorCore, locked, parseDocument } from '../../../../packages/inktag-editor-core/src/index'
import { templateResolutionOptions } from '../../data/templates'
import { templatesForResolution } from '../../data/templates'
import { calendarRowExample, previewTimestamp } from '../../design/layoutPreviewTime'
import { resolveLabText } from './textBridge'

describe('shared WebUI template migration', () => {
  it('gives small focus checklists room for complete Chinese items instead of clipped columns',()=>{
    for(const [width,height] of [[152,152],[200,200],[212,104],[250,128]]){
      const t=templatesForResolution(width,height).find(t=>t.sourceId==='todo-focus'&&t.width===width)!
      const doc=templateDocument(t.id,width,height),list=doc.widgets.find(w=>w.kind==='todo')!
      for(const row of list.cells)expect(list.width-list.style.fontSize-12).toBeGreaterThanOrEqual(row[0].text.length*list.style.fontSize)
      if(width===height)expect(list.y+list.height).toBeLessThanOrEqual(doc.widgets.find(w=>w.cells[0][0].text==='随手记')!.y)
    }
  })
  it('keeps static preset controls separate at every registered resolution',()=>{
    for(const t of labTemplates.filter(t=>!t.nativeAction&&!t.layoutData?.startsWith('weather-'))){
      const doc=templateDocument(t.id)
      for(let i=0;i<doc.widgets.length;i++)for(const b of doc.widgets.slice(i+1)){
        const a=doc.widgets[i]
        const overlap=Math.min(a.x+a.width,b.x+b.width)>Math.max(a.x,b.x)&&Math.min(a.y+a.height,b.y+b.height)>Math.max(a.y,b.y)
        expect(overlap,`${t.id}: ${a.name} / ${b.name}`).toBe(false)
      }
    }
  })
  it('keeps non-native preset text readable without shrinking small screens to six pixels',()=>{
    for(const t of labTemplates.filter(t=>!t.nativeAction)){
      const doc=templateDocument(t.id)
      for(const w of doc.widgets.filter(w=>w.kind!=='line')){
        expect(w.style.fontSize,t.id).toBeGreaterThanOrEqual(Math.max(doc.width, doc.height) <= 300 ? 10 : 12)
        if(w.kind==='text'&&!w.hostWeather)expect(w.height,t.id).toBeGreaterThanOrEqual(w.style.fontSize*1.2)
      }
    }
  })
  it('opens generated profile templates without changing their fixed resolution', () => {
    const template = templatesForResolution(212, 104).find(item => item.kind === 'schedule')!
    const doc = templateDocument(template.id, template.width, template.height)
    expect([doc.width, doc.height]).toEqual([212, 104])
    expect(() => parseDocument(JSON.stringify(doc))).not.toThrow()
  })
  it('keeps compact timetables readable and gives larger layouts more class rows', () => {
    const compact = templateDocument(templatesForResolution(212, 104).find(item => item.kind === 'schedule')!.id, 212, 104)
    const large = templateDocument(templatesForResolution(400, 300).find(item => item.kind === 'schedule')!.id, 400, 300)
    const shortTable = compact.widgets.find(item => item.kind === 'table')!
    const largeTable = large.widgets.find(item => item.kind === 'table')!
    expect(shortTable.style).toMatchObject({ fontFamily: 'InkTag Pixel Fusion', fontSize: 10 })
    expect(largeTable.style).toMatchObject({ fontFamily: 'InkTag Pixel Body', fontSize: 12 })
    expect(shortTable.cells).toHaveLength(4)
    expect(largeTable.cells).toHaveLength(6)
    expect(shortTable.cells[0]).toHaveLength(6)
    expect(large.widgets.find(item => item.name === '班级信息')?.cells[0][0].text).toBe('三年二班')
    expect(large.widgets.some(item => item.cells.flat().some(cell => cell.text.includes('{{week}}')))).toBe(false)
    const square = templateDocument('schedule-week-152x152',152,152).widgets.find(item => item.kind === 'table')!
    expect(square.cells).toHaveLength(6)
    expect(square.cells[0]).toHaveLength(2)
    expect(square.cells[5][0].style?.background).toBe('#cf262d')
    expect(() => parseDocument(JSON.stringify(large))).not.toThrow()
  })
  it('uses the available page area without putting editable content into the edition footer', () => {
    for (const [width, height] of [[400,300],[300,400],[212,104],[128,250],[152,152]] as const) {
      for (const template of templatesForResolution(width,height).filter(item => !item.nativeAction)) {
        const doc = templateDocument(template.id,width,height)
        expect(() => parseDocument(JSON.stringify(doc)), template.id).not.toThrow()
        expect(doc.widgets.filter(widget => widget.y + widget.height > doc.height - 18).map(widget => [widget.kind, widget.y, widget.height]), template.id).toEqual([])
      }
    }
    const todo = templateDocument('todo-today-400x300',400,300)
    const list = todo.widgets.find(widget => widget.kind === 'todo')!
    expect(list.y).toBeLessThanOrEqual(300 * .16)
    expect(list.height).toBeGreaterThanOrEqual(300 * .7)
    expect(list.cells[2][0].checked).toBe(true)
    const schedule = templateDocument('schedule-week',400,300).widgets.find(widget => widget.kind === 'table')!
    expect(schedule.y).toBeLessThanOrEqual(300 * .16)
    expect(schedule.height).toBeGreaterThanOrEqual(300 * .7)
    expect(schedule.cells[0][5].style?.background).toBe('#cf262d')
    expect(schedule.cells[3][5].style?.background).toBe('#cf262d')
    expect(schedule.style.borderColor).toBe('#191919')
  })
  it('keeps portrait business-card headings separate from the QR and contact rows', () => {
    const doc = templateDocument('business-minimal-300x400',300,400)
    const [latinName, chineseName, role, rule, qr, url, email] = doc.widgets
    expect(latinName.y + latinName.height).toBeLessThanOrEqual(chineseName.y)
    expect(chineseName.y + chineseName.height).toBeLessThanOrEqual(role.y)
    expect(role.y + role.height).toBeLessThanOrEqual(rule.y)
    expect(rule.y + rule.height).toBeLessThanOrEqual(qr.y)
    expect(qr.y + qr.height).toBeLessThanOrEqual(url.y)
    expect(url.y + url.height).toBeLessThanOrEqual(email.y)
    const square = templateDocument('business-minimal-152x152',152,152)
    expect(square.widgets).toHaveLength(3)
    expect(square.widgets.find(widget => widget.kind === 'qr')?.width).toBeGreaterThanOrEqual(100)
    const wifi = templateDocument('wifi-guest-152x152',152,152)
    expect(wifi.widgets).toHaveLength(2)
    expect(wifi.widgets.find(widget => widget.kind === 'qr')?.width).toBeGreaterThanOrEqual(100)
  })
  it('imports all declarative templates with bounded controls and valid persisted data', () => {
    for (const t of labTemplates.filter(t => !t.nativeAction)) {
      const doc = templateDocument(t.id)
      expect(() => parseDocument(JSON.stringify(doc)), t.id).not.toThrow()
      expect(doc.widgets.length, t.id).toBeGreaterThan(0)
    }
  })
  it.each(nativeReferencePresets)('$page templates match existing rendered pixels at all common sizes', preset => {
    for (const {width,height} of templateResolutionOptions) {
      const doc = nativeDocument(preset.page,width,height)
      expect(() => parseDocument(JSON.stringify(doc)), `${preset.page} ${width}x${height}`).not.toThrow()
      const result = new Uint8Array(width*height)
      for (const w of doc.widgets) {
        const raster = nativeRaster(w,doc)!
        for (let y=0;y<raster.height;y++) for (let x=0;x<raster.width;x++) if (raster.pixels[y*raster.width+x]) result[(w.y+y)*width+w.x+x] = raster.pixels[y*raster.width+x]
      }
      const expected = nativeReferenceFrame(createNativeReference(preset,width,height,3))
      let delta = 0; for (let i=0;i<result.length;i++) if (result[i] !== expected.pixels[i]) delta++
      expect(delta, `${preset.page} ${width}x${height}`).toBe(0)
    }
  }, 60000)
  it('protects the edition mark from keyboard, frame edits, resizing and deletion', () => {
    const core = new EditorCore(nativeDocument('calendar',400,300))
    const mark = core.doc.widgets.find(locked)!, before = JSON.stringify(mark)
    core.select(mark.id); core.nudge(10,10); core.setFrame({ x:0,width:200 }); core.remove()
    core.down({x:mark.x+1,y:mark.y+1}); core.move({x:0,y:0}); core.up({x:0,y:0})
    expect(JSON.stringify(core.doc.widgets.find(w => w.id === mark.id))).toBe(before)
    expect(core.canUndo).toBe(false)
  })
  it('keeps calendar preview samples local and supports real six-row months after resizing', () => {
    const doc = nativeDocument('calendar',400,300), calendar = doc.widgets.find(w => w.native?.role === 'calendar')!
    const other = JSON.stringify(doc.widgets.filter(w => w !== calendar))
    calendar.native!.sample!.timestamp = calendarRowExample(6,1,'06:45:12')
    calendar.width -= 20; calendar.height -= 8
    const raster = nativeRaster(calendar,doc)!
    expect(raster.width).toBe(calendar.width); expect(raster.pixels.some(p=>p!==0)).toBe(true)
    expect(JSON.stringify(doc.widgets.filter(w => w !== calendar))).toBe(other)
  })
  it('updates clock bounds when changing from a narrow leading 1 to a wider 0', () => {
    const doc = nativeDocument('clock',400,300), clock = doc.widgets.find(w => w.native?.role === 'clock')!
    const width = clock.width
    clock.native!.sample!.timestamp = previewTimestamp('2026-09-20','07:45:12')!
    refreshNativeWidget(clock,doc)
    expect(clock.width).toBeGreaterThan(width)
    const raster = nativeRaster(clock,doc)!
    expect(raster.pixels.some((p,i) => p !== 0 && i % raster.width > raster.width - 20)).toBe(true)
    expect(() => parseDocument(JSON.stringify(doc))).not.toThrow()
  })
  it('shares text variables while retaining per-widget preview dates', () => {
    const core = new EditorCore(templateDocument('schedule-week')); core.add('text')
    const w = core.selected!; w.sample = { timestamp: previewTimestamp('2026-10-31','06:45:12')! }
    expect(resolveLabText(w,'{{year}}/{{month}}/{{day}} {{time}}')).toBe('2026/10/31 06:45')
    expect(w.cells[0][0].text).toBe('输入文字')
  })
})
