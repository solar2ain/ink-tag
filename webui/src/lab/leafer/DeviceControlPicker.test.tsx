// @vitest-environment jsdom
import { act } from 'react'
import { createRoot } from 'react-dom/client'
import { expect,it,vi } from 'vitest'
import { DeviceControlPicker } from './DeviceControlPicker'

it('collapses device controls, supports Escape and closes after insertion',async()=>{
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT',true)
  const host=document.createElement('div'),root=createRoot(host),add=vi.fn()
  document.body.append(host)
  try {
    await act(async()=>root.render(<DeviceControlPicker add={add}/>))
    const trigger=host.querySelector<HTMLButtonElement>('button')!
    expect(trigger.getAttribute('aria-expanded')).toBe('false')
    expect(host.querySelector('[role=group]')).toBeNull()
    await act(async()=>trigger.click())
    expect(host.querySelectorAll('[role=group] button')).toHaveLength(6)
    await act(async()=>trigger.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true})))
    expect(trigger.getAttribute('aria-expanded')).toBe('false')
    expect(document.activeElement).toBe(trigger)
    await act(async()=>trigger.click())
    const bluetooth=Array.from(host.querySelectorAll('button')).find(button=>button.textContent==='蓝牙名称')!
    await act(async()=>bluetooth.click())
    expect(add).toHaveBeenCalledExactlyOnceWith('bluetooth')
    expect(trigger.getAttribute('aria-expanded')).toBe('false')
  } finally {
    await act(async()=>root.unmount());host.remove();vi.unstubAllGlobals()
  }
})
