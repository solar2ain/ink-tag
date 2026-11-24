import { afterEach, beforeEach, expect, it, vi } from 'vitest'
vi.mock('../data/devicePreferences', () => ({ getDevicePreference: vi.fn(), rememberDeviceEdition: vi.fn(), updateDevicePreference: vi.fn() }))
import { InkTagClient, ProtocolV2Error } from './client'
import { CapabilityV2, CommandV2, DisplayModeV2, StatusV2 } from './generatedCommandsV2'
import { encodeScreenConfig } from './screen'
import { useDeviceStore } from '../store/device'

beforeEach(() => {
  vi.stubGlobal('window', globalThis); vi.stubGlobal('navigator', { bluetooth: {} })
  useDeviceStore.getState().resetDevice(); useDeviceStore.getState().clearLogs()
  useDeviceStore.setState({ connection: 'connected', deviceId: 'tag-A' })
})
afterEach(() => { useDeviceStore.getState().resetDevice(); vi.unstubAllGlobals() })
function fixture(capabilityMask = 0) {
  const client = new InkTagClient()
  const internal = client as unknown as { protocolV2: boolean; connectionGeneration: number; requestV2: ReturnType<typeof vi.fn> }
  internal.protocolV2 = true
  const caps = new Uint8Array(17); caps[0] = 1
  new DataView(caps.buffer).setUint32(1, capabilityMask, true)
  new DataView(caps.buffer).setUint16(5, 1 << DisplayModeV2.DEVICE_INFO, true)
  const identity = new Uint8Array(23); identity[0] = 1
  const status = new Uint8Array(16); status[0] = 1
  const screen = { schemaVersion: 1, driver: 0x13, recipe: 0, color: 2, transform: 0, nativeUiScale: 0,
    width: 400, height: 300, visibleX: 0, visibleY: 0, visibleWidth: 400, visibleHeight: 300 }
  let failScreen = false
  let inputError: Error | null = null
  const request = vi.fn(async (opcode: number) => {
    if (opcode === CommandV2.GET_SCREEN_PROFILE && failScreen) throw new Error('屏幕读取超时')
    if (opcode === CommandV2.GET_INPUT_CONFIG && inputError) throw inputError
    const replies = new Map<number, Uint8Array>([
      [CommandV2.GET_CAPABILITIES, caps], [CommandV2.GET_DEVICE_INFO, identity],
      [CommandV2.GET_SCREEN_PROFILE, encodeScreenConfig(screen)],
      [CommandV2.GET_TIME, new Uint8Array(7)], [CommandV2.GET_STATUS, status], [CommandV2.GET_INPUT_CONFIG, new Uint8Array(0)],
    ])
    const payload = replies.get(opcode)
    if (!payload) throw new Error(`Unexpected non-read command: ${opcode}`)
    return { payload }
  })
  internal.requestV2 = request
  return { client, internal, request, screen, failScreen: (fail: boolean) => { failScreen = fail }, failInput: (error: Error) => { inputError = error } }
}
it('does not call an explicitly unsupported optional input configuration incomplete', async () => {
  const f = fixture(CapabilityV2.INPUT)
  f.failInput(new ProtocolV2Error(StatusV2.UNSUPPORTED))
  await expect(f.client.refreshDeviceInfo()).resolves.toBe(true)
  expect(useDeviceStore.getState().screen).toEqual(f.screen)
  expect(useDeviceStore.getState().settingsCapabilities).toMatchObject({ buttonGestureMask: 0, buttonActionMask: 0, nfcActionMask: 0 })
  expect(useDeviceStore.getState().logs.some(log => log.level === 'info' && log.message === '设备不支持按键扩展配置，已跳过')).toBe(true)
})
it.each([StatusV2.TIMEOUT, StatusV2.INVALID_REQUEST, StatusV2.UNKNOWN_COMMAND, 'malformed'] as const)(
  'still reports incomplete optional input for %s instead of hiding an error', async error => {
    const f = fixture(CapabilityV2.INPUT)
    if (error !== 'malformed') f.failInput(new ProtocolV2Error(error))
    await expect(f.client.refreshDeviceInfo()).resolves.toBe(false)
    expect(useDeviceStore.getState().screen).toEqual(f.screen)
    expect(useDeviceStore.getState().logs.some(log => log.level === 'warning' && log.message.includes('按键扩展信息不可用'))).toBe(true)
  })
it('recovers a failed optional screen read using GET commands only, without HELLO or time sync', async () => {
  const f = fixture(); f.failScreen(true)
  await expect(f.client.refreshDeviceInfo()).resolves.toBe(false)
  expect(useDeviceStore.getState().screen).toBeNull()
  expect(useDeviceStore.getState().settingsCapabilities.nativeDeviceInfo).toBe(true)
  f.failScreen(false)
  await expect(f.client.refreshDeviceInfo()).resolves.toBe(true)
  expect(useDeviceStore.getState().screen).toEqual(f.screen)
  expect(f.request.mock.calls.map(call => call[0])).toEqual(Array.from({ length: 2 }, () => [
    CommandV2.GET_CAPABILITIES, CommandV2.GET_DEVICE_INFO, CommandV2.GET_SCREEN_PROFILE,
    CommandV2.GET_TIME, CommandV2.GET_STATUS,
  ]).flat())
})
it('propagates a core read failure and allows a later explicit retry', async () => {
  const f = fixture(); f.request.mockRejectedValueOnce(new Error('能力读取超时'))
  await expect(f.client.refreshDeviceInfo()).rejects.toThrow('能力读取超时')
  await expect(f.client.refreshDeviceInfo()).resolves.toBe(true)
})
it('stops before requesting the next record after the connection generation changes', async () => {
  const f = fixture()
  f.request.mockImplementationOnce(async () => { f.internal.connectionGeneration++; return { payload: new Uint8Array(17) } })
  await expect(f.client.refreshDeviceInfo()).rejects.toThrow('连接已改变')
  expect(f.request).toHaveBeenCalledExactlyOnceWith(CommandV2.GET_CAPABILITIES)
})
it.each(['disconnected', 'ota'])('does not send reads during %s', async mode => {
  const f = fixture()
  useDeviceStore.setState({ connection: mode === 'ota' ? 'connected' : 'idle', action: mode === 'ota' ? 'ota' : 'idle' })
  await expect(f.client.refreshDeviceInfo()).rejects.toThrow()
  expect(f.request).not.toHaveBeenCalled()
})
