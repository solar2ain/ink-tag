// @vitest-environment jsdom
import { act } from 'react'
import { createRoot } from 'react-dom/client'
import { expect,it,vi } from 'vitest'
import { EditorCore,parseDocument } from '../../../../packages/inktag-editor-core/src/index'
import { CanvasImageControls } from './CanvasImageControls'

it('offers image-processing sliders, preserves values in drafts and disables unused controls',async()=>{
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT',true)
  const core=new EditorCore({version:1,width:400,height:300,pageMode:'static',widgets:[{id:'image',name:'Image',kind:'image',x:0,y:0,width:100,height:100,cells:[[{text:''}]],style:{fontSize:14,color:'#000000',bold:false}}]})
  core.select('image')
  const host=document.createElement('div'),root=createRoot(host);document.body.append(host)
  const render=()=>root.render(<CanvasImageControls core={core} act={fn=>{fn();render()}} notice={()=>{}}/>)
  const strength=()=>host.querySelector<HTMLInputElement>('[aria-label="图片抖动强度"]')!
  try{
    await act(async()=>render())
    expect(strength().disabled).toBe(true)
    const select=host.querySelector<HTMLSelectElement>('[aria-label="图片抖动算法"]')!
    await act(async()=>{select.value='auto';select.dispatchEvent(new Event('change',{bubbles:true}))})
    expect(strength().disabled).toBe(false)
    expect(strength().min).toBe('0');expect(strength().max).toBe('5')
    await act(async()=>{core.change(doc=>{doc.widgets[0].imageAdjustments={contrast:1.3,strength:2.4,heavyInk:true}});render()})
    expect(strength().value).toBe('2.4')
    expect(parseDocument(JSON.stringify(core.doc)).widgets[0].imageAdjustments).toEqual({contrast:1.3,strength:2.4,heavyInk:true})
    await act(async()=>{select.value='none';select.dispatchEvent(new Event('change',{bubbles:true}))})
    expect(strength().disabled).toBe(true)
    expect(host.querySelector<HTMLInputElement>('[aria-label="图片对比度"]')!.disabled).toBe(false)
    for(const bad of [{contrast:0,strength:1,heavyInk:false},{contrast:1,strength:6,heavyInk:false}])expect(()=>parseDocument(JSON.stringify({...core.doc,widgets:[{...core.doc.widgets[0],imageAdjustments:bad}]}))).toThrow()
  }finally{await act(async()=>root.unmount());host.remove();vi.unstubAllGlobals()}
})
