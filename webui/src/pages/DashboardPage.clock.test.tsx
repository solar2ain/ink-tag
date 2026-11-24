// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { MemoryRouter } from 'react-router-dom'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { DashboardPage } from './DashboardPage'
import { useDeviceStore } from '../store/device'

const mock=vi.hoisted(()=>({confirm:vi.fn(async(_options:unknown)=>true),runAction:vi.fn(async()=>undefined),readClockPartialRefreshCapability:vi.fn(async()=>undefined)}))
vi.mock('../protocol/client',()=>({inktagClient:{runAction:mock.runAction,readClockPartialRefreshCapability:mock.readClockPartialRefreshCapability}}))
vi.mock('../components/ConfirmationDialog',()=>({useConfirmationDialog:()=>({confirm:mock.confirm,dialog:null})}))
vi.mock('../components/TemplatePreview',()=>({TemplatePreview:()=>null}))

let host:HTMLDivElement,root:Root
beforeEach(()=>{
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT',true);vi.clearAllMocks()
  useDeviceStore.getState().resetDevice()
  host=document.createElement('div');document.body.append(host);root=createRoot(host)
})
afterEach(async()=>{
  await act(async()=>root.unmount());host.remove()
  useDeviceStore.getState().resetDevice();vi.unstubAllGlobals()
})
it.each([[true,true],[true,false],[false,true],[false,false]] as const)(
  'clock capability=%s Pro=%s: always confirms the refresh/power cost',async(supported,pro)=>{
    useDeviceStore.setState({connection:'connected',activation:{phase:pro?'active':'inactive',edition:pro?'advanced':'base'}})
    useDeviceStore.getState().setCapabilities({clockPartialRefresh:supported})
    await act(async()=>root.render(<MemoryRouter><DashboardPage /></MemoryRouter>))
    const button=Array.from(host.querySelectorAll('button')).find(b=>b.textContent==='时钟')!
    expect(button.disabled).toBe(false)
    await act(async()=>button.click())
    expect(mock.readClockPartialRefreshCapability).toHaveBeenCalledOnce()
    expect(mock.confirm).toHaveBeenCalledOnce()
    expect(mock.confirm.mock.calls[0][0]).toMatchObject({message:expect.stringContaining(supported&&pro?'每天仍会全屏刷新':'可能每分钟全刷')})
    expect(mock.runAction).toHaveBeenCalledExactlyOnceWith('clock')
  })
it('does not send a clock command after cancelling',async()=>{
  mock.confirm.mockResolvedValueOnce(false)
  useDeviceStore.setState({connection:'connected',deviceId:'clock-A'})
  await act(async()=>root.render(<MemoryRouter><DashboardPage /></MemoryRouter>))
  await act(async()=>Array.from(host.querySelectorAll('button')).find(b=>b.textContent==='时钟')!.click())
  expect(mock.runAction).not.toHaveBeenCalled()
})
it('does not act on a different device after the confirmation',async()=>{
  mock.confirm.mockImplementationOnce(async()=>{useDeviceStore.setState({deviceId:'clock-B'});return true})
  useDeviceStore.setState({connection:'connected',deviceId:'clock-A'})
  await act(async()=>root.render(<MemoryRouter><DashboardPage /></MemoryRouter>))
  await act(async()=>Array.from(host.querySelectorAll('button')).find(b=>b.textContent==='时钟')!.click())
  expect(mock.runAction).not.toHaveBeenCalled()
})
