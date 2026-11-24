import { afterEach, beforeEach, expect, it, vi, type Mock } from 'vitest'
import { acquireImageLink } from './imageLink'
import { InkTagClient, ProtocolV2Error } from './client'
import { CommandV2, StatusV2, CapabilityV2 } from './generatedCommandsV2'
import * as packbits from './packbits'
import { makeScreenConfig } from './screen'
import { useDeviceStore } from '../store/device'
import { db } from '../data/db'
import Dexie from 'dexie'

vi.mock('./imageLink', () => ({ acquireImageLink: vi.fn() }))
const original = useDeviceStore.getState()
let order: string[]
let client: any
let close: Mock<() => Promise<void>>
const screen = makeScreenConfig({ driver: 0x13, color: 2, transform: 0,
  width: 8, height: 8, visibleX: 0, visibleY: 0, visibleWidth: 8, visibleHeight: 8 })
const upload = (stored: boolean) => client.sendCanvasV2({ toDataURL: () => 'data:image/png;base64,x' },
  [{ step: 'bw', data: new Uint8Array([1, 2, 3]) }], 1, screen, 'shared', stored, 'image', undefined, false)

beforeEach(() => {
  vi.clearAllMocks()
  order = []
  client = new InkTagClient() as any
  client.device = { id: 'live' }
  client.server = { connected: true }
  client.characteristic = {}
  client.protocolV2 = true
  client.v2WritePayload = 20
  useDeviceStore.setState({ connection: 'connected', deviceId: 'live',
    bleAddress: 'D2:A3:11:07:63:CF', boardId: 'future_board',
    capabilities: { ...original.capabilities, imageMetadata: false } })
  close = vi.fn(async () => { order.push('release') })
  vi.mocked(acquireImageLink).mockImplementation(async (_address, _board, current) => {
    expect(current()).toBe(true)
    order.push('acquire')
    return { close }
  })
  client.requestV2 = vi.fn(async (op: number) => {
    order.push(op === CommandV2.TRANSFER_BEGIN ? 'begin' : 'abort')
    return { payload: new Uint8Array([1, 7, 20, 0, 1]) }
  })
  client.writeV2TransferChunk = vi.fn(async () => { order.push('data-ack') })
  client.waitForV2Completion = vi.fn(async () => new Uint8Array())
  client.commitV2Transfer = vi.fn(async () => { order.push('commit'); return { payload: new Uint8Array([7]) } })
  client.listStoredImages = vi.fn(async () => {})
  vi.spyOn(db.deviceSlotPreviews, 'put').mockResolvedValue('preview')
})
afterEach(() => { useDeviceStore.setState(original); vi.restoreAllMocks(); vi.unstubAllGlobals() })

it.each([false, true])('actual direct/stored caller acquires before BEGIN and releases after DATA ACK before COMMIT: %s', async (stored) => {
  let ack: () => void
  client.writeV2TransferChunk = vi.fn(() => new Promise<void>((resolve) => {
    order.push('data-start')
    ack = () => { order.push('data-ack'); resolve() }
  }))
  const task = upload(stored)
  await vi.waitFor(() => expect(order).toEqual(['acquire', 'begin', 'data-start']))
  expect(close).not.toHaveBeenCalled()
  ack!()
  await task
  expect(order).toEqual(['acquire', 'begin', 'data-start', 'data-ack', 'release', 'commit'])
  expect(close).toHaveBeenCalledTimes(1)
})

it.each([false, true])('optional bridge failure preserves ordinary product transfer: %s', async (stored) => {
  vi.mocked(acquireImageLink).mockResolvedValue(null)
  await upload(stored)
  expect(order).toEqual(['begin', 'data-ack', 'commit'])
  expect(close).not.toHaveBeenCalled()
})

it.each([false, true])('confirmed direct completion cannot resurrect picture busy after early COMPLETE: consumed=%s', async consumed => {
  useDeviceStore.setState({ action: 'picture', actionDetail: 'sending' })
  client.commitV2Transfer.mockImplementation(async () => {
    // Actual COMPLETE handler clears the action before COMMIT resolves.
    useDeviceStore.getState().setAction('idle')
    return { payload: new Uint8Array(), completionConsumed: consumed }
  })
  await upload(false)
  expect(useDeviceStore.getState().action).toBe('idle')
  expect(useDeviceStore.getState().actionDetail).toBe('')
})

it.each([false, true])('actual direct/stored transfer does not automatically inspect native metadata during DATA: %s', async stored => {
  const sample = vi.fn(async () => {})
  vi.mocked(acquireImageLink).mockResolvedValue({ close, sample })
  await upload(stored)
  expect(sample).not.toHaveBeenCalled()
  expect(order).toEqual(['begin', 'data-ack', 'release', 'commit'])
  expect(close).toHaveBeenCalledTimes(1)
})

it('observes native ATT backlog separately when product ACK arrives first, without changing ordering', async () => {
  vi.stubGlobal('window', globalThis)
  let now = 0, finishWrite: () => void
  vi.spyOn(performance, 'now').mockImplementation(() => now)
  client.requestV2 = (InkTagClient.prototype as any).requestV2
  client.passwordSupported = () => false
  const write = vi.fn().mockImplementationOnce(() => new Promise<void>(resolve => { finishWrite = resolve }))
    .mockResolvedValue(undefined)
  client.characteristic = { writeValueWithResponse: write }
  const events: Array<[string, number]> = []
  const observe = (phase: string, ms: number) => { events.push([phase, ms]) }
  const first = client.requestV2(CommandV2.TRANSFER_WRITE, new Uint8Array([1]), 0, 10000, 7, true, true, observe)
  await vi.waitFor(() => expect(write).toHaveBeenCalledTimes(1))
  now = 60
  client.handleV2Notification(new Uint8Array([CommandV2.TRANSFER_WRITE, 7, StatusV2.OK]))
  await first
  now = 61
  const second = client.requestV2(CommandV2.TRANSFER_WRITE, new Uint8Array([2]), 0, 10000, 8, true, true, observe)
  await vi.waitFor(() => expect(client.v2Pending.has(8)).toBe(true))
  expect(write).toHaveBeenCalledTimes(1)
  now = 120; finishWrite!()
  await vi.waitFor(() => expect(write).toHaveBeenCalledTimes(2))
  now = 180
  client.handleV2Notification(new Uint8Array([CommandV2.TRANSFER_WRITE, 8, StatusV2.OK]))
  await second
  await client.writeQueue
  expect(events).toContainEqual(['attWrite', 120])
  expect(events).toContainEqual(['writeQueue', 59])
  expect(events).toContainEqual(['reply', 119])
})

it.each(['begin', 'data', 'cancel', 'commit'])('actual caller closes once without masking or replay after %s failure', async (mode) => {
  const error = new Error('original ' + mode)
  if (mode === 'begin') client.requestV2.mockRejectedValueOnce(error)
  if (mode === 'data' || mode === 'cancel') client.writeV2TransferChunk.mockRejectedValueOnce(error)
  if (mode === 'commit') client.commitV2Transfer.mockRejectedValueOnce(error)
  await expect(upload(true)).rejects.toBe(error)
  expect(close).toHaveBeenCalledTimes(1)
  expect(client.writeV2TransferChunk.mock.calls.length).toBeLessThanOrEqual(1)
  expect(client.commitV2Transfer.mock.calls.length).toBeLessThanOrEqual(1)
})

it.each(['generation', 'disconnect', 'device'])('stale %s while awaiting native acquisition cannot send BEGIN', async (mode) => {
  vi.mocked(acquireImageLink).mockImplementation(async () => {
    if (mode === 'generation') client.transferGeneration++
    if (mode === 'disconnect') client.server.connected = false
    if (mode === 'device') client.device = { id: 'other' }
    return { close }
  })
  await expect(upload(true)).rejects.toThrow('已停止')
  expect(client.requestV2).not.toHaveBeenCalled()
  expect(close).toHaveBeenCalledTimes(1)
})

it.each([false, true])('does not COMMIT or ABORT on a replacement connection during lease release: %s', async stored => {
  close.mockImplementation(async () => {
    client.transferGeneration++
    client.device = { id: 'replacement' }
    useDeviceStore.setState({ deviceId: 'replacement', bleAddress: 'D2:A3:11:26:B8:92' })
  })
  let failure: unknown
  try { await upload(stored) } catch (error) { failure = error }
  expect(client.commitV2Transfer).not.toHaveBeenCalled()
  expect(order).not.toContain('abort')
  expect(String(failure)).toContain('已停止')
})

it.each([false, true])('does not ABORT a replacement peer when data failed before delayed lease cleanup: %s', async stored => {
  const error = new Error('original data failure')
  client.writeV2TransferChunk.mockRejectedValueOnce(error)
  close.mockImplementation(async () => {
    client.transferGeneration++
    client.device = { id: 'replacement' }
    useDeviceStore.setState({ deviceId: 'replacement', bleAddress: 'D2:A3:11:26:B8:92' })
  })
  await expect(upload(stored)).rejects.toBe(error)
  expect(order).not.toContain('abort')
  expect(client.commitV2Transfer).not.toHaveBeenCalled()
})

it.each(['connection-generation', 'server', 'characteristic', 'disconnect-request', 'store-connection'])
('same peer with changed %s during native release cannot receive old COMMIT/ABORT', async mode => {
  close.mockImplementation(async () => {
    if (mode === 'connection-generation') client.connectionGeneration++
    if (mode === 'server') client.server = { connected: true }
    if (mode === 'characteristic') client.characteristic = {}
    if (mode === 'disconnect-request') client.disconnectRequested = true
    if (mode === 'store-connection') useDeviceStore.setState({ connection: 'idle' })
  })
  await expect(upload(true)).rejects.toThrow('已停止')
  expect(client.commitV2Transfer).not.toHaveBeenCalled()
  expect(order).not.toContain('abort')
})

it('preserves acknowledged commit fact if connection changes while COMMIT is pending', async () => {
  client.runV2DisplayWork = vi.fn()
  client.commitV2Transfer.mockImplementation(async () => {
    client.connectionGeneration++
    return { payload: new Uint8Array([7]) }
  })
  await expect(upload(true)).rejects.toThrow('图片已提交')
  expect(client.commitV2Transfer).toHaveBeenCalledTimes(1)
  expect(client.runV2DisplayWork).not.toHaveBeenCalled()
  expect(client.listStoredImages).not.toHaveBeenCalled()
  expect(order).not.toContain('abort')
})

it('does not display or read back an old committed slot after connection changes during local preview caching', async () => {
  client.runV2DisplayWork = vi.fn()
  vi.mocked(db.deviceSlotPreviews.put).mockImplementation(() => Dexie.Promise.resolve().then(() => {
    client.server = { connected: true }
    return 'preview'
  }))
  await expect(client.sendCanvasV2({ toDataURL: () => 'data:original' },
    [{ step: 'bw', data: new Uint8Array([1]) }], 1, screen, 'shared', true, 'image', undefined, true))
    .rejects.toThrow('图片已提交')
  expect(vi.mocked(db.deviceSlotPreviews.put).mock.calls[0][0]).toMatchObject({ deviceId: 'live', slotId: 7 })
  expect(client.runV2DisplayWork).not.toHaveBeenCalled()
  expect(client.listStoredImages).not.toHaveBeenCalled()
  expect(order).not.toContain('abort')
})

it('does not continue post-commit readback after optional display switches connection', async () => {
  client.runV2DisplayWork = vi.fn(async () => { client.connectionGeneration++ })
  await expect(client.sendCanvasV2({ toDataURL: () => 'data:original' },
    [{ step: 'bw', data: new Uint8Array([1]) }], 1, screen, 'shared', true, 'image', undefined, true))
    .rejects.toThrow('图片已提交')
  expect(client.listStoredImages).not.toHaveBeenCalled()
  expect(order).not.toContain('abort')
})

it('does not restart compressed BEGIN on a replacement connection after its first ABORT', async () => {
  client.v2Capabilities = CapabilityV2.TRANSFER_PACKBITS_RLE
  client.v2WritePayload = 40
  vi.spyOn(packbits, 'packBitsIsWorthwhile').mockReturnValueOnce(true).mockReturnValueOnce(false)
  const originalRequest = client.requestV2.getMockImplementation()
  client.requestV2.mockImplementation(async (op: number) => {
    const value = await originalRequest(op)
    if (op === CommandV2.TRANSFER_ABORT) client.connectionGeneration++
    return value
  })
  await expect(upload(true)).rejects.toThrow('已停止')
  expect(order.filter(item => item === 'begin')).toHaveLength(1)
  expect(order.filter(item => item === 'abort')).toHaveLength(1)
})

it('queued COMMIT bytes never move to a replacement GATT characteristic', async () => {
  let resume: () => void
  client.writeQueue = new Promise<void>(resolve => { resume = resolve })
  const old = { writeValueWithResponse: vi.fn(async () => {}) }
  const replacement = { writeValueWithResponse: vi.fn(async () => {}) }
  client.characteristic = old
  const task = client.writePacket(new Uint8Array([CommandV2.TRANSFER_COMMIT, 7, 0]))
  const outcome = expect(task).rejects.toThrow('连接已改变')
  client.connectionGeneration++
  client.characteristic = replacement
  resume!()
  await outcome
  expect(old.writeValueWithResponse).not.toHaveBeenCalled()
  expect(replacement.writeValueWithResponse).not.toHaveBeenCalled()
})

it('failed old queued request cannot remove a new connection pending transaction with the same ID', async () => {
  vi.stubGlobal('window', globalThis)
  client.requestV2 = (InkTagClient.prototype as any).requestV2
  client.passwordSupported = () => false
  let resume: () => void
  client.writeQueue = new Promise<void>(resolve => { resume = resolve })
  client.characteristic = { writeValueWithResponse: vi.fn(async () => {}) }
  const old = client.requestV2(CommandV2.TRANSFER_COMMIT, new Uint8Array(), 0, 10000, 7)
  const oldOutcome = old.catch((error: unknown) => error)
  // The shared mailbox dispatches on a microtask; keep the native write queued
  // while testing replacement of the already-dispatched request's identity.
  await Promise.resolve()
  const pending = client.v2Pending.get(7)
  clearTimeout(pending.timer)
  pending.reject(new Error('original disconnect'))
  const replacement = { reject: vi.fn(), timer: undefined }
  client.v2Pending.set(7, replacement)
  client.connectionGeneration++
  client.characteristic = { writeValueWithResponse: vi.fn(async () => {}) }
  resume!()
  await oldOutcome
  await client.writeQueue
  await Promise.resolve()
  expect(client.v2Pending.get(7)).toBe(replacement)
  replacement.reject.mockClear()
  client.v2Pending.clear()
})

it.each(['data', 'commit'])('lost %s ACK never probes or retries the transaction on a replacement connection', async mode => {
  const timeout = new ProtocolV2Error(StatusV2.TIMEOUT)
  client.requestV2.mockImplementationOnce(async () => { client.connectionGeneration++; throw timeout })
  const task = mode === 'data'
    ? (InkTagClient.prototype as any).writeV2TransferChunk.call(client, 7, 1, 3, 0, new Uint8Array([1, 2, 3]))
    : (InkTagClient.prototype as any).commitV2Transfer.call(client, 7, 1, 3, null)
  await expect(task).rejects.toBe(timeout)
  expect(client.requestV2).toHaveBeenCalledTimes(1)
})
