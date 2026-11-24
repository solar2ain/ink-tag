import { describe, expect, it } from 'vitest'
import { documentKind, documentLayout, documentResolution, documentResolutions, documentMode, emptyDocumentFilters, matchesDocumentFilters } from './documentLibrary'

describe('fixed-resolution page and template library', () => {
  const record = { name: '一周课表', kind: 'schedule' as const, width: 104, height: 212, templateId: 'schedule-week-104x212' }
  it('shares category inference for generated presets, saved pages and custom works', () => {
    expect(documentKind({ templateId: 'schedule-week-712x424' })).toBe('schedule')
    expect(documentKind({ templateId: 'blank' })).toBe('custom')
    expect(documentKind({ ...record, kind: 'todo' })).toBe('todo')
    expect(documentKind({ templateId: 'unknown' })).toBe('custom')
  })
  it('filters dimensions independently of orientation without migrating the document', () => {
    expect(documentResolution(record)).toBe('212x104')
    expect(documentLayout(record)).toBe('portrait')
    expect(documentLayout({ width: 152, height: 152 })).toBe('square')
    expect(matchesDocumentFilters(record, { ...emptyDocumentFilters, resolution: '212x104', layout: 'portrait' })).toBe(true)
    expect(matchesDocumentFilters(record, { ...emptyDocumentFilters, layout: 'landscape' })).toBe(false)
    expect(record).toEqual({ name: '一周课表', kind: 'schedule', width: 104, height: 212, templateId: 'schedule-week-104x212' })
  })
  it('classifies saved date placards in their own tab without changing saved pages',()=>{
    const saved={name:'我的日期',kind:'weather' as const,templateId:'weather-date-card-200x200',width:200,height:200,
      document:JSON.stringify({version:1,width:200,height:200,widgets:[],pageMode:'host-dynamic'})}
    const before=JSON.stringify(saved)
    expect(matchesDocumentFilters(saved,{...emptyDocumentFilters,kind:'date-card'})).toBe(true)
    expect(matchesDocumentFilters(saved,{...emptyDocumentFilters,kind:'weather'})).toBe(false)
    expect(JSON.stringify(saved)).toBe(before)
    expect(documentKind({...saved,kind:'custom'})).toBe('custom')
    expect(documentKind({kind:'weather',templateId:'weather-cards-200x200'})).toBe('weather')
    expect(documentMode({kind:'date-card'})).toBe('host-dynamic')
  })
  it('combines type, size, direction and trimmed text search', () => {
    expect(matchesDocumentFilters(record, { ...emptyDocumentFilters, kind: 'schedule', query: ' 课表 ' })).toBe(true)
    expect(matchesDocumentFilters(record, { ...emptyDocumentFilters, query: '104 × 212' })).toBe(true)
    expect(matchesDocumentFilters(record, { ...emptyDocumentFilters, kind: 'todo' })).toBe(false)
    expect(matchesDocumentFilters(record, { ...emptyDocumentFilters, resolution: '400x300' })).toBe(false)
    expect(matchesDocumentFilters(record, { ...emptyDocumentFilters, query: '日历' })).toBe(false)
  })
  it('deduplicates landscape and portrait sizes and orders by pixel area', () => {
    expect(documentResolutions([record, { width: 212, height: 104 }, { width: 400, height: 300 }, { width: 152, height: 152 }])).toEqual(['212x104', '152x152', '400x300'])
    expect(documentResolutions([])).toEqual([])
  })
  it.each(['static','dynamic','host-dynamic'] as const)('filters saved %s pages and templates independently of content category',pageMode=>{
    const saved={...record,document:JSON.stringify({version:1,width:104,height:212,widgets:[],pageMode}),nativeAction:'calendar'}
    expect(documentMode(saved)).toBe(pageMode)
    expect(matchesDocumentFilters(saved,{...emptyDocumentFilters,pageMode,kind:'schedule',layout:'portrait',resolution:'212x104'})).toBe(true)
    for(const other of ['static','dynamic','host-dynamic'] as const) {
      expect(matchesDocumentFilters(saved,{...emptyDocumentFilters,pageMode:other})).toBe(other===pageMode)
    }
    expect(matchesDocumentFilters(saved,{...emptyDocumentFilters,pageMode,kind:'todo'})).toBe(false)
  })
  it('uses preset metadata or valid legacy content and never labels an unreadable source static',()=>{
    expect(documentMode({nativeAction:'calendar'})).toBe('dynamic')
    expect(documentMode({nativeAction:'clock'})).toBe('dynamic')
    expect(documentMode({})).toBe('static')
    const saved={...record,document:JSON.stringify({version:1,width:104,height:212,widgets:[]})}
    expect(documentMode(saved)).toBe('static')
    expect(matchesDocumentFilters({...saved,document:'broken'},{...emptyDocumentFilters,pageMode:'static'})).toBe(false)
    expect(matchesDocumentFilters({...saved,document:'broken'},emptyDocumentFilters)).toBe(true)
  })
})
