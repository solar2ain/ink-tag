import { expect, it, vi } from 'vitest'
import { decodeAdvertisingConfig, encodeAdvertisingConfig } from './advertising'
import { InkTagClient, ProtocolV2Error } from './client'
import { CommandV2, StatusV2 } from './generatedCommandsV2'

const wire = Uint8Array.of(1, 1, 120, 0)
function setup() {
  const client = new InkTagClient()
  const internal = client as unknown as { connectionGeneration: number; requestV2: (op: number, payload?: Uint8Array) => Promise<{ payload: Uint8Array }> }
  return { client, internal, request: vi.spyOn(internal, 'requestV2') }
}
it('roundtrips both modes and full protocol delay range', () => {
  for (const mode of ['always', 'delayed'] as const) for (const timeoutSeconds of [1, 120, 255, 256, 65535]) {
    expect(decodeAdvertisingConfig(encodeAdvertisingConfig({ mode, timeoutSeconds }))).toEqual({ mode, timeoutSeconds })
  }
})
it.each([[], [1, 1, 120], [1, 1, 120, 0, 0], [2, 1, 120, 0], [1, 2, 120, 0], [1, 0, 0, 0]])('rejects malformed response %j', (...values) => {
  expect(() => decodeAdvertisingConfig(Uint8Array.from(values))).toThrow()
})
it.each([0, 65536, 1.5, NaN])('does not send invalid delay %s', async timeoutSeconds => {
  const { client, request } = setup()
  await expect(client.setAdvertisingConfig({ mode: 'delayed', timeoutSeconds })).rejects.toThrow()
  expect(request).not.toHaveBeenCalled()
})
it('discovers unsupported firmware without inventing support from board name', async () => {
  const { client, request } = setup(); request.mockRejectedValue(new ProtocolV2Error(StatusV2.UNSUPPORTED))
  expect(await client.getAdvertisingConfig()).toBeNull()
  expect(request).toHaveBeenCalledTimes(1)
})
it.each([StatusV2.TIMEOUT, StatusV2.DENIED, StatusV2.IO_ERROR])('preserves read failure %s', async status => {
  const { client, request } = setup(); request.mockRejectedValue(new ProtocolV2Error(status))
  await expect(client.getAdvertisingConfig()).rejects.toBeInstanceOf(ProtocolV2Error)
})
it('writes once then requires exact echo and readback', async () => {
  const { client, request } = setup(); request.mockResolvedValue({ payload: wire })
  expect(await client.setAdvertisingConfig({ mode: 'delayed', timeoutSeconds: 120 })).toEqual({ mode: 'delayed', timeoutSeconds: 120 })
  expect(request.mock.calls.map(call => call[0])).toEqual([CommandV2.SET_ADVERTISING_CONFIG, CommandV2.GET_ADVERTISING_CONFIG])
  expect(request.mock.calls[0][1]).toEqual(wire)
})
it('does not repeat SET after an unknown outcome', async () => {
  const { client, request } = setup(); request.mockRejectedValue(new ProtocolV2Error(StatusV2.TIMEOUT))
  await expect(client.setAdvertisingConfig({ mode: 'delayed', timeoutSeconds: 120 })).rejects.toThrow()
  expect(request).toHaveBeenCalledTimes(1)
})
it('rejects mismatched echo and readback without repeating the write', async () => {
  for (const echo of [true, false]) {
    const { client, request } = setup()
    request.mockResolvedValueOnce({ payload: echo ? Uint8Array.of(1, 0, 120, 0) : wire }).mockResolvedValueOnce({ payload: Uint8Array.of(1, 0, 120, 0) })
    await expect(client.setAdvertisingConfig({ mode: 'delayed', timeoutSeconds: 120 })).rejects.toThrow()
    expect(request.mock.calls.filter(call => call[0] === CommandV2.SET_ADVERTISING_CONFIG)).toHaveLength(1)
  }
})
it('rejects a reply from an obsolete connection', async () => {
  const { client, internal, request } = setup()
  request.mockImplementation(async () => { internal.connectionGeneration++; return { payload: wire } })
  await expect(client.getAdvertisingConfig()).rejects.toThrow(/连接已改变/)
})
