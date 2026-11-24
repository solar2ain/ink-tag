// @vitest-environment jsdom
import { expect,it,vi } from 'vitest'
import { CanvasImageCache } from './imageRaster'
import type { Document } from '../../../../packages/inktag-editor-core/src/index'
import { optimizeCanvas } from '../../image/optimize'
vi.mock('../../image/optimize',()=>({optimizeCanvas:vi.fn(async()=>{})}))

it('reprocesses image adjustments and uses the same cached pixels for export',async()=>{
  const close=vi.fn(),drawImage=vi.fn()
  vi.stubGlobal('fetch',vi.fn(async()=>({blob:async()=>new Blob()})))
  vi.stubGlobal('createImageBitmap',vi.fn(async()=>({width:10,height:10,close})))
  vi.spyOn(HTMLCanvasElement.prototype,'getContext').mockReturnValue({fillRect:vi.fn(),drawImage} as unknown as CanvasRenderingContext2D)
  vi.spyOn(HTMLCanvasElement.prototype,'toDataURL').mockImplementation(()=>`data:image/png;base64,${vi.mocked(optimizeCanvas).mock.calls.length}`)
  const doc:Document={version:1,width:400,height:300,colorMode:2,widgets:[{id:'i',name:'Image',kind:'image',x:0,y:0,width:100,height:100,image:'data:image/png;base64,source',imageDither:'auto',style:{fontSize:14,color:'#000000',bold:false},cells:[[{text:''}]]}]}
  const cache=new CanvasImageCache(),w=doc.widgets[0]
  try{
    await cache.ready(doc);const first=cache.image(w,doc)
    w.imageAdjustments={contrast:1.4,strength:2.3,heavyInk:true}
    await cache.ready(doc)
    expect(cache.image(w,doc)).not.toBe(first)
    expect(vi.mocked(optimizeCanvas).mock.calls.at(-1)?.[3]).toMatchObject({contrast:1.4,strength:2.3,heavyInk:true})
    const calls=vi.mocked(optimizeCanvas).mock.calls.length
    await cache.ready(doc);expect(vi.mocked(optimizeCanvas).mock.calls.length).toBe(calls)
    expect(close).toHaveBeenCalledTimes(2)
  }finally{cache.dispose();vi.restoreAllMocks();vi.unstubAllGlobals()}
})
