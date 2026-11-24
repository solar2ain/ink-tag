import { afterEach, describe, expect, it, vi } from 'vitest'
import { EditorCore, clone, parseDocument, type Document } from '../../../../packages/inktag-editor-core/src/index'
import { TouchViewport } from '../../../../packages/inktag-editor-core/src/touchViewport'
import { labTemplates, templateDocument } from './templateLibrary'
import { buildClientPageTemplates } from '../../../../packages/inktag-client-core/src/templates'
import { U8g2Font } from '../../vendor/u8g2-js/index.js'
import { deviceFontData } from '../../device-gui/fonts.generated'
import fixtures from './fixtures.json'

afterEach(()=>vi.unstubAllGlobals())
describe('miniapp editor pilot',()=>{
  it('opens every existing miniapp template using the Web demo catalog',()=>{
    for(const template of buildClientPageTemplates()){
      expect(labTemplates.some(t=>t.id===template.id),template.id).toBe(true)
      const doc=templateDocument(template.id)
      expect([doc.width,doc.height]).toEqual([template.width,template.height])
      expect(parseDocument(JSON.stringify(doc))).toEqual(doc)
    }
  })
  it('first touch drag only selects, second drag moves, undo restores',()=>{
    const core=new EditorCore(fixtures[0] as Document),v=new TouchViewport(core),before=clone(core.doc)
    v.view={x:0,y:0,zoom:1}
    v.start([{x:30,y:80}]);v.move([{x:40,y:85}]);v.end({x:40,y:85})
    expect(core.selection).toEqual({id:'schedule'});expect(core.doc).toEqual(before)
    v.start([{x:30,y:80}]);v.move([{x:40,y:85}]);v.end({x:40,y:85})
    expect(core.doc).not.toEqual(before);core.undo();expect(core.doc).toEqual(before)
  })
  it('pans only outside the page unless hand mode is selected',()=>{
    const core=new EditorCore(fixtures[0] as Document),v=new TouchViewport(core)
    v.view={x:20,y:20,zoom:1};const initial={...v.view}
    v.start([{x:25,y:25}]);v.move([{x:35,y:25}]);v.end({x:35,y:25});expect(v.view).toEqual(initial)
    v.start([{x:5,y:5}]);v.move([{x:15,y:15}]);v.end({x:15,y:15});expect(v.view.x).toBe(30)
    v.hand=true;v.start([{x:60,y:80}]);v.move([{x:70,y:90}]);v.end({x:70,y:90});expect(v.view.x).toBe(40)
  })
  it('pinch cancels an unfinished widget move and keeps the anchor',()=>{
    const core=new EditorCore(fixtures[0] as Document),v=new TouchViewport(core),before=clone(core.doc)
    core.select('schedule');v.view={x:0,y:0,zoom:1}
    v.start([{x:30,y:80}]);v.move([{x:40,y:80}])
    v.start([{x:50,y:100},{x:150,y:100}]);v.move([{x:0,y:100},{x:200,y:100}]);v.end({x:200,y:100})
    expect(core.doc).toEqual(before);expect(v.view.zoom).toBe(2);expect(v.view.x).toBe(-100)
    expect(core.canUndo).toBe(false)
  })
  it('decodes the real WQY subset without atob or Buffer',()=>{
    const source=deviceFontData['wqy9-lunar'].base64
    const expected=U8g2Font.fromBase64(source)
    vi.stubGlobal('atob',undefined);vi.stubGlobal('Buffer',undefined)
    expect(U8g2Font.fromBase64(source)).toEqual(expected)
  })
})
