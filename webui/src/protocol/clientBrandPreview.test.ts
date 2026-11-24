import { beforeEach, expect, it, vi } from 'vitest'
import { InkTagClient } from './client'
import { SettingsSectionV2 } from './generatedCommandsV2'
import { DISPLAY_SETTINGS_PROTOCOL_VERSION, DISPLAY_SETTINGS_BRAND_HIDDEN } from './deviceSettings'
import { useDeviceStore } from '../store/device'

beforeEach(()=>useDeviceStore.getState().resetDevice())
const response=(hidden:boolean)=>({payload:new Uint8Array([SettingsSectionV2.DISPLAY,DISPLAY_SETTINGS_PROTOCOL_VERSION,0,hidden?DISPLAY_SETTINGS_BRAND_HIDDEN:0,0,0,0,0,0])})
it('shares device-reported and write-confirmed visibility with all previews, then clears it on disconnect',async()=>{
  const client=new InkTagClient() as any;client.protocolV2=true
  client.requestV2=vi.fn().mockResolvedValue(response(false))
  await client.getDisplaySettings();expect(useDeviceStore.getState().brandVisible).toBe(true)
  client.requestV2.mockResolvedValue(response(true))
  await client.setBrandVisible(false);expect(useDeviceStore.getState().brandVisible).toBe(false)
  useDeviceStore.getState().resetDevice();expect(useDeviceStore.getState().brandVisible).toBeNull()
})
it.each(['read','write'] as const)('does not apply an old %s reply to a new connection',async(mode)=>{
  const client=new InkTagClient() as any;client.protocolV2=true
  client.requestV2=vi.fn(async()=>{client.connectionGeneration++;return response(true)})
  await expect(mode==='read'?client.getDisplaySettings():client.writeDisplaySettings({weekStart:0,bootRefreshEnabled:false,brandVisible:false,countdownTargetDay:0,countdownName:''})).rejects.toThrow('设备连接已改变')
  expect(useDeviceStore.getState().brandVisible).toBeNull()
})
