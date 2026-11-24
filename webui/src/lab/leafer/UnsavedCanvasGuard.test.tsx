// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { createMemoryRouter, RouterProvider, Link } from 'react-router-dom'
import { afterEach, expect, it, vi } from 'vitest'
import { UnsavedCanvasGuard } from './UnsavedCanvasGuard'
let host:HTMLDivElement,root:Root
afterEach(async()=>{await act(async()=>root?.unmount());host?.remove();vi.unstubAllGlobals()})
async function setup(dirty=true){
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT',true)
  const save=vi.fn(async()=>true),draft=vi.fn(async()=>true),discard=vi.fn(async()=>true)
  const router=createMemoryRouter([{path:'/edit',element:<><Link to="/away">返回</Link><UnsavedCanvasGuard dirty={dirty} save={save} draft={draft} discard={discard}/></>},{path:'/away',element:<p>模板库</p>}],{initialEntries:['/edit']})
  host=document.createElement('div');document.body.append(host);root=createRoot(host)
  await act(async()=>root.render(<RouterProvider router={router}/>))
  await act(async()=>host.querySelector('a')!.click())
  const click=async(text:string)=>act(async()=>[...document.querySelectorAll('button')].find(b=>b.textContent===text)!.click())
  return {save,draft,discard,router,click}
}
it.each([['保存页面','save'],['暂存草稿','draft'],['不保存','discard']] as const)('leaves only after %s succeeds',async(label,method)=>{
  const s=await setup();expect(s.router.state.location.pathname).toBe('/edit')
  await s.click(label);expect(s[method]).toHaveBeenCalledOnce();expect(s.router.state.location.pathname).toBe('/away')
})
it('keeps editing on cancel or a failed save; beforeunload is protected',async()=>{
  const s=await setup();s.save.mockResolvedValueOnce(false)
  await s.click('保存页面');expect(s.router.state.location.pathname).toBe('/edit');expect(document.body.textContent).toContain('未能保存')
  const event=new Event('beforeunload',{cancelable:true});window.dispatchEvent(event);expect(event.defaultPrevented).toBe(true)
  await s.click('继续编辑');expect(document.querySelector('[role=dialog]')).toBeNull();expect(s.router.state.location.pathname).toBe('/edit')
})
it('does not prompt for an unchanged template',async()=>{const s=await setup(false);expect(s.router.state.location.pathname).toBe('/away');expect(document.querySelector('[role=dialog]')).toBeNull()})
