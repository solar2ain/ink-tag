// @vitest-environment jsdom
import { act } from 'react'
import { createRoot } from 'react-dom/client'
import { expect,it,vi } from 'vitest'
import { EditorCore } from '../../../../packages/inktag-editor-core/src/index'
import { weatherDocument } from './weatherTemplate'
import { WeatherControls } from './WeatherPanel'

it('only offers the weather dropdown on host pages and inserts one undoable group',async()=>{
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT',true)
  const host=document.createElement('div'),root=createRoot(host)
  document.body.append(host)
  const core=new EditorCore(weatherDocument(400,300)),before=JSON.stringify(core.doc)
  try{
    for(const pageMode of ['static','dynamic'] as const){
      const other=new EditorCore({...core.doc,pageMode})
      await act(async()=>root.render(<WeatherControls core={other} act={fn=>fn()}/>))
      expect(host.querySelector('button')).toBeNull()
    }
    await act(async()=>root.render(<WeatherControls core={core} act={fn=>fn()}/>))
    const trigger=host.querySelector<HTMLButtonElement>('[aria-label="天气控件"]')!
    expect(trigger.className).toContain('weather-control-trigger')
    expect(trigger.getAttribute('aria-expanded')).toBe('false')
    await act(async()=>trigger.click())
    expect(host.querySelector('[role=group]')).not.toBeNull()
    await act(async()=>trigger.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true})))
    expect(trigger.getAttribute('aria-expanded')).toBe('false')
    expect(document.activeElement).toBe(trigger)
    await act(async()=>trigger.click())
    await act(async()=>Array.from(host.querySelectorAll('button')).find(b=>b.textContent==='天气组合')!.click())
    expect(trigger.getAttribute('aria-expanded')).toBe('false')
    expect(core.doc.widgets).toHaveLength(JSON.parse(before).widgets.length+6)
    expect(new Set(core.doc.widgets.map(w=>w.id)).size).toBe(core.doc.widgets.length)
    expect(new Set(core.doc.widgets.slice(-6).map(w=>w.groupId)).size).toBe(1)
    core.undo();expect(JSON.stringify(core.doc)).toBe(before)
  }finally{await act(async()=>root.unmount());host.remove();vi.unstubAllGlobals()}
})
