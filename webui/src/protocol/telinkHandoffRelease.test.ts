import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { inktagClient } from './client'
import { transferOta } from './ota'
import { useDeviceStore } from '../store/device'

const device = { id: 'same-tlsr-grant', name: 'IKT_TL_80FA' } as BluetoothDevice
const boundary = new Error('test boundary before any firmware write')
let enter: ReturnType<typeof vi.spyOn>, release: ReturnType<typeof vi.spyOn>
let transfer: ReturnType<typeof vi.fn>
beforeEach(() => {
  vi.useFakeTimers(); useDeviceStore.getState().resetDevice()
  transfer = vi.fn().mockRejectedValue(boundary)
  vi.stubGlobal('window', { TelinkOta: { acquireRecoverySession: transfer } })
  vi.stubGlobal('document', { createElement: () => ({ remove() {} }), head: { appendChild() {} } })
  vi.spyOn(inktagClient, 'getDevice').mockReturnValue(device)
  vi.spyOn(inktagClient, 'getGattServer').mockReturnValue({ connected: true } as BluetoothRemoteGATTServer)
  vi.spyOn(inktagClient, 'getOtaRecoveryType').mockReturnValue(null)
  enter = vi.spyOn(inktagClient, 'enterOtaRecovery').mockResolvedValue(undefined)
  release = vi.spyOn(inktagClient, 'releaseTelinkAppForOta').mockResolvedValue({ deviceId: device.id, recoveryName: 'IKT_TL_80FA_OTA' })
  vi.spyOn(inktagClient, 'takeTelinkRecoveryForOta').mockReturnValue({ deviceId: device.id, recoveryName: 'IKT_TL_80FA_OTA' })
})
afterEach(() => { vi.restoreAllMocks(); vi.unstubAllGlobals(); vi.useRealTimers() })

it('awaits accepted enter then ordinary-client release before native discovery', async () => {
  let released = false
  release.mockImplementation(async () => { released = true; return { deviceId: device.id, recoveryName: 'IKT_TL_80FA_OTA' } })
  transfer.mockImplementation(async () => { expect(released).toBe(true); throw boundary })
  const run = transferOta({ type: 'telink', payload: {}, summary: '' } as any, () => {})
  const result = expect(run).rejects.toThrow(boundary)
  await vi.runAllTimersAsync(); await result
  expect(enter).toHaveBeenCalledTimes(1)
  expect(release).toHaveBeenCalledWith(device.id)
  expect(enter.mock.invocationCallOrder[0]).toBeLessThan(release.mock.invocationCallOrder[0])
  expect(release.mock.invocationCallOrder[0]).toBeLessThan(transfer.mock.invocationCallOrder[0])
  expect(transfer).toHaveBeenCalledTimes(1)
})

it.each(['enter', 'release'])('never retries enter or discovers after failed %s', async phase => {
  if (phase === 'enter') enter.mockRejectedValueOnce(new Error('accepted outcome unknown'))
  else release.mockRejectedValueOnce(new Error('native session not released'))
  const run = transferOta({ type: 'telink', payload: {}, summary: '' } as any, () => {})
  const result = expect(run).rejects.toThrow()
  await vi.runAllTimersAsync(); await result
  expect(enter).toHaveBeenCalledTimes(1)
  expect(transfer).not.toHaveBeenCalled()
  if (phase === 'enter') expect(release).not.toHaveBeenCalled()
})

it('retains an already connected Recovery without enter or ordinary-App release', async () => {
  vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue('telink')
  const run = transferOta({ type: 'telink', payload: {}, summary: '' } as any, () => {})
  const result = expect(run).rejects.toThrow(boundary)
  await vi.runAllTimersAsync(); await result
  expect(enter).not.toHaveBeenCalled(); expect(release).not.toHaveBeenCalled()
  expect(transfer).toHaveBeenCalledTimes(1)
})
