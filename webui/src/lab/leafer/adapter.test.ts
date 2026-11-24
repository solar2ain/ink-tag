import { expect, it, vi } from 'vitest'
import { EditorCore, type Document } from '../../../../packages/inktag-editor-core/src/index'
import { mountScene } from './adapter'

class Node {
  children: Node[]=[]
  attrs: Record<string,unknown>
  constructor(attrs:Record<string,unknown>={}){this.attrs=attrs}
  add(node:Node){this.children.push(node)}
  clear(){this.children=[]}
  set(attrs:Record<string,unknown>){Object.assign(this.attrs,attrs)}
  destroy(){}
  on(){}
  off(){}
  async export(){return {data:new Blob()}}
}
function scene(checked=true,rich=false,brand?:Parameters<typeof mountScene>[8],native=false){
  const core=new EditorCore({version:1,width:400,height:300,footerInset:0,pageMode:'static',widgets:[]} as Document)
  core.add('todo');core.selected!.cells=[[{text:'完成事项',checked,...(rich?{richText:{runs:[{text:'完成事项'}]}}:{})}]]
  if(native)core.doc.widgets.push({...core.selected!,id:'brand-native',kind:'native',native:{key:'edition:free',role:'edition'}} as never)
  core.deselect()
  const canvas=new Node(),raster=vi.fn(()=>({width:1,height:1,pixels:new Uint8Array([1])}))
  const renderer=mountScene({Group:Node,Box:Node,Rect:Node,Text:Node,PointerEvent:{}} as never,canvas as never,core,()=>{},1,raster,undefined,
    ()=>({width:10,height:10,pixels:new Uint8Array(100).fill(1)}),brand,true)
  const all=(node:Node):Node[]=>[node,...node.children.flatMap(all)]
  return {nodes:()=>all(canvas),renderer,raster}
}
it('centers a geometric tick inside the checkbox and strikes through completed plain text',()=>{
  const s=scene(),nodes=s.nodes()
  expect(nodes.some(n=>n.attrs.text==='✓')).toBe(false)
  expect(nodes.filter(n=>typeof n.attrs.rotation==='number')).toHaveLength(2)
  expect(nodes.find(n=>n.attrs.text==='完成事项')?.attrs.textDecoration).toBe('delete')
  expect(scene(false).nodes().find(n=>n.attrs.text==='完成事项')?.attrs.textDecoration).toBe('none')
})
it('also strikes through rich-text rows and leaves unchecked rows alone',()=>{
  const checked=scene(true,true),unchecked=scene(false,true)
  expect(checked.nodes().length).toBe(unchecked.nodes().length+3)
})
it('uses the managed badge instead of a saved base mark, including hidden Pro',()=>{
  const visible=vi.fn(()=>({x:330,y:280,width:1,height:1,pixels:new Uint8Array([2])}))
  const s=scene(false,false,visible,true)
  expect(visible).toHaveBeenCalledOnce();expect(s.raster).not.toHaveBeenCalled()
  const hidden=scene(false,false,()=>undefined,true)
  expect(hidden.raster).not.toHaveBeenCalled()
  expect(hidden.nodes().some(n=>n.attrs.text==='InkTag base')).toBe(false)
})
it('excludes the preview badge from actual image exports by default',async()=>{
  const badge=vi.fn(()=>({x:330,y:280,width:1,height:1,pixels:new Uint8Array([2])})),s=scene(false,false,badge)
  badge.mockClear();await s.renderer.exportPng()
  // Only the final editor redraw includes the badge, not render(false).
  expect(badge).toHaveBeenCalledOnce()
  badge.mockClear();await s.renderer.exportPng(true)
  expect(badge).toHaveBeenCalledTimes(2)
})
