import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { transferOta } from './ota'
import { inktagClient } from './client'
import { useDeviceStore } from '../store/device'

let transfer: ReturnType<typeof vi.fn>
let enter: ReturnType<typeof vi.fn>
let select: ReturnType<typeof vi.fn>
let sharedEntry: ReturnType<typeof vi.spyOn>
const recovery = { id: 'granted-nrf', name: 'IKT_NRF_6775_OTA' } as BluetoothDevice
const stop = new Error('virtual transfer boundary')
beforeEach(() => {
  useDeviceStore.getState().resetDevice()
  transfer = vi.fn().mockRejectedValue(stop)
  enter = vi.fn().mockResolvedValue('IKT_NRF_6775_OTA')
  select = vi.fn().mockResolvedValue(recovery)
  vi.stubGlobal('document', { createElement: () => ({ remove() {} }), head: { appendChild() {} } })
  vi.stubGlobal('window', { NordicDfu: { transferDevice: transfer, enterButtonless: enter,
    bootloaderName: () => 'IKT_NRF_6775_OTA', findGrantedBootloaderDevice: select } })
  vi.spyOn(inktagClient, 'getGattServer').mockReturnValue({ connected: true } as BluetoothRemoteGATTServer)
  vi.spyOn(inktagClient, 'getDevice').mockReturnValue(recovery)
  sharedEntry = vi.spyOn(inktagClient, 'enterOtaRecovery').mockResolvedValue(undefined)
  vi.spyOn(inktagClient, 'getExpectedRecoveryIdentity').mockReturnValue({
    deviceId: recovery.id, recoveryName: 'IKT_NRF_6775_OTA',
  })
})
afterEach(() => { vi.restoreAllMocks(); vi.unstubAllGlobals() })
it('already verified nRF Recovery transfers on its exact grant without App/buttonless writes or a chooser', async () => {
  vi.spyOn(inktagClient, 'getOtaRecoveryType').mockReturnValue('nordic')
  await expect(transferOta({ type: 'nordic', payload: {}, summary: '' }, () => {})).rejects.toBe(stop)
  expect(transfer).toHaveBeenCalledExactlyOnceWith(recovery, {}, expect.any(Function), { expectedBoard: '', maxPacketSize: 20, onPhase: expect.any(Function) })
  expect(enter).not.toHaveBeenCalled()
  expect(select).not.toHaveBeenCalled()
  expect(sharedEntry).not.toHaveBeenCalled()
})
it.each(['aesl22_nrf', 'nrf52811_aesl'])('does not reuse a retained %s label to enable larger direct-Recovery writes', async boardId => {
  useDeviceStore.setState({ boardId })
  vi.spyOn(inktagClient, 'getOtaRecoveryType').mockReturnValue('nordic')
  await expect(transferOta({ type: 'nordic', payload: { verified: { board: 'aesl22_nrf' } }, summary: '' }, () => {})).rejects.toBe(stop)
  expect(transfer.mock.calls[0]?.[3]).toEqual({ expectedBoard: 'aesl22_nrf', maxPacketSize: 20, onPhase: expect.any(Function) })
  expect(sharedEntry).not.toHaveBeenCalled()
  expect(select).not.toHaveBeenCalled()
})
it('preserves the existing nRF32 direct-Recovery packet policy', async () => {
  useDeviceStore.setState({ boardId: 'nrf52832_priceetag' })
  vi.spyOn(inktagClient, 'getOtaRecoveryType').mockReturnValue('nordic')
  await expect(transferOta({ type: 'nordic', payload: { verified: { board: 'nrf52832_priceetag' } }, summary: '' }, () => {})).rejects.toBe(stop)
  expect(transfer.mock.calls[0]?.[3]).toEqual({ expectedBoard: 'nrf52832_priceetag', maxPacketSize: 200, onPhase: expect.any(Function) })
})
it.each(['aesl22_nrf', 'nrf52832_priceetag'])('preserves the live-App 200-byte route for %s', async boardId => {
  useDeviceStore.setState({ boardId })
  vi.spyOn(inktagClient, 'getOtaRecoveryType').mockReturnValue(null)
  await expect(transferOta({ type: 'nordic', payload: { verified: { board: boardId } }, summary: '' }, () => {})).rejects.toBe(stop)
  expect(sharedEntry).toHaveBeenCalledOnce()
  expect(transfer.mock.calls[0]?.[3]).toEqual({ expectedBoard: boardId, maxPacketSize: 200, onPhase: expect.any(Function) })
})
it('normal Nordic App uses one shared AUTH entry and binds the same-grant DFU selection', async () => {
  vi.spyOn(inktagClient, 'getOtaRecoveryType').mockReturnValue(null)
  const log = vi.spyOn(useDeviceStore.getState(), 'addLog')
  const progress = vi.fn()
  select.mockImplementationOnce(async (_name, _handoff, _device, status) => {
    expect(useDeviceStore.getState().otaPhase).toBe('connecting-recovery')
    status('Recovery 服务仍在切换，断开后重新发现通道')
    return recovery
  })
  await expect(transferOta({ type: 'nordic', payload: {}, summary: '' }, progress)).rejects.toBe(stop)
  expect(sharedEntry).toHaveBeenCalledOnce()
  expect(enter).not.toHaveBeenCalled()
  expect(select).toHaveBeenCalledExactlyOnceWith('IKT_NRF_6775_OTA', {
    deviceId: recovery.id, recoveryName: 'IKT_NRF_6775_OTA',
  }, recovery, expect.any(Function))
  expect(log).toHaveBeenCalledWith('Recovery 服务仍在切换，断开后重新发现通道')
  expect(progress).toHaveBeenCalledWith({ percent: 0, message: 'Recovery 服务仍在切换，断开后重新发现通道' })
  expect(transfer).toHaveBeenCalledOnce()
  expect(useDeviceStore.getState().otaPhase).toBeNull()
})
it('an entry refusal or unknown result does not select, transfer or repeat entry', async () => {
  vi.spyOn(inktagClient, 'getOtaRecoveryType').mockReturnValue(null)
  sharedEntry.mockRejectedValueOnce(new Error('entry result unknown'))
  await expect(transferOta({ type: 'nordic', payload: {}, summary: '' }, () => {})).rejects.toThrow('entry result unknown')
  expect(sharedEntry).toHaveBeenCalledOnce()
  expect(enter).not.toHaveBeenCalled()
  expect(select).not.toHaveBeenCalled()
  expect(transfer).not.toHaveBeenCalled()
})
it('a missing or changed pre-entry identity cannot select or transfer', async () => {
  vi.spyOn(inktagClient, 'getOtaRecoveryType').mockReturnValue(null)
  vi.mocked(inktagClient.getExpectedRecoveryIdentity).mockReturnValue({ deviceId: 'another-grant', recoveryName: 'IKT_NRF_6775_OTA' })
  await expect(transferOta({ type: 'nordic', payload: {}, summary: '' }, () => {})).rejects.toThrow('无法确认升级前')
  expect(sharedEntry).toHaveBeenCalledOnce()
  expect(select).not.toHaveBeenCalled()
  expect(transfer).not.toHaveBeenCalled()
})
