import {afterEach,beforeEach,expect,it,vi} from 'vitest'
import {InkTagClient,ProtocolV2Error} from './client'
import {CommandV2,StatusV2} from './generatedCommandsV2'
import {makeScreenConfig} from './screen'
import {useDeviceStore} from '../store/device'

let client:InkTagClient
let request:ReturnType<typeof vi.fn>
const canvas={width:8,height:4,getContext:()=>({getImageData:()=>({width:8,height:4,
  data:new Uint8ClampedArray(8*4*4).fill(255)})})} as unknown as HTMLCanvasElement
beforeEach(()=>{
  client=new InkTagClient();client.keepCurrentDisplayWork()
  useDeviceStore.getState().resetDevice()
  useDeviceStore.setState({screen:makeScreenConfig({driver:0x33,recipe:0,color:3,
    transform:0,width:8,height:4,visibleX:0,visibleY:0,visibleWidth:8,visibleHeight:4})})
  vi.stubGlobal('requestAnimationFrame',(cb:FrameRequestCallback)=>{cb(0);return 1})
  request=vi.fn()
  Object.assign(client,{protocolV2:true,requestV2:request})
})
afterEach(()=>{client.keepCurrentDisplayWork();vi.unstubAllGlobals();vi.restoreAllMocks()})

it.each([StatusV2.BUSY,StatusV2.DENIED,StatusV2.LOW_POWER,StatusV2.TIMEOUT])(
  'never writes, cancels, aborts or retries after refused/unknown BEGIN %i',async status=>{
    const error=new ProtocolV2Error(status)
    request.mockRejectedValue(error)
    await expect(client.sendCanvas(canvas,'photo')).rejects.toBe(error)
    expect(request.mock.calls.map(c=>c[0])).toEqual([CommandV2.TRANSFER_BEGIN])
    if(status===StatusV2.BUSY) {
      expect(useDeviceStore.getState().action).toBe('picture')
      expect(useDeviceStore.getState().actionDetail).toContain('本次操作没有排队')
    }
  })

it('only explicit replacement waits for CANCEL and idle before a new BEGIN',async()=>{
  let release!:()=>void
  const cancelled=new Promise<void>(resolve=>{release=resolve})
  let begins=0
  request.mockImplementation(async(op:number)=>{
    if(op===CommandV2.TRANSFER_BEGIN) {
      if(++begins===1)throw new ProtocolV2Error(StatusV2.BUSY)
      throw new Error('stop after second admission')
    }
    if(op===CommandV2.CANCEL) {await cancelled;return {payload:new Uint8Array(0)}}
    if(op===CommandV2.GET_STATUS)return {payload:new Uint8Array([1,0,0])}
    throw new Error(`unexpected command ${op}`)
  })
  await expect(client.sendCanvas(canvas,'photo')).rejects.toBeInstanceOf(ProtocolV2Error)
  const replacing=client.replaceBusyDisplayWork()
  expect(request.mock.calls.map(c=>c[0])).toEqual([CommandV2.TRANSFER_BEGIN,CommandV2.CANCEL])
  release();await replacing
  await vi.waitFor(()=>expect(begins).toBe(2))
  expect(request.mock.calls.map(c=>c[0])).toEqual([
    CommandV2.TRANSFER_BEGIN,CommandV2.CANCEL,CommandV2.GET_STATUS,CommandV2.TRANSFER_BEGIN])
})

it.each([StatusV2.DENIED,StatusV2.BUSY,StatusV2.TIMEOUT])(
  'does not re-BEGIN when explicit cancellation is refused/unconfirmed %i',async status=>{
    request.mockRejectedValueOnce(new ProtocolV2Error(StatusV2.BUSY))
      .mockRejectedValueOnce(new ProtocolV2Error(status))
    await expect(client.sendCanvas(canvas,'photo')).rejects.toBeInstanceOf(ProtocolV2Error)
    await expect(client.replaceBusyDisplayWork()).rejects.toMatchObject({status})
    expect(request.mock.calls.map(c=>c[0])).toEqual([CommandV2.TRANSFER_BEGIN,CommandV2.CANCEL])
  })
