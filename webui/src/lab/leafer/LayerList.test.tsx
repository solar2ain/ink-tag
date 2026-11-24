// @vitest-environment jsdom
import { act } from 'react'
import { createRoot } from 'react-dom/client'
import { expect,it,vi } from 'vitest'
import { EditorCore } from '../../../../packages/inktag-editor-core/src/index'
import { weatherDocument } from './weatherTemplate'
import { LayerList } from './LayerList'

it('deletes only the row target, disables locked rows and preserves undo',async()=>{
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT',true)
  const core=new EditorCore(weatherDocument(400,300)),host=document.createElement('div'),root=createRoot(host)
  document.body.append(host)
  const target=core.doc.widgets[0],other=core.doc.widgets[1],before=JSON.stringify(core.doc)
  const render=()=>root.render(<LayerList core={core} act={fn=>{fn();render()}}/>)
  const remove=()=>host.querySelector<HTMLButtonElement>(`[aria-label="删除图层 ${target.name}"]`)!
  try{
    core.select(other.id)
    await act(async()=>render())
    expect(remove().previousElementSibling?.className).toBe('layer-lock')
    await act(async()=>{core.setLocked(true,[target.id]);render()})
    expect(remove().disabled).toBe(true)
    await act(async()=>remove().click())
    expect(core.doc.widgets.some(w=>w.id===target.id)).toBe(true)
    await act(async()=>{core.setLocked(false,[target.id]);render()})
    await act(async()=>remove().click())
    expect(core.doc.widgets.some(w=>w.id===target.id)).toBe(false)
    expect(core.doc.widgets.some(w=>w.id===other.id)).toBe(true)
    core.undo();expect(JSON.stringify(core.doc)).toBe(before)
    const members=core.doc.widgets.slice(0,2);members.forEach(w=>w.groupId='group');members[1].locked=true
    await act(async()=>render())
    expect(remove().disabled).toBe(true)
  }finally{await act(async()=>root.unmount());host.remove();vi.unstubAllGlobals()}
})
