import { describe, expect, it } from 'vitest'
import { parseHostScene, scenePlan } from '../../../../packages/inktag-editor-core/src/hostScene'
import { weatherDocument, weatherWidget, weatherCombination } from './weatherTemplate'
import { demoWeather, validateWeather, weatherFields, weatherCalendarLabels } from '../../../../packages/inktag-editor-core/src/weather'
import { templateResolutionOptions } from '../../data/templates'
import { EditorCore } from '../../../../packages/inktag-editor-core/src/index'
import { builtInScreenProfiles } from '../../data/screenProfiles'
import {drawSevenSegmentNumber} from '../../device-gui/deviceGuiComponents'
import {DevicePixelGfx,DEVICE_RED,DEVICE_WHITE} from '../../device-gui/devicePixelGfx'

describe('editable host weather scenes',()=>{
  it('keeps weather template controls from overlapping at all registered orientations',()=>{
    const sizes=new Map(builtInScreenProfiles.flatMap(({config:{width,height}})=>[[width,height],[height,width]]).map(size=>[size.join('x'),size]))
    for(const [width,height] of sizes.values())for(const variant of ['paper','cards','date-card']){
      const doc=weatherDocument(width,height,variant)
      for(let i=0;i<doc.widgets.length;i++)for(const b of doc.widgets.slice(i+1)){
        const a=doc.widgets[i]
        expect(Math.min(a.x+a.width,b.x+b.width)>Math.max(a.x,b.x)&&Math.min(a.y+a.height,b.y+b.height)>Math.max(a.y,b.y),`${variant} ${width}x${height}: ${a.name}/${b.name}`).toBe(false)
      }
    }
  })
  it('keeps date placards and grouped weather bounded on every panel orientation',()=>{
    const sizes=new Map(builtInScreenProfiles.flatMap(({config:{width,height}})=>[[width,height],[height,width]]).map(size=>[size.join('x'),size]))
    for(const [width,height] of sizes.values()){
      const doc=weatherDocument(width,height,'date-card')
      expect(()=>parseHostScene(JSON.stringify(doc))).not.toThrow()
      for(const w of [...doc.widgets,...weatherCombination(doc)]){
        expect(w.x,`${width}x${height} ${w.id}`).toBeGreaterThanOrEqual(0)
        expect(w.y).toBeGreaterThanOrEqual(0)
        expect(w.x+w.width).toBeLessThanOrEqual(width)
        expect(w.y+w.height,`${width}x${height} ${w.id}`).toBeLessThanOrEqual(height-18)
      }
      const plan=scenePlan(JSON.stringify(doc),demoWeather())
      expect(plan.widgets.find(w=>w.hostWeather?.field==='day-number')?.primitives?.length).toBeGreaterThan(0)
      expect(doc.widgets.find(w=>w.hostWeather?.field==='temperature')?.style.color).toBe('#000000')
    }
  })
  it('colors dates but never temperature in every weather design',()=>{
    for(const variant of ['paper','cards','date-card']){
      const doc=weatherDocument(400,300,variant)
      for(const w of doc.widgets.filter(w=>['date','day-number','lunar-date'].includes(w.hostWeather?.field??'')))expect(w.style.color).toBe('#b32025')
      expect(doc.widgets.find(w=>w.hostWeather?.field==='temperature')!.style.color).toBe('#000000')
      const primitives=scenePlan(JSON.stringify(doc),demoWeather()).widgets.find(w=>w.hostWeather?.field==='date')!.primitives!
      expect(primitives.filter(p=>p.kind==='text').map(p=>[p.text,p.color])).toEqual([['2026','#b32025'],['年','#000000'],['10','#b32025'],['月','#000000'],['18','#b32025'],['日','#000000']])
    }
  })
  it('reuses device-clock drawn digits exactly, including their beveled segment ends',()=>{
    const doc=weatherDocument(400,300,'date-card'),day=doc.widgets.find(w=>w.hostWeather?.field==='day-number')!
    for(const value of [1,3,8,18,28,31]){
      const data={...demoWeather(),observed_at:`2026-10-${String(value).padStart(2,'0')}T08:00`}
      const parts=scenePlan(JSON.stringify(doc),data).widgets.find(w=>w.id===day.id)!.primitives!
      const count=String(value).length,scale=Math.floor(Math.min((day.width/count-2)/11,(day.height-2)/20,day.style.fontSize/20))
      const gfx=new DevicePixelGfx(day.width,day.height)
      drawSevenSegmentNumber(gfx,value,Math.floor((day.width-count*(11*scale+2))/2),Math.floor((day.height-(20*scale+2))/2),scale,DEVICE_RED,DEVICE_WHITE,count)
      const actual=new Uint8Array(day.width*day.height)
      for(const p of parts){expect(p.kind).toBe('rect');if(p.kind!=='rect')continue;for(let y=p.y;y<p.y+p.height;y++)for(let x=p.x;x<p.x+p.width;x++)actual[y*day.width+x]=DEVICE_RED}
      expect(actual).toEqual(gfx.frame.pixels)
    }
  })
  it('rejects retired annual-progress documents instead of silently rendering them',()=>{
    const doc=weatherDocument(400,300)
    expect(()=>parseHostScene(JSON.stringify({...doc,calendarSource:{}}))).toThrow()
    expect(()=>parseHostScene(JSON.stringify({...doc,widgets:[...doc.widgets,{...doc.widgets[0],id:'retired',hostCalendar:{field:'year-progress'}}]}))).toThrow()
  })
  it('keeps every profile orientation and both designs bounded with a visible weather reading',()=>{
    const sizes=new Map(builtInScreenProfiles.flatMap(({config:{width,height}})=>[[width,height],[height,width]]).map(size=>[size.join('x'),size]))
    for(const [width,height] of sizes.values())for(const variant of ['paper','cards']){
      const doc=weatherDocument(width,height,variant)
      expect(()=>parseHostScene(JSON.stringify(doc)),`${width}x${height}`).not.toThrow()
      for(const w of doc.widgets){expect(w.x).toBeGreaterThanOrEqual(0);expect(w.y).toBeGreaterThanOrEqual(0);expect(w.x+w.width).toBeLessThanOrEqual(width);expect(w.y+w.height).toBeLessThanOrEqual(height-18)}
      for(const field of ['date','weekday','lunar-date','temperature','icon','summary','humidity'])expect(doc.widgets.some(w=>w.hostWeather?.field===field),`${width}x${height} ${field}`).toBe(true)
    }
  })
  it.each(templateResolutionOptions)('has bounded editable controls at $width x $height',({width,height})=>{
    const doc=weatherDocument(width,height)
    expect(parseHostScene(JSON.stringify(doc))).toEqual(doc)
    for(const w of doc.widgets){expect(w.x+w.width).toBeLessThanOrEqual(width);expect(w.y+w.height).toBeLessThanOrEqual(height)}
    const plan=scenePlan(JSON.stringify(doc),demoWeather())
    expect(plan.widgets.some(w=>w.primitives?.length)).toBe(true)
    expect(doc.widgets.every(w=>w.style.fontSize>=12)).toBe(true)
    expect(doc.widgets.some(w=>w.hostWeather?.field==='attribution')).toBe(false)
    const rendered=plan.widgets.flatMap(w=>w.primitives??[]).filter(p=>p.kind==='text')
    expect(rendered.every(p=>p.size>=12)).toBe(true)
    expect(rendered.map(p=>p.text).join(' ')).not.toMatch(/示例|DEMO|Open-Meteo|CC BY|扩大图表/)
    for(const field of ['date','weekday','lunar-date'])expect(doc.widgets.some(w=>w.hostWeather?.field===field)).toBe(true)
    expect(doc.widgets.some(w=>w.hostWeather?.field==='updated')).toBe(false)
  })
  it('all fields retain bindings through copy, resize, undo and JSON',()=>{
    const core=new EditorCore(weatherDocument(400,300))
    for(const field of weatherFields){const w=weatherWidget(core.doc,field);core.change(doc=>doc.widgets.push(w));core.select(w.id);core.setFrame({x:10,y:10});core.copy();core.paste();expect(core.selected!.hostWeather?.field).toBe(field);core.undo();expect(parseHostScene(JSON.stringify(core.doc))).toBeDefined()}
  })
  it('uses a restrained paper hierarchy with a large temperature, icon and readable chart',()=>{
    for(const [width,height] of [[400,300],[300,400],[768,552]]){
      const doc=weatherDocument(width,height),plan=scenePlan(JSON.stringify(doc),demoWeather())
      const temperature=doc.widgets.find(w=>w.hostWeather?.field==='temperature')!
      expect(temperature.style.fontSize).toBeGreaterThanOrEqual(65)
      expect(doc.widgets.some(w=>w.hostWeather?.field==='icon')).toBe(true)
      const chart=plan.widgets.find(w=>w.hostWeather?.field==='temperature-chart')!
      expect(chart.height).toBeGreaterThanOrEqual(72)
      expect(chart.primitives!.filter(p=>p.kind==='line'&&p.color==='#b32025')).toHaveLength(23)
      expect(chart.primitives!.filter(p=>p.kind==='rect').every(p=>p.color==='#000000')).toBe(true)
      expect(doc.widgets.filter(w=>w.hostWeather?.field!=='temperature').every(w=>!w.style.bold)).toBe(true)
      const forecast=plan.widgets.find(w=>w.hostWeather?.field==='forecast')
      for(const p of forecast?.primitives??[])if(p.kind==='text')expect(p.height).toBeGreaterThanOrEqual(p.size*1.3)
    }
  })
  it('fits signed temperature extremes without truncating the number or unit',()=>{
    for(const side of [152,200,300])for(const value of [-100,-12,0,22,100]){
      const doc=weatherDocument(side,side===300?400:side)
      const widget=scenePlan(JSON.stringify(doc),{...demoWeather(),temperature:value}).widgets.find(w=>w.hostWeather?.field==='temperature')!
      const primitive=widget.primitives![0]!
      expect(primitive.kind).toBe('text')
      if(primitive.kind==='text')expect(primitive.text.length*primitive.size*.65).toBeLessThanOrEqual(widget.width+.01)
    }
  })
  it('binds year/date, weekday and lunar date to provider-local dates, not the host time',()=>{
    expect(weatherCalendarLabels({observed_at:'2026-10-18T23:59'})).toEqual({date:'2026年10月18日',weekday:'星期日','lunar-date':'九月初九'})
    expect(weatherCalendarLabels({observed_at:'2026-02-17T00:01'})).toEqual({date:'2026年2月17日',weekday:'星期二','lunar-date':'正月初一'})
    expect(weatherCalendarLabels({observed_at:'2023-03-22T00:00'})['lunar-date']).toBe('闰二月初一')
  })
  it('shares deterministic chart coordinates and never changes authoring data',()=>{
    const doc=weatherDocument(400,300),before=JSON.stringify(doc),data=demoWeather()
    const plan=scenePlan(before,data),chart=plan.widgets.find(w=>w.hostWeather?.field==='temperature-chart')!
    expect(chart.primitives!.filter(p=>p.kind==='line').length).toBeGreaterThan(20)
    expect(chart.primitives!.filter(p=>p.kind==='rect')).toHaveLength(24)
    expect(chart.primitives!.filter(p=>p.kind==='text').map(p=>p.text).join(' ')).toContain('湿度 %')
    expect(JSON.stringify(doc)).toBe(before)
    expect(scenePlan(before,data)).toEqual(plan)
  })
  it('does not invent hourly humidity for snapshots that only contain current humidity',()=>{
    const data=demoWeather();data.hours=data.hours.map(({humidity,...hour})=>hour)
    const chart=scenePlan(JSON.stringify(weatherDocument(400,300)),data).widgets.find(w=>w.hostWeather?.field==='temperature-chart')!
    expect(chart.primitives!.filter(p=>p.kind==='rect')).toHaveLength(24)
    expect(chart.primitives!.filter(p=>p.kind==='line')).toHaveLength(1)
    expect(chart.primitives!.filter(p=>p.kind==='text').map(p=>p.text).join(' ')).not.toContain('湿度')
    expect(()=>validateWeather({...data,hours:data.hours.map(h=>({...h,humidity:101}))})).toThrow()
  })
  it('rejects malformed snapshots and unimplemented device controls',()=>{
    const doc=weatherDocument(400,300)
    expect(()=>validateWeather({...demoWeather(),hours:[]})).toThrow()
    doc.widgets[0].cells[0][0].text='{{voltage}}'
    expect(()=>parseHostScene(JSON.stringify(doc))).toThrow()
  })
})
