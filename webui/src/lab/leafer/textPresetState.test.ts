import { describe,expect,it } from 'vitest'
import { clone,EditorCore,parseDocument,type Document } from '../../../../packages/inktag-editor-core/src/index'
import { applyTextPreset,selectedTextPreset } from './textPresetState'
import { textCellLayout } from './textFit'

const make=()=>{
  const doc:Document={version:1,width:400,height:300,widgets:[{id:'t',name:'文字',kind:'text',x:20,y:40,width:25,height:20,
    style:{fontSize:12,fontId:'wqy9-lunar',color:'#000000',bold:false},cells:[[{text:'文字'}]]}]}
  const core=new EditorCore(doc);core.select('t');return core
}
const selected=(core:EditorCore)=>selectedTextPreset(core.selected!.cells[0][0].richText!,core.selected!.style)
describe('text preset provenance and sizing',()=>{
  it('fits resolved text on application and shrinks again for a shorter preset',()=>{
    const core=make();applyTextPreset(core,'date');const wide=core.selected!.width
    expect(wide).toBeGreaterThan(25);expect(selected(core)).toBe('date')
    expect(textCellLayout(core.selected!,core.selected!.cells[0][0].richText!).overflow).toBe(false)
    applyTextPreset(core,'weekday');expect(core.selected!.width).toBeLessThan(wide);expect(selected(core)).toBe('weekday')
  })
  it('persists selection, detects content and typography edits, and restores on undo',()=>{
    const core=make();applyTextPreset(core,'date')
    const loaded=new EditorCore(parseDocument(JSON.stringify(core.doc)));loaded.select('t');expect(selected(loaded)).toBe('date')
    core.change(()=>{core.selected!.cells[0][0].richText!.runs[0].text='2027'})
    expect(selected(core)).toBe('');core.undo();expect(selected(core)).toBe('date')
    core.change(()=>{core.selected!.style.bold=true});expect(selected(core)).toBe('')
    core.undo();expect(selected(core)).toBe('date');core.redo();expect(selected(core)).toBe('')
  })
  it('keeps a preset selected after initial size adaptation and ignores position/sample edits',()=>{
    const core=make();core.selected!.width=60
    core.doc.widgets.push({...clone(core.selected!),id:'neighbor',x:80,width:100})
    applyTextPreset(core,'date');expect(selected(core)).toBe('date');expect(core.selected!.width).toBe(60)
    expect(textCellLayout(core.selected!,core.selected!.cells[0][0].richText!).overflow).toBe(false)
    core.selected!.x=10;core.selected!.sample={timestamp:1790812800};expect(selected(core)).toBe('date')
  })
  it('never resizes a table to accommodate a single cell preset',()=>{
    const core=make();core.selected!.kind='table';const before=clone(core.selected!)
    applyTextPreset(core,'date')
    expect(core.selected!.width).toBe(before.width);expect(core.selected!.height).toBe(before.height)
    expect(selected(core)).toBe('date')
  })
})
