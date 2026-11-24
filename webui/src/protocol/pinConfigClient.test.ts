import { beforeEach, describe, expect, it, vi } from 'vitest'
import { InkTagClient, ProtocolV2Error } from './client'
import { emptyDeviceSettingsCapabilities } from './deviceSettings'
import { CommandV2, StatusV2 } from './generatedCommandsV2'
import { encodePinBlock, pinRoles, PinWriteUnconfirmedError, type PinConfig } from './pinConfig'
import { useDeviceStore } from '../store/device'
import { emptyAuxIoConfiguration, encodeAuxIoConfiguration } from './auxIo'

const config = (): PinConfig => ({ activeLow: 0, pins: Object.fromEntries(pinRoles.map((role, i) => [role, i < 10 ? i : 255])) as PinConfig['pins'] })
const report = (flags = 2) => new Uint8Array([1, flags, 0xff, 3, ...encodePinBlock(config()), ...encodePinBlock(config())])
function setup() {
  const client = new InkTagClient()
  const internal = client as unknown as { protocolV2: boolean; connectionGeneration: number; requestV2: (op: number, payload?: Uint8Array) => Promise<{ payload: Uint8Array }> }
  internal.protocolV2 = true
  const request = vi.spyOn(internal, 'requestV2')
  return { client, internal, request }
}
beforeEach(() => { useDeviceStore.getState().resetDevice(); useDeviceStore.setState({ settingsCapabilities: { ...emptyDeviceSettingsCapabilities, runtimePinConfig: true } }) })
describe('actual V2 pin client', () => {
  it.each([config(), null])('refuses every write including defaults on a read-only report', async candidate => {
    const { client, request } = setup()
    const readOnly = report(); readOnly[2] = 0; readOnly[3] = 0
    request.mockResolvedValue({ payload: readOnly })
    await expect(client.setRuntimePinConfig(candidate)).rejects.toThrow('只读')
    expect(request.mock.calls.map(call => call[0])).toEqual([CommandV2.GET_IO_CONFIG])
  })
  it('does not infer new pins from the legacy IO capability', async () => {
    useDeviceStore.setState({ settingsCapabilities: { ...emptyDeviceSettingsCapabilities, customButtonGpio: true, reservedIo: true } })
    const { client, request } = setup()
    await expect(client.setRuntimePinConfig(config())).rejects.toThrow(/未声明/)
    expect(request).not.toHaveBeenCalled()
  })
  it('writes one complete candidate and consumes effective ACK', async () => {
    const { client, request } = setup()
    request.mockResolvedValueOnce({ payload: report() }).mockResolvedValueOnce({payload:encodeAuxIoConfiguration(emptyAuxIoConfiguration())}).mockResolvedValueOnce({ payload: report(3) })
    expect((await client.setRuntimePinConfig(config())).overridePresent).toBe(true)
    expect(request.mock.calls.map(call => call[0])).toEqual([CommandV2.GET_IO_CONFIG, CommandV2.GET_IO_CONFIG, CommandV2.SET_IO_CONFIG])
    expect(Array.from(request.mock.calls[0][1]!)).toEqual([2])
    expect(Array.from(request.mock.calls[1][1]!)).toEqual([3])
    expect(Array.from(request.mock.calls[2][1]!)).toEqual([2, 1, ...encodePinBlock(config())])
  })
  it('restores defaults with 0200 rather than rewriting the factory', async () => {
    const { client, request } = setup()
    request.mockResolvedValue({ payload: report() })
    await client.setRuntimePinConfig(null)
    expect(Array.from(request.mock.calls[1][1]!)).toEqual([2, 0])
  })
  it.each([new Error('GATT disconnected'), new ProtocolV2Error(StatusV2.TIMEOUT), new Error('lost ACK')])('an unknown outcome never replays', async error => {
    const { client, request } = setup()
    request.mockResolvedValueOnce({ payload: report() }).mockResolvedValueOnce({payload:encodeAuxIoConfiguration(emptyAuxIoConfiguration())}).mockRejectedValueOnce(error)
    await expect(client.setRuntimePinConfig(config())).rejects.toBeInstanceOf(PinWriteUnconfirmedError)
    expect(request).toHaveBeenCalledTimes(3)
  })
  it('preserves a known busy refusal', async () => {
    const { client, request } = setup()
    const busy = new ProtocolV2Error(StatusV2.BUSY)
    request.mockResolvedValueOnce({ payload: report() }).mockResolvedValueOnce({payload:encodeAuxIoConfiguration(emptyAuxIoConfiguration())}).mockRejectedValueOnce(busy)
    await expect(client.setRuntimePinConfig(config())).rejects.toBe(busy)
    expect(request).toHaveBeenCalledTimes(3)
  })
  it.each([StatusV2.PERSISTENCE_ERROR, StatusV2.IO_ERROR])('locks post-commit pin failure %s without retry', async status => {
    const { client, request } = setup()
    request.mockResolvedValueOnce({ payload: report() }).mockResolvedValueOnce({payload:encodeAuxIoConfiguration(emptyAuxIoConfiguration())}).mockRejectedValueOnce(new ProtocolV2Error(status))
    await expect(client.setRuntimePinConfig(config())).rejects.toBeInstanceOf(PinWriteUnconfirmedError)
    expect(useDeviceStore.getState().ioWriteUnconfirmed).toBe(true)
    await expect(client.setRuntimePinConfig(config())).rejects.toBeInstanceOf(PinWriteUnconfirmedError)
    expect(request).toHaveBeenCalledTimes(3)
  })
  it('rejects unavailable TXD/RXD roles before SET', async () => {
    const { client, request } = setup()
    request.mockResolvedValue({ payload: report() })
    const changed = config(); changed.pins.txd = 30
    await expect(client.setRuntimePinConfig(changed)).rejects.toThrow(/txd/i)
    expect(request).not.toHaveBeenCalled()
  })
  it('rejects a connection-generation change before SET', async () => {
    const { client, internal, request } = setup()
    request.mockImplementation(async () => { internal.connectionGeneration++; return { payload: report() } })
    await expect(client.setRuntimePinConfig(config())).rejects.toThrow(/连接已变化/)
    expect(request).toHaveBeenCalledTimes(1)
  })
  it('checks real aux readback before changing display pins', async () => {
    useDeviceStore.setState({ settingsCapabilities: { ...emptyDeviceSettingsCapabilities, runtimePinConfig: true, customButtonGpio: true } })
    const { client, request } = setup()
    const aux = emptyAuxIoConfiguration(); aux.buttons[2] = { gpio: 3, activeLow: true, pull: 'up' }
    request.mockResolvedValueOnce({ payload: report() }).mockResolvedValueOnce({ payload: encodeAuxIoConfiguration(aux) })
    await expect(client.setRuntimePinConfig(config())).rejects.toThrow(/冲突/)
    expect(request.mock.calls.every(call => call[0] === CommandV2.GET_IO_CONFIG)).toBe(true)
  })
})
