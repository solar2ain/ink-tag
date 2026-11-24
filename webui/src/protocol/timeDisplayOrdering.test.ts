import { afterEach, beforeEach, expect, it, vi } from 'vitest'

vi.mock('../data/db', () => ({ db: { devicePreferences: { get: vi.fn() } } }))
vi.mock('./ota', () => ({}))

import { InkTagClient, ProtocolV2Error } from './client'
import { CommandV2, EventV2, StatusV2 } from './generatedCommandsV2'
import { useDeviceStore } from '../store/device'

beforeEach(() => {
  Object.defineProperty(globalThis, 'window', { configurable: true, value: globalThis })
  useDeviceStore.getState().resetDevice()
  vi.useFakeTimers()
})
afterEach(() => vi.useRealTimers())

function fixture() {
  const client = new InkTagClient()
  const internal = client as unknown as {
    protocolV2: boolean
    server: { connected: boolean } | null
    device: BluetoothDevice
    connectionGeneration: number
    writePacket: (packet: Uint8Array) => Promise<void>
    handleV2Notification: (packet: Uint8Array) => void
    refreshTelemetryAfterDisplay: (generation: number) => Promise<void>
    v2Completions: Map<number, unknown>
    v2Pending: Map<number, unknown>
    runV2DisplayWork: (mode: number) => Promise<Uint8Array>
  }
  internal.protocolV2 = true
  internal.server = { connected: true }
  internal.device = { id: 'exact-time-fixture' } as BluetoothDevice
  internal.refreshTelemetryAfterDisplay = vi.fn().mockResolvedValue(undefined)
  const state = {
    busy: 1, flags: [1, 0], malformed: false, disconnect: false,
    timeStatus: StatusV2.OK as number,
    displayStatus: undefined as number | undefined,
    completionStatus: StatusV2.OK as number,
    failWrite: false, omitCompletion: false, lateOldCompletion: false, cancelWait: false,
    lateOldAction: '', calls: [] as Uint8Array[],
  }
  internal.writePacket = async (packet) => {
    state.calls.push(packet.slice())
    const [opcode, transaction] = packet
    let status = StatusV2.OK as number
    let reply = new Uint8Array(0)
    if (opcode === CommandV2.SET_TIME) status = state.timeStatus
    else if (opcode === CommandV2.GET_STATUS) {
      const afterTime = state.calls.some(packet => packet[0] === CommandV2.SET_TIME)
      if (!afterTime) {
        internal.handleV2Notification(new Uint8Array([opcode, transaction, StatusV2.OK, 1, 1, 0, ...new Uint8Array(13)]))
        return
      }
      if (state.disconnect) internal.connectionGeneration += 1
      const flags = state.flags.length > 1 ? state.flags.shift()! : state.flags[0]
      reply = new Uint8Array(state.malformed ? 3 : 16)
      reply.set([1, 1, flags])
      if (state.lateOldCompletion) {
        const old = state.calls.find((p) => p[0] === CommandV2.SET_DISPLAY_MODE)!
        internal.handleV2Notification(new Uint8Array([EventV2.COMPLETE, 0, StatusV2.OK, CommandV2.SET_DISPLAY_MODE, old[1], 0]))
        state.lateOldAction = useDeviceStore.getState().action
      }
      if (state.cancelWait) void client.stop()
    } else if (opcode === CommandV2.SET_DISPLAY_MODE) {
      if (state.failWrite) throw new Error('GATT disconnected before ACK')
      if (state.displayStatus !== undefined) status = state.displayStatus
      else if (state.busy > 0) { state.busy -= 1; status = StatusV2.BUSY }
    } else if (opcode !== CommandV2.CANCEL) throw new Error(`Unexpected opcode ${opcode}; no reset is permitted`)
    internal.handleV2Notification(new Uint8Array([opcode, transaction, status, ...reply]))
    if (opcode === CommandV2.SET_DISPLAY_MODE && status === StatusV2.OK && !state.omitCompletion)
      internal.handleV2Notification(new Uint8Array([EventV2.COMPLETE, 0, state.completionStatus, opcode, transaction, packet[3]]))
  }
  return { client, internal, state, count: (opcode: number) => state.calls.filter((p) => p[0] === opcode).length }
}

it.each([1, 2] as const)('waits out only acknowledged-time display admission BUSY for mode %s', async (mode) => {
  const { client, internal, count, state } = fixture()
  const work = client.syncTime(mode)
  const result = work.then(() => 'done', (error: Error) => error.message)
  await vi.advanceTimersByTimeAsync(101)
  expect(await result).toBe('done')
  expect(count(CommandV2.SET_TIME)).toBe(1)
  expect(count(CommandV2.SET_DISPLAY_MODE)).toBe(2)
  expect(count(CommandV2.GET_STATUS)).toBe(3)
  const displays = state.calls.filter((p) => p[0] === CommandV2.SET_DISPLAY_MODE)
  expect(displays[0][1]).not.toBe(displays[1][1])
  expect(internal.v2Completions.size + internal.v2Pending.size).toBe(0)
})

it.each(['denied', 'unknown', 'terminal-busy', 'second-busy', 'invalid', 'disconnect', 'deadline', 'missing-completion', 'late-old-completion', 'user-cancel'])('does not replay on %s', async (kind) => {
  const { client, internal, count, state } = fixture()
  if (kind === 'denied') state.displayStatus = StatusV2.DENIED
  if (kind === 'unknown') state.failWrite = true
  if (kind === 'terminal-busy') { state.busy = 0; state.completionStatus = StatusV2.BUSY }
  if (kind === 'second-busy') state.busy = 2
  if (kind === 'invalid') state.malformed = true
  if (kind === 'disconnect') state.disconnect = true
  if (kind === 'deadline') state.flags = [1]
  if (kind === 'missing-completion') { state.busy = 0; state.omitCompletion = true }
  if (kind === 'late-old-completion') { state.lateOldCompletion = true; state.completionStatus = StatusV2.IO_ERROR }
  if (kind === 'user-cancel') state.cancelWait = true
  useDeviceStore.getState().setAction('calendar')
  const result = client.syncTime(1).then(() => null, (error: Error) => error)
  await vi.advanceTimersByTimeAsync(['deadline', 'missing-completion'].includes(kind) ? 90001 : 101)
  expect(await result).toBeInstanceOf(Error)
  expect(count(CommandV2.SET_TIME)).toBe(1)
  expect(count(CommandV2.SET_DISPLAY_MODE)).toBe(['second-busy', 'late-old-completion'].includes(kind) ? 2 : 1)
  expect(internal.v2Completions.size + internal.v2Pending.size).toBe(0)
  expect(count(CommandV2.CANCEL)).toBe(kind === 'user-cancel' ? 1 : 0)
  if (kind === 'late-old-completion') expect(state.lateOldAction).toBe('calendar')
})

it('does not start display or idle polling if SET_TIME is rejected', async () => {
  const { client, count, state } = fixture()
  state.timeStatus = StatusV2.BUSY
  await expect(client.syncTime(1)).rejects.toBeInstanceOf(ProtocolV2Error)
  expect(count(CommandV2.SET_TIME)).toBe(1)
  expect(count(CommandV2.SET_DISPLAY_MODE)).toBe(0)
  expect(count(CommandV2.GET_STATUS)).toBe(1)
})

it('keeps ordinary no-sync display BUSY as a single refusal', async () => {
  const { internal, count } = fixture()
  await expect(internal.runV2DisplayWork(1)).rejects.toBeInstanceOf(ProtocolV2Error)
  expect(count(CommandV2.SET_DISPLAY_MODE)).toBe(1)
  expect(count(CommandV2.GET_STATUS) + count(CommandV2.SET_TIME)).toBe(0)
})
