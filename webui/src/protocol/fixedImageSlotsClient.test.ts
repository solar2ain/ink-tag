import { afterEach, describe, expect, it, vi } from 'vitest'
import { inktagClient } from './client'
import type { ImageStoreInfo } from '../types'
import { useDeviceStore } from '../store/device'

const transport = inktagClient as unknown as {
  requestResourceSnapshot: () => Promise<{payload: Uint8Array}>
  readV2ResourceInfo: (budget: unknown) => Promise<ImageStoreInfo>
  protocolV2: boolean
}
const reply = (length: number, count = 0, required = 4, total = 40, saved = 2) => {
  const payload = new Uint8Array(length), view = new DataView(payload.buffer)
  payload[0] = 1
  view.setUint16(9, required, true)
  view.setUint16(11, total, true)
  view.setUint16(13, 32, true)
  view.setUint16(15, 32, true)
  view.setUint16(17, saved, true)
  if (length >= 32) view.setUint16(30, 8, true)
  if (length >= 34) view.setUint16(32, count, true)
  return payload
}
const decode = (payload: Uint8Array) => {
  vi.spyOn(transport, 'requestResourceSnapshot').mockResolvedValue({payload})
  return transport.readV2ResourceInfo({})
}
const previousV2 = transport.protocolV2
afterEach(() => { vi.restoreAllMocks(); transport.protocolV2 = previousV2 })

describe('fixed-slot resource information tail', () => {
  it.each([30, 32, 34])('does not infer empty-slot targeting from old/zero information (%i bytes)', async (length) => {
    expect((await decode(reply(length))).fixedSlotCount).toBeUndefined()
  })
  it.each([8, 10])('uses the exact count, including separate-Flash rounding (%i)', async (count) => {
    expect((await decode(reply(34, count))).fixedSlotCount).toBe(count)
  })
  it('does not mistake the base-clamped allowance for physical Pro capacity', async () => {
    const raw = reply(34, 32, 2, 64, 0), view = new DataView(raw.buffer)
    view.setUint16(13, 64, true)
    view.setUint16(30, 10, true)
    const info = await decode(raw)
    expect(info.contiguousFit).toBe(10)
    expect(info.physicalContiguousFit).toBe(32)
  })
  it('accepts 255 slots, leaving wire ID 255 reserved', async () => {
    expect((await decode(reply(34, 255, 1, 300))).fixedSlotCount).toBe(255)
  })
  it.each([
    reply(33), reply(34, 256, 1, 300), reply(34, 11), reply(34, 10, 0), reply(34, 8, 4, 40, 9),
  ])('rejects a truncated or impossible fixed capacity', async (payload) => {
    await expect(decode(payload)).rejects.toThrow(/固定图槽/)
  })
})

describe('fixed-slot complete physical occupancy', () => {
  const list = async (done: Uint8Array, itemId?: number) => {
    transport.protocolV2 = true
    useDeviceStore.setState({activation: {...useDeviceStore.getState().activation, phase: 'unsupported'}})
    vi.spyOn(transport, 'readV2ResourceInfo').mockResolvedValue({
      fixedSlotCount: 10, totalSectors: 40, requiredSectors: 4, contiguousFit: 8, saved: 1,
    } as ImageStoreInfo)
    const snapshot = vi.spyOn(transport, 'requestResourceSnapshot')
    if (itemId !== undefined) {
      const payload = new Uint8Array(19)
      payload[1] = itemId
      snapshot.mockResolvedValueOnce({payload})
    }
    snapshot.mockResolvedValue({payload: done})
    await inktagClient.listStoredImages()
    return useDeviceStore.getState()
  }

  it('keeps hidden occupied IDs without exposing their names', async () => {
    const state = await list(new Uint8Array([1, 0xff, 0x05, 0x01]), 0)
    expect(state.imageStoreInfo?.occupiedIds).toEqual([0, 2, 8])
    expect(state.imageStoreItems.map((item) => item.id)).toEqual([0])
  })
  it('does not invent complete occupancy from a missing tail', async () => {
    expect((await list(new Uint8Array([1, 0xff]))).imageStoreInfo?.occupiedIds).toBeUndefined()
  })
  it.each([[1, 0xff, 1], [1, 0xff, 1, 4], [1, 0xff, 0, 0]])('refuses truncated, out-of-range or contradictory occupancy (%s)', async (...bytes) => {
    await expect(list(new Uint8Array(bytes), 0)).rejects.toThrow(/占用位图/)
    expect(useDeviceStore.getState().resourceReadState).toBe('error')
  })
})
