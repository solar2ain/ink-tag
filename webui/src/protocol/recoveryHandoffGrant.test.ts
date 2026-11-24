import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { InkTagClient } from './client'
import { useDeviceStore } from '../store/device'

beforeEach(() => vi.stubGlobal('window', {}))
afterEach(() => { vi.useRealTimers(); vi.restoreAllMocks(); vi.unstubAllGlobals() })
function peer() {
  const client = new InkTagClient() as any
  const gatt: any = { connected: true, disconnect: vi.fn(() => { gatt.connected = false }) }
  const device = { id: 'same-grant-80FA', gatt, removeEventListener: vi.fn() }
  client.device = device; client.server = gatt
  client.requestSystemAction = vi.fn().mockResolvedValue(undefined)
  useDeviceStore.getState().resetDevice()
  useDeviceStore.getState().setCapabilities({ ota: 'telink' })
  useDeviceStore.getState().setIdentity({ bleAddress: 'DC:0A:FE:F0:80:FA' })
  return { client: client as InkTagClient, raw: client, gatt, device }
}
it('retains the exact grant after accepted entry and central release without getDevices support', async () => {
  vi.useFakeTimers()
  const p = peer(); await p.client.enterOtaRecovery()
  const handoff = p.client.getExpectedRecoveryIdentity()!
  expect(p.client.getRecoveryHandoffDevice()).toBe(p.device)
  const run = p.client.releaseForRecovery(handoff)
  await vi.advanceTimersByTimeAsync(900); await run
  expect(p.gatt.disconnect).toHaveBeenCalledOnce()
  expect(p.client.getDevice()).toBeNull()
  expect(p.client.getExpectedRecoveryIdentity()).toEqual(handoff)
  expect(p.client.getRecoveryHandoffDevice()).toBe(p.device)
  expect(p.raw.requestSystemAction).toHaveBeenCalledOnce() // no repeated entry/reset
  p.client.disconnect() // explicit user disconnect cancels the retained grant
  expect(p.client.getRecoveryHandoffDevice()).toBeNull()
  expect(p.client.getExpectedRecoveryIdentity()).toBeNull()
})
it('refuses a changed handoff before touching the link and revokes a cancelled pending release', async () => {
  vi.useFakeTimers()
  const p = peer(); await p.client.enterOtaRecovery()
  const handoff = p.client.getExpectedRecoveryIdentity()!
  await expect(p.client.releaseForRecovery({ ...handoff, deviceId: 'other' })).rejects.toThrow('身份已变化')
  expect(p.gatt.disconnect).not.toHaveBeenCalled()
  const run = p.client.releaseForRecovery(handoff), rejected = expect(run).rejects.toThrow('交接已取消')
  p.client.disconnect()
  await vi.advanceTimersByTimeAsync(900); await rejected
  expect(p.client.getRecoveryHandoffDevice()).toBeNull()
})
