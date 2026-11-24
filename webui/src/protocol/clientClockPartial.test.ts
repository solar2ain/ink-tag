import { beforeEach, expect, it, vi } from 'vitest'
import { InkTagClient } from './client'
import { CommandV2 } from './generatedCommandsV2'
import { useDeviceStore } from '../store/device'

beforeEach(()=>useDeviceStore.getState().resetDevice())
it.each([0,0x80000000])('reads the current high capability bit unsigned (%s)',async(mask)=>{
  const client=new InkTagClient();(client as any).protocolV2=true
  const payload=new Uint8Array(17);payload[0]=1;new DataView(payload.buffer).setUint32(1,mask,true)
  const send=vi.spyOn(client as any,'requestV2').mockResolvedValue({payload})
  await client.readClockPartialRefreshCapability()
  expect(send).toHaveBeenCalledExactlyOnceWith(CommandV2.GET_CAPABILITIES)
  expect(useDeviceStore.getState().capabilities.clockPartialRefresh).toBe(mask!==0)
})
it.each(['failure','bad-packet','changed-device'])('discards stale capability on %s',async(reason)=>{
  const client=new InkTagClient();(client as any).protocolV2=true
  useDeviceStore.getState().setCapabilities({clockPartialRefresh:true})
  vi.spyOn(client as any,'requestV2').mockImplementation(async()=>{
    if(reason==='failure')throw new Error('read refused')
    const payload=new Uint8Array(17);payload[0]=1;payload[4]=128
    if(reason==='bad-packet')payload[0]=0
    if(reason==='changed-device')(client as any).connectionGeneration++
    return {payload}
  })
  await client.readClockPartialRefreshCapability()
  expect(useDeviceStore.getState().capabilities.clockPartialRefresh).toBe(false)
})
