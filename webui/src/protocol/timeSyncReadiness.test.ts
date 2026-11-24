import { afterEach, beforeEach, expect, it, vi } from 'vitest'
vi.mock('../data/db', () => ({ db: { devicePreferences: { get: vi.fn() } } }))
vi.mock('./ota', () => ({}))
import { InkTagClient, ProtocolV2Error } from './client'
import { CommandV2, StatusV2 } from './generatedCommandsV2'

beforeEach(() => {
  Object.defineProperty(globalThis, 'window', { configurable: true, value: globalThis })
  vi.useFakeTimers()
  vi.setSystemTime(new Date('2026-11-07T00:00:00.900Z'))
})
afterEach(() => vi.useRealTimers())
const idle = () => ({ payload: new Uint8Array([1, 1, 0, ...new Uint8Array(13)]) })
function fixture() {
  const client = new InkTagClient()
  const internal = client as unknown as {
    device: BluetoothDevice; server: { connected: boolean }; connectionGeneration: number
    requestV2: (opcode: number, payload?: Uint8Array) => Promise<{ payload: Uint8Array }>
  }
  internal.device = { id: 'readiness-only-no-radio' } as BluetoothDevice
  internal.server = { connected: true }
  return { client, internal }
}

it('samples fresh current time after read-only busy and timeout waits, then writes once', async () => {
  const { client, internal } = fixture()
  let reads = 0
  const writes: number[] = []
  vi.spyOn(internal, 'requestV2').mockImplementation(async (opcode, payload) => {
    if (opcode === CommandV2.GET_STATUS) {
      if (++reads === 1) throw new ProtocolV2Error(StatusV2.TIMEOUT, 'read timeout')
      if (reads === 2) return { payload: new Uint8Array([1, 1, 0x11, ...new Uint8Array(13)]) }
      return idle()
    }
    expect(opcode).toBe(CommandV2.SET_TIME)
    writes.push(new DataView(payload!.buffer, payload!.byteOffset).getUint32(0, true))
    return { payload: new Uint8Array() }
  })
  const work = client.syncTime()
  await vi.advanceTimersByTimeAsync(501)
  await work
  expect(reads).toBe(3)
  expect(writes).toEqual([Date.parse('2026-11-07T00:00:01Z') / 1000])
})

it.each(['denied', 'malformed', 'changed-connection', 'deadline'])('never writes time when readiness is %s', async (kind) => {
  const { client, internal } = fixture()
  const ops: number[] = []
  vi.spyOn(internal, 'requestV2').mockImplementation(async opcode => {
    ops.push(opcode)
    if (kind === 'denied') throw new ProtocolV2Error(StatusV2.DENIED, 'denied')
    if (kind === 'malformed') return { payload: new Uint8Array(3) }
    if (kind === 'changed-connection') internal.connectionGeneration++
    return kind === 'deadline' ? { payload: new Uint8Array([1, 1, 1, ...new Uint8Array(13)]) } : idle()
  })
  const result = client.syncTime().then(() => null, error => error)
  await vi.advanceTimersByTimeAsync(kind === 'deadline' ? 90001 : 1)
  expect(await result).toBeInstanceOf(Error)
  expect(ops.every(op => op === CommandV2.GET_STATUS)).toBe(true)
})

it('does not replay an unknown SET_TIME result after successful readiness', async () => {
  const { client, internal } = fixture()
  const ops: number[] = []
  vi.spyOn(internal, 'requestV2').mockImplementation(async opcode => {
    ops.push(opcode)
    if (opcode === CommandV2.SET_TIME) throw new ProtocolV2Error(StatusV2.TIMEOUT, 'write outcome unknown')
    return idle()
  })
  await expect(client.syncTime()).rejects.toBeInstanceOf(ProtocolV2Error)
  expect(ops).toEqual([CommandV2.GET_STATUS, CommandV2.SET_TIME])
})
