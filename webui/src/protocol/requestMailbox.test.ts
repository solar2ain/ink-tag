import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { InkTagClient, ProtocolV2Error } from './client'
import { CommandV2, RequestFlagV2, StatusV2 } from './generatedCommandsV2'
import { useDeviceStore } from '../store/device'

beforeEach(() => {
  vi.useFakeTimers(); vi.stubGlobal('window', { setTimeout, clearTimeout })
  useDeviceStore.getState().resetDevice(); useDeviceStore.setState({ connection: 'connected' })
})
afterEach(() => { vi.restoreAllMocks(); vi.unstubAllGlobals(); vi.useRealTimers() })
function fixture() {
  const client = new InkTagClient() as any, writes: Uint8Array[] = []
  client.protocolV2 = true
  client.characteristic = { writeValueWithResponse: vi.fn(async (bytes: Uint8Array) => { writes.push(Uint8Array.from(bytes)) }) }
  const ack = (index: number, status = StatusV2.OK) => {
    const packet = writes[index]
    client.handleV2Notification(new Uint8Array([packet[0]!, packet[1]!, status]))
  }
  return { client, writes, ack }
}
it('keeps connected while background SET_TIME owns the mailbox, then queues independent details through each reply', async () => {
  const p = fixture()
  const time = p.client.requestV2(CommandV2.SET_TIME, new Uint8Array(6))
  const details = [CommandV2.GET_SCREEN_PROFILE, CommandV2.GET_INPUT_CONFIG, CommandV2.GET_STATUS]
    .map(op => p.client.requestV2(op))
  await vi.advanceTimersByTimeAsync(3252)
  expect(p.writes.map(b => b[0])).toEqual([CommandV2.SET_TIME])
  expect(useDeviceStore.getState().connection).toBe('connected')
  p.ack(0); await time
  for (let n = 0; n < details.length; n++) {
    await vi.advanceTimersByTimeAsync(0)
    expect(p.writes).toHaveLength(n + 2)
    p.ack(n + 1); await details[n]
  }
  expect(p.writes.map(b => b[0])).toEqual([CommandV2.SET_TIME, ...[CommandV2.GET_SCREEN_PROFILE, CommandV2.GET_INPUT_CONFIG, CommandV2.GET_STATUS]])
})
it('starts each response timeout on dispatch, releases a failed request and never replays it', async () => {
  const p = fixture()
  const first = p.client.requestV2(CommandV2.HELLO, new Uint8Array(), 0, 25)
  const failed = expect(first).rejects.toBeInstanceOf(ProtocolV2Error)
  const next = p.client.requestV2(CommandV2.GET_STATUS, new Uint8Array(), 0, 25)
  await vi.advanceTimersByTimeAsync(26); await failed
  expect(p.writes.map(b => b[0])).toEqual([CommandV2.HELLO, CommandV2.GET_STATUS])
  p.ack(1); await next
  expect(p.writes).toHaveLength(2)
})
it('releases the mailbox on the initial ACK, without waiting for long display COMPLETE', async () => {
  const p = fixture()
  const display = p.client.requestV2(CommandV2.DISPLAY_ACTION, new Uint8Array([1]))
  const stop = p.client.requestV2(CommandV2.DISPLAY_ACTION, new Uint8Array([0]))
  await vi.advanceTimersByTimeAsync(0)
  expect(p.writes).toHaveLength(1)
  p.ack(0); await display; await vi.advanceTimersByTimeAsync(0)
  expect(p.writes).toHaveLength(2)
  p.ack(1); await stop // no COMPLETE event was needed to dispatch STOP
})
it('revokes old queued requests on a connection change without touching the new peer', async () => {
  const p = fixture(), old: Promise<any>[] = [p.client.requestV2(CommandV2.SET_TIME), p.client.requestV2(CommandV2.GET_STATUS)]
  const rejected = Promise.all(old.map(p => expect(p).rejects.toThrow()))
  await vi.advanceTimersByTimeAsync(0)
  const oldCharacteristic = p.client.characteristic
  p.client.connectionGeneration++; p.client.resetImageTransport(); p.client.protocolV2 = true
  p.client.characteristic = { writeValueWithResponse: vi.fn(async (bytes: Uint8Array) => {
    p.client.handleV2Notification(new Uint8Array([bytes[0]!, bytes[1]!, StatusV2.OK]))
  }) }
  await p.client.requestV2(CommandV2.HELLO); await rejected
  expect(oldCharacteristic.writeValueWithResponse).toHaveBeenCalledOnce()
  expect(p.client.characteristic.writeValueWithResponse).toHaveBeenCalledOnce()
})
it('never sends a timed-out packet later or lets its late write error reject a reused transaction', async () => {
  const p = fixture(); let release!: () => void
  p.client.characteristic.writeValueWithResponse.mockImplementationOnce(async (bytes: Uint8Array) => {
    p.writes.push(Uint8Array.from(bytes)); await new Promise<void>(resolve => { release = resolve })
  })
  const time = p.client.requestV2(CommandV2.SET_TIME)
  await vi.advanceTimersByTimeAsync(0); p.ack(0); await time
  const stale = p.client.requestV2(CommandV2.GET_STATUS, new Uint8Array(), RequestFlagV2.NONE, 25, 42)
  const expired = expect(stale).rejects.toBeInstanceOf(ProtocolV2Error)
  await vi.advanceTimersByTimeAsync(26); await expired
  const abort = p.client.requestV2(CommandV2.TRANSFER_ABORT, new Uint8Array(), RequestFlagV2.NONE, 25, 42)
  await vi.advanceTimersByTimeAsync(0); release(); await vi.advanceTimersByTimeAsync(0)
  expect(p.writes.map(b => b[0])).toEqual([CommandV2.SET_TIME, CommandV2.TRANSFER_ABORT])
  p.ack(1); await abort
})
