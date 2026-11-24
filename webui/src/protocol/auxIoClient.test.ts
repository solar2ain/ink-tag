import { beforeEach, expect, it, vi } from 'vitest'
import { InkTagClient, ProtocolV2Error } from './client'
import { emptyDeviceSettingsCapabilities } from './deviceSettings'
import { CapabilityV2, CommandV2, StatusV2 } from './generatedCommandsV2'
import { decodeAuxIoConfiguration, emptyAuxIoConfiguration, encodeAuxIoConfiguration } from './auxIo'
import { encodePinBlock, pinRoles, PinWriteUnconfirmedError, type PinConfig } from './pinConfig'
import { encodeScreenConfig, makeScreenConfig } from './screen'
import { useDeviceStore } from '../store/device'

function setup() {
  const client = new InkTagClient()
  const internal = client as unknown as { protocolV2: boolean; connectionGeneration: number; requestV2: (op: number, payload?: Uint8Array) => Promise<{ payload: Uint8Array }> }
  internal.protocolV2 = true
  const request = vi.spyOn(internal, 'requestV2')
  return { client, internal, request }
}
const wire = () => encodeAuxIoConfiguration(emptyAuxIoConfiguration())
beforeEach(() => {
  useDeviceStore.getState().resetDevice()
  useDeviceStore.setState({ settingsCapabilities: { ...emptyDeviceSettingsCapabilities, customButtonGpio: true, reservedIo: true } })
})
it('reads all fixed slots, including a board-default first button, into connection state', async () => {
  const config = emptyAuxIoConfiguration(); config.buttons[0] = { gpio: 17, activeLow: true, pull: 'up' }
  const { client, request } = setup(); request.mockResolvedValue({ payload: encodeAuxIoConfiguration(config) })
  expect(await client.getAuxIoConfiguration()).toEqual(config)
  expect([...request.mock.calls[0][1]!]).toEqual([3])
  expect(useDeviceStore.getState().reportedAuxIoConfiguration?.buttons.map(b => b.gpio)).toEqual([17, 255, 255])
  expect(useDeviceStore.getState().reportedAuxIo?.buttonGpio).toBe(17)
})
it.each([StatusV2.UNSUPPORTED, StatusV2.INVALID_REQUEST])('does not retry an old empty selector after explicit status %s', async status => {
  const { client, request } = setup()
  request.mockRejectedValueOnce(new ProtocolV2Error(status)).mockResolvedValueOnce({ payload: new Uint8Array([1, 17, 3, 0, 255, 255, 0xd0, 7]) })
  await expect(client.getAuxIoConfiguration()).rejects.toBeInstanceOf(ProtocolV2Error)
  expect(request).toHaveBeenCalledTimes(1)
})
it.each([new ProtocolV2Error(StatusV2.TIMEOUT), new ProtocolV2Error(StatusV2.DENIED), new Error('GATT disconnected')])('does not fallback on timeout, denial or transport failure', async error => {
  const { client, request } = setup(); request.mockRejectedValue(error)
  await expect(client.getAuxIoConfiguration()).rejects.toBe(error); expect(request).toHaveBeenCalledTimes(1)
})
it.each([new Uint8Array([3]), new Uint8Array([1, 17, 3, 0, 255, 255, 0xd0, 7])])('does not fallback on malformed or wrong-version response', async payload => {
  const { client, request } = setup(); request.mockResolvedValue({ payload })
  await expect(client.getAuxIoConfiguration()).rejects.toThrow(); expect(request).toHaveBeenCalledTimes(1)
})
it('does not adopt a previous connection response', async () => {
  const { client, internal, request } = setup()
  request.mockImplementation(async () => { internal.connectionGeneration++; return { payload: wire() } })
  await expect(client.getAuxIoConfiguration()).rejects.toThrow(/设备已变化/)
  expect(useDeviceStore.getState().reportedAuxIoConfiguration).toBeNull()
})
it('writes three buttons once and requires an exact persisted ACK', async () => {
  const { client, request } = setup()
  const candidate = decodeAuxIoConfiguration(new Uint8Array([1, 1, 3, 2, 4, 3, 0, 3, 4, 255, 0xd0, 7]))
  candidate.reserved = Array.from(wire().subarray(7))
  const payload = encodeAuxIoConfiguration(candidate)
  request.mockResolvedValueOnce({ payload: wire() }).mockResolvedValueOnce({ payload })
  expect(await client.setAuxIoConfiguration(candidate)).toEqual(candidate)
  expect(request.mock.calls.map(c => c[0])).toEqual([CommandV2.GET_IO_CONFIG, CommandV2.SET_IO_CONFIG])
  expect(request.mock.calls[1][1]).toEqual(payload)
})
it.each([new Error('lost ACK'), new ProtocolV2Error(StatusV2.TIMEOUT), new Uint8Array([3])])('does not replay unknown write outcomes', async failure => {
  const { client, request } = setup(); request.mockResolvedValueOnce({ payload: wire() })
  if (failure instanceof Uint8Array) request.mockResolvedValueOnce({ payload: failure }); else request.mockRejectedValueOnce(failure)
  await expect(client.setAuxIoConfiguration(emptyAuxIoConfiguration())).rejects.toBeInstanceOf(PinWriteUnconfirmedError)
  expect(request).toHaveBeenCalledTimes(2)
  await expect(client.setAuxIoConfiguration(emptyAuxIoConfiguration())).rejects.toBeInstanceOf(PinWriteUnconfirmedError)
  await expect(client.setRuntimePinConfig(null)).rejects.toBeInstanceOf(PinWriteUnconfirmedError)
  expect(request).toHaveBeenCalledTimes(2)
})
it('preserves a known backend refusal without retry', async () => {
  const { client, request } = setup(); const error = new ProtocolV2Error(StatusV2.BUSY)
  request.mockResolvedValueOnce({ payload: wire() }).mockRejectedValueOnce(error)
  await expect(client.setAuxIoConfiguration(emptyAuxIoConfiguration())).rejects.toBe(error)
  expect(request).toHaveBeenCalledTimes(2)
})
it.each([StatusV2.PERSISTENCE_ERROR, StatusV2.IO_ERROR])('a post-commit failure status %s is not a safe admission refusal', async status => {
  const { client, request } = setup()
  request.mockResolvedValueOnce({ payload: wire() }).mockRejectedValueOnce(new ProtocolV2Error(status))
  await expect(client.setAuxIoConfiguration(emptyAuxIoConfiguration())).rejects.toBeInstanceOf(PinWriteUnconfirmedError)
  expect(useDeviceStore.getState().ioWriteUnconfirmed).toBe(true)
  await expect(client.setAuxIoConfiguration(emptyAuxIoConfiguration())).rejects.toBeInstanceOf(PinWriteUnconfirmedError)
  expect(request).toHaveBeenCalledTimes(2)
})

it.each([[false,true],[true,true],[false,false],[true,false]])('connection details read IO and BUSY; failure=%s writable=%s', async (failAux, writable) => {
  const { request, internal } = setup()
  useDeviceStore.setState({ settingsCapabilities: { ...emptyDeviceSettingsCapabilities, runtimePinConfig: true, customButtonGpio: writable } })
  const config: PinConfig = { activeLow: 960, pins: Object.fromEntries(pinRoles.map((role, i) => [role, i < 10 ? i : 255])) as PinConfig['pins'] }
  const runtime = new Uint8Array([1, 2, 0xff, 3, ...encodePinBlock(config), ...encodePinBlock(config)])
  const screen = makeScreenConfig({ driver: 0x24, color: 1, transform: 0, width: 400, height: 300, visibleX: 0, visibleY: 0, visibleWidth: 400, visibleHeight: 300 })
  request.mockImplementation(async (op, payload) => {
    if (op === CommandV2.GET_IO_CONFIG && payload?.[0] === 3) {
      if (failAux) throw Error('optional IO failed')
      return { payload: wire() }
    }
    if (op === CommandV2.GET_IO_CONFIG && payload?.[0] === 2) return { payload: runtime }
    if (op === CommandV2.GET_SCREEN_PROFILE) return { payload: encodeScreenConfig(screen) }
    if (op === CommandV2.GET_TIME) return { payload: new Uint8Array(7) }
    throw Error(`Unexpected operation ${op}`)
  })
  const details = internal as unknown as { requestTelemetry: () => Promise<void>; initializeV2Details: (mask: number) => Promise<void> }
  details.requestTelemetry = vi.fn(async () => {})
  await details.initializeV2Details(CapabilityV2.PIN_CONFIG | (writable ? CapabilityV2.IO : 0))
  expect(useDeviceStore.getState().reportedPinConfig?.effective).toEqual(config)
  expect(useDeviceStore.getState().reportedAuxIoConfiguration === null).toBe(failAux)
  expect(useDeviceStore.getState().screen).toEqual(screen)
  expect(request.mock.calls.filter(call => call[0] === CommandV2.GET_IO_CONFIG).map(call => [...call[1]!])).toEqual([[3], [2]])
})

it('PIN_CONFIG reads built-in slot1 but cannot write even an all-disabled AUX candidate', async () => {
  useDeviceStore.setState({ settingsCapabilities: { ...emptyDeviceSettingsCapabilities, runtimePinConfig: true } })
  const { client, request } = setup()
  const config=emptyAuxIoConfiguration();config.buttons[0]={gpio:2,activeLow:true,pull:'up'}
  request.mockResolvedValue({payload:encodeAuxIoConfiguration(config)})
  expect((await client.getAuxIoConfiguration()).buttons.map(b=>b.gpio)).toEqual([2,255,255])
  expect(useDeviceStore.getState().reportedAuxIo?.buttonGpio).toBe(2)
  for(const candidate of [config,emptyAuxIoConfiguration()]) await expect(client.setAuxIoConfiguration(candidate)).rejects.toThrow(/未开放/)
  expect(request).toHaveBeenCalledTimes(1)
  expect([...request.mock.calls[0][1]!]).toEqual([3])
})

it('PIN_CONFIG-only UNSUPPORTED remains a failed read without probing an old selector', async () => {
  useDeviceStore.setState({ settingsCapabilities: { ...emptyDeviceSettingsCapabilities, runtimePinConfig: true } })
  const {client,request}=setup();const error=new ProtocolV2Error(StatusV2.UNSUPPORTED)
  request.mockRejectedValue(error)
  await expect(client.getAuxIoConfiguration()).rejects.toBe(error)
  expect(request).toHaveBeenCalledTimes(1)
  expect(useDeviceStore.getState().reportedAuxIoConfiguration).toBeNull()
})
