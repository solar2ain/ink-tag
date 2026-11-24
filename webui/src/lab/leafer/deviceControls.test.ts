import { describe,expect,it } from 'vitest'
import { EditorCore,parseDocument,type Document } from '../../../../packages/inktag-editor-core/src/index'
import { createDeviceControl,deviceControls } from './deviceControls'
import { nativeRaster } from './nativeBridge'
import { resolveLabText } from './richTextRenderer'
import { selectedTextPreset } from './textPresetState'
import { textCellLayout } from './textFit'

const blank = (width=400,height=300):Document => ({version:1,width,height,pageMode:'dynamic',widgets:[]})
describe('device control insertions',() => {
  it('refuses device controls in a static page',()=>{
    expect(()=>createDeviceControl('bluetooth',{...blank(),pageMode:'static'})).toThrow('设备控件仅限动态页面')
  })
  it.each([[152,152],[200,200],[212,104],[250,128],[296,152],[400,300],[800,480]])('fits native-size controls at %ix%i', (width,height) => {
    const doc = blank(width,height)
    for(const {id} of deviceControls) {
      const w=createDeviceControl(id,doc)
      expect(w.x).toBeGreaterThanOrEqual(0);expect(w.y).toBeGreaterThanOrEqual(0)
      expect(w.x+w.width).toBeLessThanOrEqual(width);expect(w.y+w.height).toBeLessThanOrEqual(height-18)
      expect(() => parseDocument(JSON.stringify({...doc,widgets:[w]})),id).not.toThrow()
      if(w.kind==='text') {
        expect(w.native).toBeUndefined();expect(w.style.fontSize).toBe(12)
        expect(textCellLayout(w,w.cells[0][0].richText!).overflow,id).toBe(false)
        expect(selectedTextPreset(w.cells[0][0].richText!,w.style)).toBe(id)
      } else expect(nativeRaster(w,doc)?.pixels.some(Boolean)).toBe(true)
    }
  })
  it('keeps text values independent, serializable and editable as ordinary variables',() => {
    const doc=blank(),first=createDeviceControl('bluetooth',doc);doc.widgets.push(first)
    const second=createDeviceControl('bluetooth',doc);doc.widgets.push(second)
    expect(first.id).not.toBe(second.id)
    second.sample!.bluetoothName='MY_TAG'
    const restored=parseDocument(JSON.stringify(doc))
    expect(resolveLabText(restored.widgets[0],first.cells[0][0].text)).toBe('[IKT_GR_3A57]')
    expect(resolveLabText(restored.widgets[1],second.cells[0][0].text)).toBe('[MY_TAG]')
    expect(restored.widgets[1].cells[0][0].text).toBe('[{{bluetoothName}}]')
  })
  it('changes only the battery sample and keeps its native bounds',() => {
    const doc=blank(),w=createDeviceControl('battery-icon',doc)
    w.native!.sample={voltage:2200};const low=nativeRaster(w,doc)!
    w.native!.sample={voltage:3300};const high=nativeRaster(w,doc)!
    expect(low.width).toBe(high.width);expect(low.height).toBe(high.height)
    expect(low.pixels).not.toEqual(high.pixels)
  })
  it('adds one undoable operation without leaving a placeholder text widget',() => {
    const core=new EditorCore(blank()),w=createDeviceControl('temperature',core.doc)
    core.change(doc=>doc.widgets.push(w));core.select(w.id)
    core.undo();expect(core.doc.widgets).toHaveLength(0);expect(core.canUndo).toBe(false)
    core.redo();expect(core.doc.widgets).toEqual([w])
    core.select(w.id);core.copy();core.paste();expect(core.doc.widgets).toHaveLength(2)
  })
  it('refuses an unknown preset instead of inserting an empty control',() => {
    expect(()=>createDeviceControl('unknown',blank())).toThrow('未知设备控件')
  })
  it('also permits preview controls in a host-rendered dynamic page',()=>{
    expect(createDeviceControl('temperature',{...blank(),pageMode:'host-dynamic'}).cells[0][0].text).toBe('{{temperature}}')
  })
})
