import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
vi.mock('../data/db', () => ({ db: { devicePreferences: { get: vi.fn().mockResolvedValue(undefined) } } }))
import { InkTagClient, ProtocolV2Error } from './client'
import { CommandV2, StatusV2 } from './generatedCommandsV2'
import { useDeviceStore } from '../store/device'

function fixture(statuses: number[], delayInfoMs = 0) {
  const client = new InkTagClient()
  const internal = client as unknown as {
    protocolV2: boolean; characteristic: BluetoothRemoteGATTCharacteristic
    handleV2Notification: (bytes: Uint8Array) => void
  }
  internal.protocolV2 = true
  const packets: Uint8Array[] = []
  internal.characteristic = {
    removeEventListener: vi.fn(),
    writeValueWithResponse: vi.fn(async (value: BufferSource) => {
      const packet = new Uint8Array(value as ArrayBuffer)
      packets.push(packet)
      const status = statuses.shift() ?? StatusV2.OK
      const info = new Uint8Array(32); info[0] = 1
      const payload = packet[0] === CommandV2.GET_RESOURCE_INFO ? info : new Uint8Array([1, 0])
      const reply = new Uint8Array([packet[0], packet[1], status, ...payload])
      if (packet[0] === CommandV2.GET_RESOURCE_INFO && delayInfoMs) {
        setTimeout(() => internal.handleV2Notification(reply), delayInfoMs)
      } else internal.handleV2Notification(reply)
    }),
  } as unknown as BluetoothRemoteGATTCharacteristic
  return { client, packets }
}

describe('resource readiness on an established link', () => {
  beforeEach(() => {
    vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout', 'performance'] })
    Object.defineProperty(globalThis, 'window', { configurable: true, value: globalThis })
    useDeviceStore.getState().resetDevice()
    useDeviceStore.getState().setConnection('connected')
  })
  afterEach(() => vi.useRealTimers())

  it('keeps the base connection and retries the same list cursor with fresh transactions', async () => {
    const { client, packets } = fixture([StatusV2.BUSY, StatusV2.OK, StatusV2.BUSY, StatusV2.OK])
    const pending = client.listStoredImages()
    await vi.advanceTimersByTimeAsync(0)
    expect(useDeviceStore.getState().resourceReadState).toBe('loading')
    expect(useDeviceStore.getState().connection).toBe('connected')
    await vi.advanceTimersByTimeAsync(4000)
    await pending
    expect(packets.map((packet) => packet[0])).toEqual([0x18, 0x18, 0x19, 0x19])
    expect(new Set(packets.map((packet) => packet[1])).size).toBe(4)
    expect(packets.slice(2).map((packet) => Array.from(packet.subarray(3)))).toEqual([[255], [255]])
    expect(useDeviceStore.getState().resourceReadState).toBe('idle')
    expect(useDeviceStore.getState().imageStoreInfo).not.toBeNull()
  })

  it('preserves previously verified records on failure without claiming an empty store', async () => {
    const { client, packets } = fixture([StatusV2.DENIED])
    const old = { id: 0, name: 'old', contentType: 'image' as const, format: 1, segments: 1, compatible: true, width: 100, height: 100, bytes: 10, createdAt: 0 }
    useDeviceStore.getState().setImageStoreItems([old])
    await expect(client.listStoredImages()).rejects.toBeInstanceOf(ProtocolV2Error)
    expect(packets).toHaveLength(1)
    expect(useDeviceStore.getState().imageStoreItems).toEqual([old])
    expect(useDeviceStore.getState().resourceReadState).toBe('error')
    expect(useDeviceStore.getState().connection).toBe('connected')
  })

  it('waits for a post-COMMIT notification delayed by a 20-second native repaint without resending', async () => {
    const { client, packets } = fixture([], 20000)
    const pending = client.listStoredImages({ afterCommit: true })
    const completed = expect(pending).resolves.toBeUndefined()
    await vi.advanceTimersByTimeAsync(19999)
    expect(packets.map(packet => packet[0])).toEqual([CommandV2.GET_RESOURCE_INFO])
    expect(useDeviceStore.getState().resourceReadState).toBe('loading')
    await vi.advanceTimersByTimeAsync(1)
    await completed
    expect(packets.map(packet => packet[0])).toEqual([CommandV2.GET_RESOURCE_INFO, CommandV2.LIST_RESOURCES])
    expect(useDeviceStore.getState().resourceReadState).toBe('idle')
    expect(useDeviceStore.getState().connection).toBe('connected')
  })

  it('bounds the entire post-COMMIT read and ignores late replies after timeout', async () => {
    const { client, packets } = fixture([], 200000)
    const pending = client.listStoredImages({ afterCommit: true })
    const rejected = expect(pending).rejects.toThrow('响应超时')
    await vi.advanceTimersByTimeAsync(120000)
    await rejected
    await vi.advanceTimersByTimeAsync(80000)
    expect(packets.map(packet => packet[0])).toEqual([CommandV2.GET_RESOURCE_INFO])
    expect(useDeviceStore.getState().resourceReadState).toBe('error')
    expect(useDeviceStore.getState().imageStoreInfo).toBeNull()
  })

  it('cancels a post-COMMIT in-flight read on disconnect and never publishes its late reply', async () => {
    const { client, packets } = fixture([], 20000)
    const pending = client.listStoredImages({ afterCommit: true })
    const rejected = expect(pending).rejects.toThrow('取消')
    await vi.advanceTimersByTimeAsync(1000)
    client.disconnect()
    await rejected
    await vi.advanceTimersByTimeAsync(20000)
    expect(packets).toHaveLength(1)
    expect(useDeviceStore.getState().imageStoreInfo).toBeNull()
    expect(useDeviceStore.getState().resourceReadState).toBe('idle')
  })
  it('explains INFO NO_SPACE without deleting images, disabling capability or retrying a write', async () => {
    const { client, packets } = fixture([StatusV2.NO_SPACE])
    useDeviceStore.getState().setCapabilities({ imageStore: true, imageStoreSectors: 16 })
    await expect(client.listStoredImages()).rejects.toThrow('不要先清空图片')
    expect(packets.map(packet => packet[0])).toEqual([CommandV2.GET_RESOURCE_INFO])
    expect(useDeviceStore.getState().capabilities.imageStore).toBe(true)
    expect(useDeviceStore.getState().connection).toBe('connected')
  })

  it('cancels resource loading on disconnect without contaminating a reset store', async () => {
    const { client, packets } = fixture([StatusV2.BUSY])
    const pending = client.listStoredImages()
    const rejected = expect(pending).rejects.toThrow('取消')
    await vi.advanceTimersByTimeAsync(0)
    client.disconnect()
    await rejected
    await vi.advanceTimersByTimeAsync(60000)
    expect(packets).toHaveLength(1)
    expect(useDeviceStore.getState().resourceReadState).toBe('idle')
    expect(useDeviceStore.getState().imageStoreItems).toEqual([])
  })
})
