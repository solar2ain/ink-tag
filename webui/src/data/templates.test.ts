import { describe, expect, it } from 'vitest'
import { pageTemplates, pageTemplateTarget, templateKindLabels, templateKindOrder, templateResolutionOptions, templatesForResolution, templatesForScreens } from './templates'
import { builtInScreenProfiles } from './screenProfiles'
import { matchesDocumentFilters, emptyDocumentFilters } from './documentLibrary'

describe('page template catalog', () => {
  it('orders static categories before device and host dynamic categories', () => {
    const firstDynamic = Math.min(...['calendar', 'clock', 'weather', 'date-card'].map(kind => templateKindOrder.indexOf(kind as typeof templateKindOrder[number])))
    for (const kind of ['schedule', 'todo', 'business', 'wifi', 'photo'] as const)
      expect(templateKindOrder.indexOf(kind)).toBeLessThan(firstDynamic)
  })
  it('omits photo presets below 400x300 without removing small weather pages',()=>{
    for(const [width,height] of [[152,152],[200,200],[212,104],[300,300],[300,400],[400,300],[800,480]]){
      const templates=templatesForResolution(width,height)
      expect(templates.some(t=>t.kind==='photo')).toBe(Math.max(width,height)>=400&&Math.min(width,height)>=300)
      expect(templates.some(t=>t.kind==='weather')).toBe(true)
    }
  })
  it('separates date placards and keeps only newspaper weather at every profile size and orientation', () => {
    const catalog=templatesForScreens(builtInScreenProfiles.map(profile=>profile.config))
    expect(new Set(catalog.map(t=>t.id)).size).toBe(catalog.length)
    expect(catalog.some(t=>t.sourceId==='weather-cards')).toBe(false)
    expect(templateKindOrder).toContain('date-card')
    expect(templateKindLabels['date-card']).toBe('日期牌')
    for(const profile of builtInScreenProfiles){
      const {width,height}=profile.config
      for(const [w,h] of [[width,height],[height,width]]){
        const matches=catalog.filter(t=>t.kind==='weather'&&t.width===w&&t.height===h)
        expect(matches.map(t=>t.name),`${w}x${h}`).toEqual(['报纸天气站'])
        expect(matches.every(t=>matchesDocumentFilters(t,{...emptyDocumentFilters,kind:'weather',pageMode:'host-dynamic'}))).toBe(true)
        const dates=catalog.filter(t=>t.kind==='date-card'&&t.width===w&&t.height===h)
        expect(dates.map(t=>t.name),`${w}x${h}`).toEqual(['日期牌'])
        expect(matchesDocumentFilters(dates[0],{...emptyDocumentFilters,kind:'date-card',pageMode:'host-dynamic'})).toBe(true)
        expect(matchesDocumentFilters(dates[0],{...emptyDocumentFilters,kind:'weather'})).toBe(false)
        expect(pageTemplateTarget(dates[0])).toContain('pageMode=host-dynamic')
      }
    }
  })
  it('keeps every template id and geometry unambiguous', () => {
    expect(new Set(pageTemplates.map((template) => template.id)).size).toBe(pageTemplates.length)
    for (const template of pageTemplates) {
      expect(template.width).toBeGreaterThan(0)
      expect(template.height).toBeGreaterThan(0)
      if (template.layout === 'portrait') expect(template.height).toBeGreaterThan(template.width)
      if (template.layout === 'landscape') expect(template.width).toBeGreaterThan(template.height)
      if (template.layout === 'square') expect(template.width).toBe(template.height)
    }
  })

  it('provides calendar, clock and schedule presets for common resolutions', () => {
    const required = ['250x128', '296x152', '400x300', '800x480']
    for (const kind of ['calendar', 'clock', 'schedule']) {
      const available = required.flatMap((size) => { const [width, height] = size.split('x').map(Number); return templatesForResolution(width, height).filter((template) => template.kind === kind).map((template) => `${template.width}x${template.height}`) })
      expect(available).toEqual(expect.arrayContaining(required))
    }
    expect(templateResolutionOptions.map((item) => item.id)).toEqual(expect.arrayContaining(required))
  })

  it('opens native presets on the canvas at their fixed dimensions', () => {
    const template = templatesForResolution(128, 250).find((item) => item.kind === 'calendar')!
    expect(pageTemplateTarget(template)).toBe('/create/canvas/calendar-native-250x128?width=250&height=128&pageMode=dynamic')
  })

  it('adapts semantic templates to a profile resolution without up-front duplicates', () => {
    const templates = templatesForResolution(104, 212)
    expect(templates.filter((template) => template.nativeAction).map((template) => template.deviceGuiPreset)).toEqual([
      'layout-v1-calendar', 'layout-v1-clock',
    ])
    expect(templates.filter((template) => template.nativeAction).every((template) => template.width === 212 && template.height === 104 && template.layout === 'landscape')).toBe(true)
    for (const kind of ['business', 'todo']) expect(templates.filter((template) => template.kind === kind).map((template) => template.layout)).toEqual(expect.arrayContaining(['portrait', 'landscape']))
    expect(templates.filter(t=>t.kind==='schedule').map(t=>t.layout)).toEqual(['landscape'])
  })

  it('never offers portrait timetable presets, including registered extra resolutions',()=>{
    for(const catalog of [pageTemplates,templatesForScreens(builtInScreenProfiles.map(p=>p.config)),templatesForResolution(176,480)]){
      expect(catalog.filter(t=>t.kind==='schedule'&&t.height>t.width)).toEqual([])
      expect(catalog.some(t=>t.kind==='schedule'&&t.width>=t.height)).toBe(true)
    }
  })

  it('passes the selected screen color into the editor route', () => {
    const template = pageTemplates.find((item) => item.id === 'calendar-native')!
    expect(pageTemplateTarget(template, { color: 3 })).toContain('&color=3')
  })

  it('opens other templates on the same canvas', () => {
    const template = pageTemplates.find((item) => item.id === 'wifi-guest')!
    expect(pageTemplateTarget(template)).toBe('/create/canvas/wifi-guest?width=128&height=250&pageMode=static')
  })

  it('retains native preset metadata without falling back to the previous editor', () => {
    for (const template of pageTemplates.filter((item) => item.nativeAction)) {
      expect(template.deviceGuiPreset).toBeTruthy()
      expect(pageTemplateTarget(template)).toMatch(/^\/create\/canvas\//)
    }
  })
  it('does not expose device-information pages in any creation preset', () => {
    for (const templates of [pageTemplates, templatesForResolution(152,152),templatesForResolution(400,300)]) {
      expect(templates.some(template => template.kind === 'device-info' || template.nativeAction === 'device-info')).toBe(false)
    }
  })
})
