import { describe, expect, it } from 'vitest'
import { clone, parseDocument, type Document, type Widget } from '../../../../packages/inktag-editor-core/src/index'
import { expandedTextFrame, fittedTextContent, textCellLayout } from './textFit'

const make=():Widget=>({id:'text',name:'文字',kind:'text',x:30,y:30,width:20,height:10,
  style:{fontSize:16,color:'#000000',bold:false,fontId:'wqy12-lunar'},
  cells:[[{text:'2026年',richText:{runs:[{text:'2026',fontId:'wqy12-lunar',style:{fontSize:24,color:'#dc3026'}},{text:'年',fontId:'wqy12-lunar',style:{fontSize:16}}],padding:2}}]]})
describe('explicit rich text fitting',()=>{
  it('expands a text box without changing its fonts, and stays within the page',()=>{
    const w=make(),before=clone(w),doc:Document={version:1,width:400,height:300,widgets:[w]}
    const frame=expandedTextFrame(doc,w,w.cells[0][0].richText!)!
    expect(frame.width).toBeGreaterThan(w.width);expect(frame.height).toBeGreaterThan(w.height)
    expect(w).toEqual(before)
    expect(textCellLayout({...w,...frame},w.cells[0][0].richText!).overflow).toBe(false)
    expect(()=>parseDocument(JSON.stringify({...doc,widgets:[{...w,...frame}]}))).not.toThrow()
  })
  it('refuses growth into neighbors or beyond the available page',()=>{
    const w=make(),other={...make(),id:'neighbor',x:51,y:20,width:80,height:60}
    expect(expandedTextFrame({version:1,width:400,height:300,widgets:[w,other]},w,w.cells[0][0].richText!)).toBeUndefined()
    expect(expandedTextFrame({version:1,width:40,height:50,widgets:[w]},w,w.cells[0][0].richText!)).toBeUndefined()
  })
  it('fits mixed styles without changing the box, colors or font families',()=>{
    const w=make();w.width=60;w.height=22
    const before=clone(w),content=fittedTextContent(w,w.cells[0][0].richText!)!
    expect(content).toBeDefined();expect(textCellLayout(w,content).overflow).toBe(false)
    expect(w).toEqual(before)
    expect(content.runs[0].style!.fontSize).toBeGreaterThan(content.runs[1].style!.fontSize!)
    expect(content.runs[0].style!.color).toBe('#dc3026');expect(content.runs[0].fontId).toBe('wqy12-lunar')
    expect(()=>parseDocument(JSON.stringify({version:1,width:400,height:300,widgets:[{...w,cells:[[{text:'2026年',richText:content}]]}]}))).not.toThrow()
  })
  it('does not reduce text below the minimum readable size',()=>{
    const w=make();w.width=5;w.height=5
    expect(fittedTextContent(w,w.cells[0][0].richText!)).toBeUndefined()
  })
  it('uses integer cell bounds and reserves list marker space',()=>{
    const w=make();w.kind='todo';w.width=70;w.height=40
    expect(textCellLayout(w,w.cells[0][0].richText!).overflow).toBe(true)
    const content=fittedTextContent(w,w.cells[0][0].richText!)!
    expect(textCellLayout(w,content).overflow).toBe(false)
    expect(expandedTextFrame({version:1,width:400,height:300,widgets:[w]},w,content)).toBeUndefined()
  })
})
