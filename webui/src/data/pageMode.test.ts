import { describe, expect, it } from 'vitest'
import { canSendDocument, documentPageMode, hasDynamicContent, parseDocument, type Document } from '../../../packages/inktag-editor-core/src/index'
import { canSendSavedPage, savedPageMode, templatePageMode } from './pageMode'
import { pageTemplates } from './templates'
import { templateDocument } from '../lab/leafer/templateLibrary'
import { nativeDocument } from '../lab/leafer/nativeBridge'

const blank = (): Document => ({version:1,width:400,height:300,widgets:[]})
describe('static and device-dynamic pages',()=>{
  it.each(['dynamic','host-dynamic'] as const)('keeps declared %s mode even without runtime widgets',pageMode=>{
    const doc={...blank(),pageMode}
    expect(documentPageMode(parseDocument(JSON.stringify(doc)))).toBe(pageMode)
    expect(canSendDocument(doc)).toBe(false)
    expect(canSendSavedPage({document:JSON.stringify(doc)})).toBe(false)
  })
  it('sends ordinary static scenes but not device runtime scenes',()=>{
    expect(canSendDocument({...blank(),pageMode:'static'})).toBe(true)
    expect(documentPageMode(nativeDocument('clock',400,300))).toBe('dynamic')
    expect(canSendDocument(nativeDocument('calendar',400,300))).toBe(false)
  })
  it('infers older saved scenes without rewriting their JSON',()=>{
    const legacy=nativeDocument('calendar',400,300);delete legacy.pageMode
    const json=JSON.stringify(legacy),doc=parseDocument(json)
    expect(documentPageMode(doc)).toBe('dynamic');expect(JSON.stringify(doc)).toBe(json)
    expect(documentPageMode(blank())).toBe('static')
  })
  it('cannot reclassify a dynamic widget as a sendable static page',()=>{
    const doc={...nativeDocument('calendar',400,300),pageMode:'static' as const}
    expect(canSendDocument(doc)).toBe(false)
    expect(canSendSavedPage({document:JSON.stringify(doc)})).toBe(false)
  })
  it('detects device text tokens even across styled text runs',()=>{
    const doc=templateDocument('schedule-week')
    doc.widgets[0].cells[0][0]={text:'ignored',richText:{runs:[{text:'{{blue'},{text:'toothName}}'}]}}
    expect(hasDynamicContent(doc)).toBe(true);expect(canSendDocument(doc)).toBe(false)
    delete doc.pageMode;expect(documentPageMode(doc)).toBe('dynamic')
  })
  it('rejects invalid page types and unreadable saved scenes',()=>{
    expect(()=>parseDocument(JSON.stringify({...blank(),pageMode:'unknown'}))).toThrow('Invalid document')
    expect(savedPageMode({document:'broken'})).toBeUndefined()
    expect(canSendSavedPage({document:'broken'})).toBe(false)
  })
  it('classifies all built-in templates consistently and keeps static templates sendable',()=>{
    for(const template of pageTemplates) {
      const doc=templateDocument(template.id)
      expect(documentPageMode(doc),template.id).toBe(templatePageMode(template))
      expect(canSendDocument(doc),template.id).toBe(templatePageMode(template)==='static')
    }
  })
})
