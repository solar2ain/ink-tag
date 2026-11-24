// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { createMemoryRouter, RouterProvider } from 'react-router-dom'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { Lab } from './main'
import { useDeviceStore } from '../../store/device'
import { makeScreenConfig } from '../../protocol/screen'
import type { EditorCore } from '../../../../packages/inktag-editor-core/src/index'

const mock=vi.hoisted(()=>({core:undefined as EditorCore|undefined,export:vi.fn(),sendCanvas:vi.fn(async()=>undefined),listStoredImages:vi.fn(async()=>undefined),put:vi.fn(async(_record:{document:string})=>undefined),delete:vi.fn(async()=>undefined),draftGet:vi.fn(async()=>undefined)}))
vi.mock('leafer-editor',()=>({Leafer:class{resize(){}destroy(){}}}))
vi.mock('./adapter',()=>({mountScene:(_e:unknown,_c:unknown,core:EditorCore)=>{mock.core=core;return {render:vi.fn(),viewport:vi.fn(),destroy:vi.fn(),exportPng:mock.export}}}))
vi.mock('./useViewport',()=>({useViewport:()=>({stage:{current:null},size:{width:600,height:500},view:{x:0,y:0,zoom:1},fit:vi.fn(),handlers:{}})}))
vi.mock('./Inspector',()=>({Inspector:()=>null}))
vi.mock('./LayerList',()=>({LayerList:()=>null}))
vi.mock('./GuideHandles',()=>({GuideHandles:()=>null}))
vi.mock('../../components/ResizableEditorPanels',()=>({ResizableEditorPanels:({children}:{children:React.ReactNode})=><div>{children}</div>}))
vi.mock('../../protocol/client',()=>({inktagClient:{sendCanvas:mock.sendCanvas,listStoredImages:mock.listStoredImages}}))
vi.mock('../../data/db',()=>({db:{pages:{put:mock.put},templates:{put:mock.put},canvasDrafts:{get:mock.draftGet,put:mock.put,delete:mock.delete}}}))
let host:HTMLDivElement,root:Root
beforeEach(()=>{
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT',true);vi.clearAllMocks();useDeviceStore.getState().resetDevice()
  Object.defineProperty(document,'fonts',{configurable:true,value:{ready:Promise.resolve(),load:async()=>[],addEventListener:vi.fn(),removeEventListener:vi.fn()}})
  vi.stubGlobal('createImageBitmap',async()=>({width:400,height:300,close:vi.fn()}))
  vi.spyOn(HTMLCanvasElement.prototype,'getContext').mockReturnValue({drawImage:vi.fn()} as never)
  mock.export.mockResolvedValue(new Blob())
  host=document.createElement('div');document.body.append(host);root=createRoot(host)
})
afterEach(async()=>{await act(async()=>root.unmount());host.remove();useDeviceStore.getState().resetDevice();vi.restoreAllMocks();vi.unstubAllGlobals()})
async function setup(){
  const router=createMemoryRouter([{path:'/edit',element:<Lab initialTemplateId="blank" initialWidth={400} initialHeight={300} initialPageMode="static"/>},{path:'/create',element:<p>模板库</p>}],{initialEntries:['/edit']})
  await act(async()=>root.render(<RouterProvider router={router}/>))
  return router
}
const click=async(text:string)=>act(async()=>[...document.querySelectorAll<HTMLButtonElement>('button')].find(b=>b.textContent===text)!.click())
it('sends an unsaved static template with no font/package or page-library admission',async()=>{
  useDeviceStore.setState({connection:'connected',deviceId:'virtual',screen:makeScreenConfig({driver:0x13,color:2,transform:0,width:400,height:300,visibleX:0,visibleY:0,visibleWidth:400,visibleHeight:300})})
  await setup();await click('文字');await click('发送到设备');await click('开始发送')
  expect(mock.sendCanvas).toHaveBeenCalledOnce();expect(mock.put).not.toHaveBeenCalled()
  expect(mock.export).toHaveBeenCalledWith(false)
  expect(mock.core!.doc.widgets).toHaveLength(1)
})
it('opens an informative offline send chooser without saving or attempting BLE',async()=>{
  await setup();await click('发送到设备')
  expect(document.body.textContent).toContain('当前画面无需保存')
  expect([...document.querySelectorAll('button')].find(b=>b.textContent==='开始发送')?.disabled).toBe(true)
  expect(mock.sendCanvas).not.toHaveBeenCalled();expect(mock.put).not.toHaveBeenCalled()
})
it('persists a separate draft on return without creating a saved page',async()=>{
  const router=await setup();await click('文字')
  await act(async()=>host.querySelector<HTMLAnchorElement>('[aria-label="返回模板"]')!.click())
  expect(router.state.location.pathname).toBe('/edit');await click('暂存草稿')
  expect(router.state.location.pathname).toBe('/create')
  expect(mock.put).toHaveBeenCalledOnce()
  expect(JSON.parse(mock.put.mock.calls[0][0].document).widgets).toHaveLength(1)
})
it('preserves the todo row selection when its inline text commits on blur',async()=>{
  await setup();await click('待办事项');await click('编辑格子')
  await act(async()=>document.querySelector<HTMLInputElement>('[aria-label="单元格内容"]')!.blur())
  expect(mock.core!.selection).toMatchObject({row:0,col:0})
  mock.core!.toggleChecked()
  expect(mock.core!.selected!.cells[0][0].checked).toBe(true)
})
