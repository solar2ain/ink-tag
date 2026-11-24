import { expect,it } from 'vitest'
import { adaptFrameToSize } from './canvasFit'
import { transformRgba } from '../../../packages/inktag-client-core/src/screen'
const image=(width:number,height:number)=>({width,height,data:Uint8ClampedArray.from({length:width*height*4},(_,i)=>i%4===3?255:Math.floor(i/4)%251)})
// Check every byte without the large typed-array object matcher overhead.
const sameBytes=(actual:Uint8ClampedArray,expected:Uint8ClampedArray)=>{
  expect(actual.length).toBe(expected.length)
  expect(actual.every((value,index)=>value===expected[index])).toBe(true)
}
it('crops a 250×128 template to the 250×122 window without scaling or changing its source',()=>{
  const source=image(250,128),before=source.data.slice(),result=adaptFrameToSize(source,{width:250,height:122})
  sameBytes(result.data,source.data.slice(3*250*4,125*250*4));sameBytes(source.data,before)
})
it('center pads smaller images and crops mixed larger/smaller axes',()=>{
  const result=adaptFrameToSize(image(3,6),{width:4,height:5})
  expect([...result.data.slice(0,4)]).toEqual([3,3,3,255])
  expect([...result.data.slice(4,8)]).toEqual([4,4,4,255])
  expect([...result.data.slice(12,16)]).toEqual([255,255,255,255])
})
it('pads without changing original pixels, preserving crisp palette colors',()=>{
  const source=image(4,2),result=adaptFrameToSize(source,{width:4,height:4})
  expect(result.data.slice(0,16)).toEqual(new Uint8ClampedArray(16).fill(255))
  expect(result.data.slice(16,48)).toEqual(source.data)
})
it('does not rewrite exact-size frames',()=>{const source=image(3,2);expect(adaptFrameToSize(source,source)).toBe(source)})
it.each([[128,250,250,128],[250,128,128,250],[300,400,400,300]])('rotates %i×%i before adapting to %i×%i, retaining every pixel', (sw,sh,width,height)=>{
  const source=image(sw,sh),before=source.data.slice()
  const result=adaptFrameToSize(source,{width,height}),expected=transformRgba(source,1)
  expect([result.width,result.height]).toEqual([width,height]);sameBytes(result.data,expected.data)
  sameBytes(source.data,before)
})
it('rotates the portrait ZKC21V template before removing only the six padding rows',()=>{
  const source=image(128,250),rotated=transformRgba(source,1)
  const result=adaptFrameToSize(source,{width:250,height:122})
  sameBytes(result.data,rotated.data.slice(3*250*4,125*250*4))
})
it('uses the same clockwise orientation as the image tool, not a second physical-screen transform',()=>{
  const result=adaptFrameToSize(image(2,3),{width:3,height:2})
  expect(Array.from({length:6},(_,i)=>result.data[i*4])).toEqual([4,2,0,5,3,1])
})
