import { describe,expect,it } from 'vitest'
import { canSendDocument, documentPageMode, EditorCore, parseDocument, type Document } from '../../../../packages/inktag-editor-core/src/index'
import { createSlotCarousel,parseSlotNumbers } from './slotCarousel'

const blank=(width=400,height=300):Document=>({version:1,width,height,pageMode:'dynamic',widgets:[]})
describe('image-slot carousel authoring',()=>{
  it.each([[152,152],[200,200],[212,104],[250,128],[296,152],[400,300],[800,480]])('fits a bounded image region at %ix%i', (width,height)=>{
    const doc=blank(width,height),w=createSlotCarousel(doc)
    expect(w.kind).toBe('image');expect(w.image).toBeUndefined()
    expect(w.imageSlots).toEqual({slotIds:[],intervalSeconds:300})
    expect(w.x+w.width).toBeLessThanOrEqual(width);expect(w.y+w.height).toBeLessThanOrEqual(height-18)
    expect(()=>parseDocument(JSON.stringify({...doc,widgets:[w]}))).not.toThrow()
  })
  it('uses existing zero-based IDs while preserving the user playlist order',()=>{
    expect(parseSlotNumbers('1, 3，2\n255')).toEqual([0,2,1,254])
    expect(parseSlotNumbers('1, 2, 1')).toEqual([0,1,0])
    expect(parseSlotNumbers('')).toEqual([])
    for(const value of ['0','256','1.5','-1','1e2','abc'])expect(parseSlotNumbers(value),value).toBeUndefined()
  })
  it('persists independent parameters and supports ordinary undo/copy without copying any image pixels',()=>{
    const core=new EditorCore(blank()),w=createSlotCarousel(core.doc)
    core.change(doc=>doc.widgets.push(w));core.select(w.id)
    core.change(()=>{w.imageSlots={slotIds:[0,2,1],intervalSeconds:60}})
    core.undo();expect(core.selected!.imageSlots!.slotIds).toEqual([])
    core.redo();expect(core.selected!.imageSlots!.slotIds).toEqual([0,2,1])
    core.copy();core.paste();expect(core.doc.widgets[1].id).not.toBe(w.id)
    core.doc.widgets[1].imageSlots!.slotIds.push(3)
    expect(core.doc.widgets[0].imageSlots!.slotIds).toEqual([0,2,1])
    const restored=parseDocument(JSON.stringify(core.doc))
    expect(restored.widgets[0].imageSlots).toEqual({slotIds:[0,2,1],intervalSeconds:60})
  })
  it('refuses static insertion and never makes either dynamic type sendable',()=>{
    expect(()=>createSlotCarousel({...blank(),pageMode:'static'})).toThrow('图槽轮播仅限动态页面')
    const doc=blank();doc.widgets.push(createSlotCarousel(doc))
    doc.pageMode='host-dynamic';expect(canSendDocument(doc)).toBe(false)
    doc.pageMode='static';expect(canSendDocument(doc)).toBe(false)
    delete doc.pageMode;expect(documentPageMode(doc)).toBe('dynamic')
  })
  it.each([
    {slotIds:[-1],intervalSeconds:300}, {slotIds:[255],intervalSeconds:300},
    {slotIds:[1.5],intervalSeconds:300}, {slotIds:[0],intervalSeconds:29},
    {slotIds:[0],intervalSeconds:86401}, {slotIds:[0],intervalSeconds:30.5},
    {slotIds:Array(256).fill(0),intervalSeconds:300},
  ])('refuses invalid persisted carousel parameters: %j',imageSlots=>{
    const doc=blank();doc.widgets.push({...createSlotCarousel(doc),imageSlots})
    expect(()=>parseDocument(JSON.stringify(doc))).toThrow('Invalid widget')
  })
})
