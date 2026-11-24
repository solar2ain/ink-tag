import { expect, it, vi } from 'vitest'
import { screenRequiresPro, encodeScreenConfig } from './screen'
import { builtInScreenProfiles } from '../data/screenProfiles'
import { InkTagClient, ProtocolV2Error } from './client'
import { StatusV2 } from './generatedCommandsV2'
import { useDeviceStore } from '../store/device'

it('classifies all four E6 recipes without charging for six-color offline conversion', () => {
  const e6=builtInScreenProfiles.filter(p=>p.config.color===10)
  expect(e6).toHaveLength(4)
  for(const p of builtInScreenProfiles) {
    expect(screenRequiresPro(p.config)).toBe(p.config.color===10)
    expect(encodeScreenConfig(p.config)).toHaveLength(17)
  }
  expect(screenRequiresPro({driver:0x31,recipe:1})).toBe(false)
  expect(screenRequiresPro({driver:0x32,recipe:0})).toBe(false)
})

it.each([StatusV2.DENIED,StatusV2.UNSUPPORTED])('preserves real E6 device refusal %s without invalidating configuration',async status=>{
  useDeviceStore.getState().resetDevice()
  const screen=builtInScreenProfiles.find(p=>p.id==='osprey_154_e6')!.config
  useDeviceStore.setState({screen,deviceId:'e6-device'})
  const client=new InkTagClient()
  const request=vi.fn().mockRejectedValue(new ProtocolV2Error(status))
  Object.assign(client,{protocolV2:true,requestV2:request})
  await expect(client.setScreen(screen)).rejects.toMatchObject({status})
  if(status===StatusV2.DENIED)await expect(client.setScreen(screen)).rejects.toThrow('E6 屏幕控制需要')
  expect(useDeviceStore.getState().screen).toEqual(screen)
  expect(useDeviceStore.getState().ioWriteUnconfirmed).toBe(false)
  useDeviceStore.getState().resetDevice()
})
