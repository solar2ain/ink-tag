// @vitest-environment jsdom
import { act } from 'react'
import { createRoot } from 'react-dom/client'
import { expect,it,vi } from 'vitest'
import { EditorCore } from '../../../../packages/inktag-editor-core/src/index'
import { weatherDocument } from './weatherTemplate'
import { SelectionActions, SelectionTools } from './SelectionTools'

it('places lock and delete together only in bottom actions, with a usable unlock',async()=>{
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT',true)
  const core=new EditorCore(weatherDocument(400,300)),host=document.createElement('div'),root=createRoot(host)
  document.body.append(host);core.select(core.doc.widgets[0].id)
  const render=()=>root.render(<><SelectionTools core={core} act={fn=>{fn();render()}}/><SelectionActions core={core} act={fn=>{fn();render()}}/></>)
  const button=(text:string)=>Array.from(host.querySelectorAll('button')).find(b=>b.textContent===text)!
  try{
    await act(async()=>render())
    const lock=button('锁定所选'),remove=button('删除控件')
    expect(lock.parentElement).toBe(remove.parentElement)
    expect(host.querySelector('.selection-tools')?.contains(lock)).toBe(false)
    expect(host.lastElementChild?.className).toBe('selection-bottom-actions')
    await act(async()=>lock.click())
    expect(button('删除控件').disabled).toBe(true)
    expect(button('解锁所选').disabled).toBe(false)
    await act(async()=>button('解锁所选').click())
    expect(button('删除控件').disabled).toBe(false)
  }finally{await act(async()=>root.unmount());host.remove();vi.unstubAllGlobals()}
})
