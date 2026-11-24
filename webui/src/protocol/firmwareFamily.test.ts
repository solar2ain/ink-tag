import { afterEach, expect, it, vi } from 'vitest'
import { InkTagClient } from './client'
import { CapabilityV2, CommandV2, PROTOCOL_V2_VERSION } from './generatedCommandsV2'
import { resolveBoardProfile } from './boards'
import { useDeviceStore } from '../store/device'

afterEach(() => useDeviceStore.getState().resetDevice())

const families = [
  ['tlsr8359_hanshow', 'telink'], ['gr5331_zkel', 'goodix'], ['gr551x_zkel', 'goodix'],
  ['gr5513_zkel_internal', 'goodix'],
  ['om6626_zkcxxb', 'onmicro'], ['om6626_hsela', 'onmicro'],
] as const

it.each(families.flatMap(([identity, ota]) => [false, true].map(enabled => ({identity, ota, enabled}))))('routes $identity without a model/PCB lookup (OTA advertised: $enabled)', async ({identity, ota, enabled}) => {
  useDeviceStore.getState().resetDevice()
  const client = new InkTagClient() as unknown as {
    initializeV2Core(): Promise<number>
    requestV2(opcode: number): Promise<{payload: Uint8Array}>
  }
  const family = new TextEncoder().encode(identity)
  const name = new TextEncoder().encode('IKT_TL_4992')
  const info = new Uint8Array(23 + family.length + name.length)
  info.set([1, family.length, name.length, 0, 0])
  info.set(family, 23); info.set(name, 23 + family.length)
  const capabilities = new Uint8Array(17)
  capabilities[0] = 1
  new DataView(capabilities.buffer).setUint32(1, enabled ? CapabilityV2.OTA_RECOVERY : 0, true)
  client.requestV2 = vi.fn(async opcode => {
    if (opcode === CommandV2.HELLO) return {payload: new Uint8Array([1, PROTOCOL_V2_VERSION, 244, 0, 237, 0])}
    if (opcode === CommandV2.GET_CAPABILITIES) return {payload: capabilities}
    if (opcode === CommandV2.GET_DEVICE_INFO) return {payload: info}
    throw new Error(`Unexpected command ${opcode}`)
  })
  await client.initializeV2Core()
  const state = useDeviceStore.getState()
  expect(state.boardId).toBe(identity)
  expect(state.capabilities.ota).toBe(enabled ? ota : 'none')
  expect(state.capabilities.nfc).toBe('none')
  expect(state.capabilities.led).toBe('none')
  expect(state.capabilities.imageStore).toBe(false)
  expect(resolveBoardProfile(state.boardId)).toBeUndefined()
  expect(client.requestV2).toHaveBeenCalledTimes(3)
})
