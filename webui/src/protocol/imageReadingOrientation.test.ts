import {afterEach,expect,it,vi} from 'vitest'
import {decodeCapabilitiesV2} from '../../../packages/inktag-client-core/src/decoders'
import {decodeStoredImage,encodeImage} from './image'
import {transformRgba} from '../../../packages/inktag-client-core/src/screen'
import {makeScreenConfig} from './screen'
import {inktagClient} from './client'
import {CapabilityExtensionV2,CommandV2} from './generatedCommandsV2'

it('reads extension only beyond the original settings fields',()=>{
  for(const size of [17,24,27,28,32]) {
    const bytes=new Uint8Array(size).fill(255);bytes[0]=1
    expect(decodeCapabilitiesV2(bytes)!.extensionMask).toBe(size>=28?0xffffffff:0)
  }
})
it.each([0,1,2,3])('download reverses reading turn %i without changing native profile',q=>{
  const screen=makeScreenConfig({driver:0x33,recipe:1,color:3,transform:5,width:8,height:4,
    visibleX:0,visibleY:0,visibleWidth:8,visibleHeight:4})
  const frame={width:4,height:8,data:new Uint8ClampedArray(128).fill(255)}
  for(let i=0;i<16;i++)frame.data[i]=i%4===3?255:0
  const encoded=encodeImage(frame,screen)
  const original=decodeStoredImage(encoded.transfers[0].data,screen,encoded.format,1)
  expect(decodeStoredImage(encoded.transfers[0].data,screen,encoded.format,1,q))
    .toEqual(transformRgba(original,(4-q)&3))
})
const transport=inktagClient as unknown as {
  v2CapabilityExtensions:number
  requestV2:(opcode:number,payload:Uint8Array)=>Promise<unknown>
  sendCanvasV2:(canvas:HTMLCanvasElement,t:{step:string;data:Uint8Array}[],format:number,
    screen:ReturnType<typeof makeScreenConfig>,name:string,save:boolean,type:'image',
    replace:undefined,display:boolean,q:number)=>Promise<unknown>
}
afterEach(()=>{vi.restoreAllMocks();transport.v2CapabilityExtensions=0})
it.each([false,true])('negotiates the same metadata for direct/store %s before any wire write',async save=>{
  const screen=makeScreenConfig({driver:0x33,recipe:0,color:3,transform:0,width:8,height:4,
    visibleX:0,visibleY:0,visibleWidth:8,visibleHeight:4})
  const spy=vi.spyOn(transport,'requestV2').mockImplementation(async(op,payload)=>{
    expect(op).toBe(CommandV2.TRANSFER_BEGIN);expect(payload[3]&12).toBe(4)
    throw new Error('captured before IO')
  })
  const send=(q:number)=>transport.sendCanvasV2({} as HTMLCanvasElement,[{step:'color',data:new Uint8Array(8)}],3,screen,'',save,'image',undefined,false,q)
  await expect(send(1)).rejects.toThrow('尚未声明');expect(spy).not.toHaveBeenCalled()
  transport.v2CapabilityExtensions=CapabilityExtensionV2.IMAGE_READING_ORIENTATION
  await expect(send(1)).rejects.toThrow('captured before IO');expect(spy).toHaveBeenCalledOnce()
})
