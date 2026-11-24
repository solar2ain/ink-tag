import { afterEach, expect, it, vi } from 'vitest'
import { clockDiagnostic } from './clockDiagnostic'
import { InkTagClient } from './client'
import { CommandV2 } from './generatedCommandsV2'

const utc = 1791436800
const payload = (zone = 480) => { const data = new Uint8Array(7); const view = new DataView(data.buffer); view.setUint32(0, utc, true); view.setInt16(4, zone, true); data[6] = 1; return data }
afterEach(() => vi.restoreAllMocks())
it('compares UTC midpoint independently of device timezone and reports conservative uncertainty', () => {
  const plus = clockDiagnostic(payload(), utc * 1000 - 1100, utc * 1000 - 900, 200)
  expect(plus.offsetFromHostSeconds).toBe(1)
  expect(plus.uncertaintySeconds).toBe(1.1)
  expect(plus.deviceLocalTime).toContain('+08:00')
  expect(clockDiagnostic(payload(-300), utc * 1000 - 1100, utc * 1000 - 900, 200).offsetFromHostSeconds).toBe(1)
  expect(clockDiagnostic(payload(), utc * 1000 + 900, utc * 1000 + 1100, 200).offsetFromHostSeconds).toBe(-1)
})
it('does not present clock jumps or uninitialized device time as valid offsets', () => {
  expect(clockDiagnostic(payload(), utc * 1000, utc * 1000 + 1000, 100).offsetFromHostSeconds).toBeNull()
  const empty = payload(); empty.fill(0, 0, 4)
  expect(clockDiagnostic(empty, utc * 1000, utc * 1000 + 100, 100).deviceTimeValid).toBe(false)
  for (const invalid of [new Uint8Array(6), new Uint8Array(8), payload(841)]) expect(() => clockDiagnostic(invalid, 0, 0, 0)).toThrow()
})
it('shipping client issues exactly one GET_TIME, never SET_TIME or display work', async () => {
  const client = new InkTagClient()
  const request = vi.fn().mockResolvedValue({payload: payload()})
  Object.assign(client, {requestV2: request})
  vi.spyOn(Date, 'now').mockReturnValue(utc * 1000)
  const result = await client.readClockDiagnostic()
  expect(result.readOnly).toBe(true)
  expect(request.mock.calls).toEqual([[CommandV2.GET_TIME]])
})
it('keeps request failure and changed connection unconfirmed without retry', async () => {
  const client = new InkTagClient()
  const request = vi.fn().mockRejectedValue(new Error('timeout'))
  Object.assign(client, {requestV2: request})
  await expect(client.readClockDiagnostic()).rejects.toThrow('timeout')
  expect(request).toHaveBeenCalledTimes(1)
  request.mockImplementation(async () => { Object.assign(client, {connectionGeneration: 9}); return {payload: payload()} })
  await expect(client.readClockDiagnostic()).rejects.toThrow('连接已改变')
})
