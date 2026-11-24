import { beforeEach, expect, it, vi } from 'vitest'
vi.mock('../data/db', () => ({ db: { devicePreferences: { get: vi.fn().mockResolvedValue(undefined) } } }))
import { InkTagClient } from './client'
import { CommandV2 } from './generatedCommandsV2'
import { useDeviceStore } from '../store/device'
beforeEach(() => { useDeviceStore.getState().resetDevice(); useDeviceStore.setState({ connection: 'connected', deviceId: 'fake-only' }) })
const caps = new Uint8Array([1, 0x41, 0, 0x10, 0, 192, 0, 8, 10, 1])
const status = new Uint8Array([1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])
it('retains validated NFC capabilities using only the two existing read commands', async () => {
  const client = new InkTagClient()
  const request = vi.fn(async (command: number) => ({ payload: command === CommandV2.GET_NFC_CAPABILITIES ? caps : status }))
  ;(client as unknown as { requestV2: typeof request }).requestV2 = request
  await client.requestNfcStatus()
  expect(request.mock.calls.map(([command]) => command)).toEqual([CommandV2.GET_NFC_CAPABILITIES, CommandV2.GET_NFC_STATUS])
  expect(useDeviceStore.getState().nfcCapabilities?.commandMask).toBe(1 << 4)
  useDeviceStore.getState().resetDevice()
  expect(useDeviceStore.getState().nfcCapabilities).toBeNull()
})
it('does not apply NFC readback after the device changes', async () => {
  const client = new InkTagClient()
  const request = vi.fn(async (command: number) => {
    if (command === CommandV2.GET_NFC_STATUS) useDeviceStore.setState({ deviceId: 'other-fake' })
    return { payload: command === CommandV2.GET_NFC_CAPABILITIES ? caps : status }
  })
  ;(client as unknown as { requestV2: typeof request }).requestV2 = request
  await client.requestNfcStatus()
  expect(useDeviceStore.getState().nfcCapabilities).toBeNull()
})

it('keeps independently validated commands visible when the later tag-status read fails', async () => {
  const client = new InkTagClient()
  const request = vi.fn(async (command: number) => {
    if (command === CommandV2.GET_NFC_STATUS) throw new Error('status unavailable')
    return { payload: caps }
  })
  ;(client as unknown as { requestV2: typeof request }).requestV2 = request
  await expect(client.requestNfcStatus()).rejects.toThrow('status unavailable')
  expect(useDeviceStore.getState().nfcCapabilities?.commandMask).toBe(1 << 4)
  expect(request.mock.calls.map(([command]) => command)).toEqual([CommandV2.GET_NFC_CAPABILITIES, CommandV2.GET_NFC_STATUS])
})

it('does not clear a different device capability response during stale status cleanup', async () => {
  const client = new InkTagClient()
  const replacement = { flags: 1, commandMask: 0, maxNdefBytes: 0, maxCommandPayload: 0, maxUidBytes: 0, commandFormatVersion: 1 }
  const request = vi.fn(async (command: number) => {
    if (command === CommandV2.GET_NFC_STATUS) useDeviceStore.setState({ deviceId: 'other-fake', nfcCapabilities: replacement })
    return { payload: command === CommandV2.GET_NFC_CAPABILITIES ? caps : status }
  })
  ;(client as unknown as { requestV2: typeof request }).requestV2 = request
  await client.requestNfcStatus()
  expect(useDeviceStore.getState().nfcCapabilities).toBe(replacement)
})

const inputConfig = new Uint8Array([1, 7, 0xff, 2, 1, 0, 3, 0, 9, 0, 0xff, 1, 5, 0])
it('retains the three already-correct four-byte button writes', async () => {
  const state = useDeviceStore.getState()
  useDeviceStore.setState({ settingsCapabilities: { ...state.settingsCapabilities, buttonActionMask: 0x6ff, buttonGestureMask: 7 } })
  const client = new InkTagClient()
  const request = vi.fn(async (_command: number, _payload: Uint8Array) => ({ payload: inputConfig }))
  ;(client as unknown as { requestV2: typeof request }).requestV2 = request
  await client.setButtonActions({ single: { action: 'nextPage', slotId: 0 }, double: { action: 'previousPage', slotId: 0 }, long: { action: 'toggleClearPrevious', slotId: 0 } })
  expect(request.mock.calls.map(([command, payload]) => [command, [...payload]])).toEqual([
    [CommandV2.SET_INPUT_CONFIG, [1, 2, 1, 0]],
    [CommandV2.SET_INPUT_CONFIG, [1, 3, 10, 0]],
    [CommandV2.SET_INPUT_CONFIG, [1, 4, 9, 0]],
  ])
})

it('has no NFC configuration writer on the user WebUI client',()=>{
 expect('setNfcTapSettings' in new InkTagClient()).toBe(false)
})
