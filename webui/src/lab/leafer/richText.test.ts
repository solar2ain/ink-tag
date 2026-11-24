import { describe, expect, it } from 'vitest'
import { EditorCore, parseDocument } from '../../../../packages/inktag-editor-core/src/index'
import { nativeDocument } from './nativeBridge'
import { resolveLabText, richTextLayout, richTextRaster, richTextWarnings, measureTextRun } from './richTextRenderer'
import { previewTimestamp } from '../../design/layoutPreviewTime'
import { createEditionBadgeFrame, editionBadgeGeometry } from '../../device-gui/editionBadgeBitmap'
import { drawInkTagEditionBadge } from '../../device-gui/deviceGuiComponents'
import { DevicePixelGfx } from '../../device-gui/devicePixelGfx'
import { templateResolutionOptions } from '../../data/templates'

const countdown=()=>nativeDocument('calendar',400,300).widgets.find(w=>w.name==='倒计时')!
describe('independent rich text controls',()=>{
  it('does not apply device-font coverage validation to host-rendered pages',()=>{
    const w=countdown(),content={runs:[{text:'中文',fontId:'helv-bold-14-num' as const}]}
    expect(richTextWarnings(w,content,'static')).toEqual([])
    expect(richTextWarnings(w,content,'host-dynamic')).toEqual([])
    expect(richTextWarnings(w,content,'dynamic').length).toBeGreaterThan(0)
  })
  it.each(['calendar','clock'] as const)('%s peripheral text no longer depends on a native page',page=>{
    for(const {width,height} of templateResolutionOptions) {
      const doc=nativeDocument(page,width,height)
      expect(doc.widgets.filter(w=>w.native?.role==='text')).toEqual([])
      expect(doc.widgets.filter(w=>w.kind==='text').every(w=>Boolean(w.cells[0][0].richText))).toBe(true)
      for(const w of doc.widgets.filter(w=>w.kind==='text')) expect(w.cells[0][0].text,`${width}x${height} ${w.name}`).toContain('{{')
    }
  })
  it('keeps bold compact date numbers bound to variables, not the sample date or duplicate paint passes',()=>{
    for(const width of [200,240]) for(const page of ['calendar','clock'] as const) {
      const date=nativeDocument(page,width,width).widgets.find(w=>w.name==='日期')!
      expect(date.cells[0][0].text).toBe('{{year}}年{{month}}月{{day}}日')
      const runs=date.cells[0][0].richText!.runs
      expect(runs.filter(r=>r.style?.bold).map(r=>r.text)).toEqual(['{{year}}','{{month}}','{{day}}'])
      expect(runs.filter(r=>!r.style?.bold).map(r=>r.text)).toEqual(['年','月','日'])
      date.sample={...date.sample,timestamp:previewTimestamp('2027-12-31','12:00')!}
      expect(resolveLabText(date,date.cells[0][0].text)).toBe('2027年12月31日')
    }
  })
  it('shares the event name/days resolver, including expiry and same-day targets',()=>{
    const w=countdown(),text=w.cells[0][0].text
    expect(text).toBe('离{{countdownName}}{{countdownDays}}天')
    expect(resolveLabText(w,text)).toBe('离秋分3天')
    w.countdownTarget={name:'国庆',targetTimestamp:previewTimestamp('2026-10-01','00:00')!}
    expect(resolveLabText(w,text)).toBe('离国庆11天')
    w.sample!.timestamp=previewTimestamp('2026-10-01','23:59')!
    expect(resolveLabText(w,text)).toBe('离国庆0天')
    w.sample!.timestamp=previewTimestamp('2026-10-02','00:00')!
    expect(resolveLabText(w,text)).not.toContain('国庆')
    w.countdownTarget={name:'',targetTimestamp:0}
    expect(resolveLabText(w,text)).not.toContain('undefined')
  })
  it('splits lunar fields without duplicate units and changes only the local sample',()=>{
    const doc=nativeDocument('calendar',400,300),w=doc.widgets.find(w=>w.name==='农历日期')!
    const before=JSON.stringify(doc.widgets.filter(item=>item!==w))
    w.sample!.timestamp=previewTimestamp('2026-09-25','12:00')!
    expect(resolveLabText(w,'{{lunarYearName}}年{{lunarZodiac}} {{lunarMonth}}月{{lunarDay}}')).toBe('丙午年马 八月十五')
    expect(JSON.stringify(doc.widgets.filter(item=>item!==w))).toBe(before)
  })
  it('retains mixed styles, gaps, bindings and data through copy/undo/JSON',()=>{
    const core=new EditorCore(nativeDocument('calendar',400,300)),w=core.doc.widgets.find(w=>w.name==='倒计时')!
    core.select(w.id);const original=JSON.stringify(w)
    core.change(()=>{w.cells[0][0].richText!.runs[1].style!.color='#000000'})
    core.undo();expect(JSON.stringify(core.doc.widgets.find(item=>item.id===w.id))).toBe(original)
    core.redo();core.select(w.id);core.copy();core.paste()
    expect(core.selected!.cells).toEqual(core.doc.widgets.find(item=>item.id===w.id)!.cells)
    expect(()=>parseDocument(JSON.stringify(core.doc))).not.toThrow()
  })
  it('keeps glyph sizes and baseline relationships when the box shrinks; reports overflow',()=>{
    const w=countdown(),content=w.cells[0][0].richText!,before=JSON.stringify(content)
    const large=richTextLayout(w), small=richTextLayout({...w,width:30,height:8})
    expect(small.overflow).toBe(true);expect(large.overflow).toBe(false)
    expect(small.runs.map(r=>r.metrics)).toEqual(large.runs.map(r=>r.metrics))
    expect(large.runs.map(r=>r.y)).toEqual([14,14,14,14])
    expect(JSON.stringify(content)).toBe(before)
  })
  it('supports inherited device font and opaque white text for colored table cells',()=>{
    const w=countdown(),content={runs:[{text:'日期'}]},style={...w.style,fontId:'wqy12-lunar' as const,fontSize:16,color:'#ffffff'}
    const frame=richTextRaster(w,content,style,80,30)
    expect(frame.alpha.some(Boolean)).toBe(true)
    expect(frame.pixels.every(p=>p===0)).toBe(true)
    expect(measureTextRun(content.runs[0],style).height).toBeGreaterThan(10)
  })
  it('lays out explicit line breaks without stretching type',()=>{
    const w=countdown(),content={runs:[{text:'日期\n{{year}}',fontId:'wqy9-lunar' as const}],padding:2}
    const layout=richTextLayout(w,content,w.style,100,60)
    expect(layout.runs).toHaveLength(2)
    expect(layout.runs[1].y).toBeGreaterThan(layout.runs[0].y+10)
    expect(layout.overflow).toBe(false)
  })
  it('warns about unavailable glyphs without replacing WQY',()=>{
    const w=countdown(),content=w.cells[0][0].richText!
    content.runs[1].text='春江花月夜'
    expect(richTextWarnings(w,content)).toContain('江')
    expect(content.runs[1].fontId).toBe('wqy12-lunar')
  })
  it('rejects malformed run geometry/font and countdown data',()=>{
    const doc=nativeDocument('calendar',400,300),w=doc.widgets.find(w=>w.name==='倒计时')!
    for(const bad of [{gap:NaN},{scale:99999},{fontId:'unknown'},{style:{fontSize:-1}}]) {
      const copy=structuredClone(doc),run=copy.widgets.find(item=>item.id===w.id)!.cells[0][0].richText!.runs[0]
      Object.assign(run,bad);expect(()=>parseDocument(JSON.stringify(copy))).toThrow()
    }
    w.countdownTarget={name:'test',targetTimestamp:-1};expect(()=>parseDocument(JSON.stringify(doc))).toThrow()
  })
  it('new day/month values grow the text advance rather than squeezing glyphs',()=>{
    const w=nativeDocument('calendar',400,300).widgets.find(w=>w.name==='日期')!,first=richTextLayout(w)
    w.sample!.timestamp=previewTimestamp('2026-10-31','12:00')!
    const second=richTextLayout(w)
    expect(second.width).toBeGreaterThan(first.width)
    expect(resolveLabText(w,w.cells[0][0].text)).toBe('2026年10月31日')
  })
})
describe('shared InkTag badge',()=>{
  it.each(['base','pro'] as const)('%s has the same geometry and pixels as the WebUI badge',edition=>{
    for(const {width,height} of templateResolutionOptions) {
      const geometry=editionBadgeGeometry(width,height,edition),frame=createEditionBadgeFrame(edition,2,width,height)
      const gfx=new DevicePixelGfx(geometry.areaWidth,geometry.areaHeight)
      const bounds=drawInkTagEditionBadge(gfx,{x:0,y:0,width:geometry.areaWidth,height:geometry.areaHeight},{edition,colorCount:2,align:'right'})
      expect([frame.width,frame.height]).toEqual([geometry.width,geometry.height])
      for(let y=0;y<frame.height;y++) expect(frame.pixels.slice(y*frame.width,(y+1)*frame.width)).toEqual(gfx.frame.pixels.slice((y+bounds.y)*gfx.width+bounds.x,(y+bounds.y)*gfx.width+bounds.x+frame.width))
    }
  })
})
