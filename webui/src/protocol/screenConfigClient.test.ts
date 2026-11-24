import { beforeEach, expect, it, vi } from 'vitest'
import { InkTagClient, ProtocolV2Error } from './client'
import { CommandV2, StatusV2 } from './generatedCommandsV2'
import { encodeScreenConfig, ScreenWriteUnconfirmedError } from './screen'
import { useDeviceStore } from '../store/device'
import type { ScreenConfig } from '../types'

const screen: ScreenConfig = { schemaVersion: 1, driver: 0x24, recipe: 1, color: 2,
  transform: 0, nativeUiScale: 0, width: 400, height: 300, visibleX: 0, visibleY: 0,
  visibleWidth: 400, visibleHeight: 300 }
function setup() {
  const client = new InkTagClient()
  const request = vi.fn()
  Object.assign(client, { protocolV2: true, requestV2: request,
    refreshV2ResourceInfo: vi.fn().mockResolvedValue(undefined) })
  useDeviceStore.setState({ deviceId: 'original', screen })
  return { client, request }
}
beforeEach(() => useDeviceStore.getState().resetDevice())

it.each([StatusV2.PERSISTENCE_ERROR, StatusV2.TIMEOUT, StatusV2.IO_ERROR, StatusV2.INTERNAL_ERROR,
  'disconnect', 'malformed', 'mismatch'])('keeps unknown screen result %s unconfirmed without replay', async failure => {
  const { client, request } = setup()
  if (typeof failure === 'number') request.mockRejectedValue(new ProtocolV2Error(failure))
  else if (failure === 'disconnect') request.mockRejectedValue(new Error('GATT disconnected'))
  else request.mockResolvedValue({ payload: failure === 'malformed' ? new Uint8Array()
    : encodeScreenConfig({ ...screen, color: 1 }) })
  await expect(client.setScreen(screen)).rejects.toBeInstanceOf(ScreenWriteUnconfirmedError)
  expect(useDeviceStore.getState()).toMatchObject({ screen: null, reportedPinConfig: null,
    imageStoreInfo: null, imageStoreItems: [], ioWriteUnconfirmed: true })
  // A later displayed observation must not unlock or acknowledge the old write.
  useDeviceStore.setState({ screen })
  await expect(client.setScreen(screen)).rejects.toBeInstanceOf(ScreenWriteUnconfirmedError)
  expect(request).toHaveBeenCalledExactlyOnceWith(CommandV2.SET_SCREEN_PROFILE, encodeScreenConfig(screen))
})

it.each([StatusV2.BUSY, StatusV2.LOW_POWER, StatusV2.INCOMPATIBLE, StatusV2.DENIED])(
  'preserves explicit preflight refusal %s', async status => {
    const { client, request } = setup()
    const failure = new ProtocolV2Error(status)
    request.mockRejectedValue(failure)
    await expect(client.setScreen(screen)).rejects.toBe(failure)
    expect(useDeviceStore.getState().screen).toEqual(screen)
    expect(useDeviceStore.getState().ioWriteUnconfirmed).toBe(false)
  })

it.each([false, true])('does not apply a late ACK to a new identity/connection: %s', async generation => {
  const { client, request } = setup()
  request.mockImplementation(async () => {
    if (generation) Object.assign(client, { connectionGeneration: 900 })
    useDeviceStore.setState({ deviceId: 'new', screen: null })
    return { payload: encodeScreenConfig(screen) }
  })
  await expect(client.setScreen(screen)).rejects.toBeInstanceOf(ScreenWriteUnconfirmedError)
  expect(useDeviceStore.getState()).toMatchObject({ deviceId: 'new', screen: null, ioWriteUnconfirmed: false })
  expect(request).toHaveBeenCalledTimes(1)
})

it('still requires an initialized V2 session before sending', async () => {
  const client = new InkTagClient()
  const write = vi.fn(); Object.assign(client, { writePacket: write })
  await expect(client.setScreen(screen)).rejects.toThrow('设备未启用当前 v1 协议')
  expect(write).not.toHaveBeenCalled()
  expect(useDeviceStore.getState().ioWriteUnconfirmed).toBe(false)
})

it('does not return an old connection result after the read-only refresh switches devices', async () => {
  const { client, request } = setup()
  request.mockResolvedValue({ payload: encodeScreenConfig(screen) })
  Object.assign(client, { refreshV2ResourceInfo: async () => {
    useDeviceStore.setState({ deviceId: 'next', screen: null })
  } })
  await expect(client.setScreen(screen)).rejects.toBeInstanceOf(ScreenWriteUnconfirmedError)
  expect(useDeviceStore.getState()).toMatchObject({ deviceId: 'next', screen: null, ioWriteUnconfirmed: false })
  expect(request).toHaveBeenCalledTimes(1)
})
