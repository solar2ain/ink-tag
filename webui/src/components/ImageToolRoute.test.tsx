// @vitest-environment jsdom
import {act} from 'react'
import {createRoot} from 'react-dom/client'
import {expect,it,vi} from 'vitest'
import {ImageToolRoute} from './ImageToolRoute'
import {useDeviceStore} from '../store/device'

it('contains a failed image tool and retries without unmounting the connected shell',async()=>{
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT',true)
  const error=vi.spyOn(console,'error').mockImplementation(()=>{})
  const host=document.createElement('div'),root=createRoot(host)
  const previous=useDeviceStore.getState()
  useDeviceStore.setState({connection:'connected',deviceName:'IKT_TL_TEST'})
  const load=vi.fn().mockRejectedValueOnce(new TypeError('Failed to fetch dynamically imported module')).mockResolvedValue({default:()=> <div>图片工具已打开</div>})
  try{
    await act(async()=>root.render(<><header>连接状态保留</header><ImageToolRoute load={load} /></>))
    expect(host.querySelector('[role=alert]')?.textContent).toContain('图片工具未能打开')
    expect(host.querySelector('[role=alert]')?.textContent).toContain('资源未能加载')
    expect(host.querySelector('header')?.textContent).toBe('连接状态保留')
    await act(async()=>host.querySelector('button')!.click())
    expect(host.textContent).toContain('图片工具已打开')
    expect(host.querySelector('[role=alert]')).toBeNull()
    expect(load).toHaveBeenCalledTimes(2)
    expect(useDeviceStore.getState().connection).toBe('connected')
    expect(useDeviceStore.getState().deviceName).toBe('IKT_TL_TEST')
  }finally{
    await act(async()=>root.unmount());useDeviceStore.setState(previous,true)
    error.mockRestore();vi.unstubAllGlobals()
  }
})

it('does not mislabel a rendering exception as a network or update failure',async()=>{
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT',true)
  const error=vi.spyOn(console,'error').mockImplementation(()=>{})
  const host=document.createElement('div'),root=createRoot(host)
  const load=async()=>({default:()=>{throw new Error('render failure')}})
  try{
    await act(async()=>root.render(<ImageToolRoute load={load} />))
    expect(host.textContent).toContain('图片工具发生了错误')
    expect(host.textContent).not.toContain('资源未能加载')
  }finally{
    await act(async()=>root.unmount());error.mockRestore();vi.unstubAllGlobals()
  }
})
