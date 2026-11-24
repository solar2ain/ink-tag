import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { InkTagClient } from './client'
import { CommandV2 } from './generatedCommandsV2'
import { useDeviceStore } from '../store/device'

beforeEach(() => { vi.useFakeTimers(); vi.setSystemTime(new Date('2026-11-05T14:00:00Z')); useDeviceStore.getState().resetDevice() })
afterEach(() => { vi.restoreAllMocks(); vi.useRealTimers() })

function fixture() {
  const client = new InkTagClient()
  const device = { id: 'exact-return' } as BluetoothDevice
  const internal = client as unknown as { device: BluetoothDevice; protocolV2: boolean; v2WeekStart: number;
    connectionGeneration: number; requestV2: ReturnType<typeof vi.fn> }
  Object.assign(internal, { device, protocolV2: true, v2WeekStart: 1 })
  vi.spyOn(client, 'waitForPasswordAuthorization').mockResolvedValue(undefined)
  const response = new Uint8Array(7)
  new DataView(response.buffer).setUint32(0, Date.now() / 1000, true)
  new DataView(response.buffer).setInt16(4, -new Date().getTimezoneOffset(), true)
  response[6] = 1
  internal.requestV2 = vi.fn().mockImplementation(async opcode => ({ payload: opcode === CommandV2.GET_STATUS
    ? new Uint8Array([1, 1, 0, ...new Uint8Array(13)]) : response }))
  return { client, internal, response }
}

it('uses one ordinary SET_TIME then GET_TIME, retaining negotiated week start/timezone', async () => {
  const { client, internal } = fixture()
  await client.syncTimeAndVerify()
  expect(internal.requestV2.mock.calls.map(([opcode]) => opcode)).toEqual([CommandV2.GET_STATUS, CommandV2.SET_TIME, CommandV2.GET_TIME])
  const sent = internal.requestV2.mock.calls[1][1] as Uint8Array
  expect(sent[6]).toBe(1)
  expect(new DataView(sent.buffer).getInt16(4, true)).toBe(-new Date().getTimezoneOffset())
})

it.each(['time', 'timezone', 'week-start', 'format'] as const)('refuses wrong %s readback without another SET_TIME', async kind => {
  const { client, internal, response } = fixture()
  if (kind === 'time') new DataView(response.buffer).setUint32(0, 1, true)
  if (kind === 'timezone') new DataView(response.buffer).setInt16(4, 999, true)
  if (kind === 'week-start') response[6] = 0
  if (kind === 'format') internal.requestV2.mockImplementation(async opcode => ({ payload: opcode === CommandV2.GET_STATUS
    ? new Uint8Array([1, 1, 0, ...new Uint8Array(13)]) : new Uint8Array(1) }))
  await expect(client.syncTimeAndVerify()).rejects.toThrow('读回')
  expect(internal.requestV2.mock.calls.filter(([opcode]) => opcode === CommandV2.SET_TIME)).toHaveLength(1)
})

it('does not replay an unknown SET_TIME or read back a denied write', async () => {
  const { client, internal } = fixture()
  internal.requestV2.mockImplementation(async opcode => {
    if (opcode === CommandV2.GET_STATUS) return { payload: new Uint8Array([1, 1, 0, ...new Uint8Array(13)]) }
    throw new Error('SET_TIME timeout')
  })
  await expect(client.syncTimeAndVerify()).rejects.toThrow('timeout')
  expect(internal.requestV2.mock.calls.map(([opcode]) => opcode)).toEqual([CommandV2.GET_STATUS, CommandV2.SET_TIME])
})

it('rejects a changed connection before clock write', async () => {
  const { client, internal } = fixture()
  vi.mocked(client.waitForPasswordAuthorization).mockImplementation(async () => { internal.connectionGeneration++ })
  await expect(client.syncTimeAndVerify()).rejects.toThrow('连接已变化')
  expect(internal.requestV2).not.toHaveBeenCalled()
})

it('releases automatic-sync suppression after both success and rejection', async () => {
  const { client } = fixture()
  const state = client as unknown as { automaticClockSyncSuppression: number }
  await client.withoutAutomaticClockSync(async () => { expect(state.automaticClockSyncSuppression).toBe(1) })
  await expect(client.withoutAutomaticClockSync(async () => { throw new Error('failed') })).rejects.toThrow('failed')
  expect(state.automaticClockSyncSuppression).toBe(0)
})
