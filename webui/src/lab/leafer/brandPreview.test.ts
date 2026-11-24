import { expect, it } from 'vitest'
import { brandPreview, overlayBrandPreview } from './brandPreview'
import { createEditionBadgeFrame } from '../../device-gui/editionBadgeBitmap'
import { deviceEffect, paperPreview } from './deviceEffect'
const state={connection:'connected',activation:{phase:'active' as const,edition:'advanced' as const},brandVisible:true}
it('preserves browser ImageData prototype dimensions through the complete preview pipeline',()=>{
  class BrowserImageData {
    get width(){return 250}
    get height(){return 122}
    readonly data=new Uint8ClampedArray(250*122*4).fill(255)
  }
  const source=new BrowserImageData()
  expect(Object.keys(source)).not.toContain('width')
  for(const previewState of [state,{...state,brandVisible:false}]) {
    const frame=paperPreview(deviceEffect(overlayBrandPreview(source,previewState,2),2).frame)
    expect([frame.width,frame.height,frame.data.length]).toEqual([250,122,250*122*4])
  }
  expect([paperPreview(source).width,paperPreview(source).height]).toEqual([250,122])
})
it('uses the connected verified Pro device and follows its hidden setting',()=>{
  expect(brandPreview(state,400,300,2)?.pixels).toEqual(createEditionBadgeFrame('pro',3,400,300).pixels)
  expect(brandPreview({...state,brandVisible:false},400,300,2)).toBeUndefined()
  expect(brandPreview({...state,brandVisible:null},400,300,2)).toBeDefined()
})
it('never hides base or trusts a stale/disconnected Pro status',()=>{
  const free=createEditionBadgeFrame('base',3,400,300).pixels
  expect(brandPreview({...state,connection:'disconnected',brandVisible:false},400,300,2)?.pixels).toEqual(free)
  expect(brandPreview({...state,activation:{phase:'inactive',edition:'base'},brandVisible:false},400,300,2)?.pixels).toEqual(free)
})
it('overlays the complete badge after cropping to final device dimensions, without touching source pixels',()=>{
  const source={width:250,height:122,data:new Uint8ClampedArray(250*122*4).fill(255)}
  const output=overlayBrandPreview(source,state,2),badge=brandPreview(state,250,122,2)!
  expect(output.data).not.toEqual(source.data)
  expect([...source.data].every(v=>v===255)).toBe(true)
  expect(badge.y+badge.height).toBeLessThanOrEqual(122)
  expect(overlayBrandPreview(source,{...state,brandVisible:false},2)).toBe(source)
})
