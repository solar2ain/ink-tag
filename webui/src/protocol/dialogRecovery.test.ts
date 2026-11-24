import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { exitDialogRecovery, exitNordicRecovery, verifyDialogRecovery } from './ota'
import { inktagClient } from './client'
import { useDeviceStore } from '../store/device'

const device = { id: 'exact-da', name: 'IKT_DA_A458_OTA' } as BluetoothDevice
let resume: ReturnType<typeof vi.fn>
let disconnect: ReturnType<typeof vi.spyOn>
let reconnect: ReturnType<typeof vi.spyOn>
beforeEach(() => {
  vi.useFakeTimers()
  useDeviceStore.getState().resetDevice()
  resume = vi.fn().mockResolvedValue(undefined)
  vi.stubGlobal('window', { DialogSuota: { resumeApplication: resume, queryRecovery: vi.fn().mockResolvedValue({safeResume:true}) } })
  vi.stubGlobal('document', { createElement: () => ({remove() {}}), head: {appendChild() {}} })
  vi.spyOn(inktagClient, 'getDevice').mockReturnValue(device)
  vi.spyOn(inktagClient, 'getGattServer').mockReturnValue({connected:true,device} as unknown as BluetoothRemoteGATTServer)
  vi.spyOn(inktagClient, 'getOtaRecoveryType').mockReturnValue('dialog')
  vi.spyOn(inktagClient, 'getRecoverySafeResumeSupported').mockReturnValue(true)
  vi.spyOn(inktagClient, 'getVerifiedRecoveryName').mockReturnValue(device.name!)
  vi.spyOn(inktagClient, 'getRecoveryOriginalImage').mockReturnValue({version:1,appBytes:80676,checksum:42})
  vi.spyOn(inktagClient, 'getRuntimeImageInfo').mockReturnValue({version:1,appBytes:80676,checksum:42})
  vi.spyOn(inktagClient, 'verifyReturnedApplication').mockImplementation(() => undefined)
  vi.spyOn(inktagClient, 'syncTimeAndVerify').mockResolvedValue(undefined)
  disconnect = vi.spyOn(inktagClient, 'disconnectAndWait').mockResolvedValue(undefined)
  reconnect = vi.spyOn(inktagClient, 'reconnectDevice').mockImplementation(async () => {
    vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue(null)
  })
})
afterEach(() => {vi.restoreAllMocks();vi.unstubAllGlobals();vi.useRealTimers()})
it('legacy proof rejects before any exit/disconnect', async () => {
  vi.mocked(inktagClient.getRecoverySafeResumeSupported).mockReturnValue(false)
  await expect(exitDialogRecovery()).rejects.toThrow('不支持')
  expect(resume).not.toHaveBeenCalled();expect(disconnect).not.toHaveBeenCalled()
})
it('metadata read failure remains unsupported', async () => {
  window.DialogSuota!.queryRecovery = vi.fn().mockRejectedValue(new Error('read failed'))
  await expect(verifyDialogRecovery({} as BluetoothRemoteGATTServer)).resolves.toEqual({safeResume:false})
})
it('fresh ACK causes one disconnect then same-device App/image verification', async () => {
  const pending = exitDialogRecovery()
  await vi.runAllTimersAsync();await pending
  expect(resume).toHaveBeenCalledExactlyOnceWith(expect.anything(), {deviceId:device.id,recoveryName:device.name})
  expect(disconnect).toHaveBeenCalledOnce();expect(reconnect).toHaveBeenCalledWith(device,expect.any(Number))
})
it('changed App readback is not reported as success or retried', async () => {
  vi.mocked(inktagClient.getRuntimeImageInfo).mockReturnValue({version:1,appBytes:80676,checksum:43})
  const result = expect(exitDialogRecovery()).rejects.toThrow('原应用读回尚未核验')
  await vi.runAllTimersAsync();await result
  expect(resume).toHaveBeenCalledOnce()
})
it('busy/refusal does not disconnect or replay', async () => {
  resume.mockRejectedValueOnce(new Error('target rejected SAFE_RESUME_APP'))
  await expect(exitDialogRecovery()).rejects.toThrow('rejected')
  expect(resume).toHaveBeenCalledOnce();expect(disconnect).not.toHaveBeenCalled()
})

it('Nordic follows the same idle/proof/actual App attestation gates without forcing reset', async () => {
  vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue('nordic')
  window.NordicDfu = { resumeApplication: resume } as any
  const pending = exitNordicRecovery()
  await vi.runAllTimersAsync();await pending
  expect(resume).toHaveBeenCalledExactlyOnceWith(expect.anything(), {deviceId:device.id,recoveryName:device.name})
  expect(disconnect).toHaveBeenCalledOnce();expect(reconnect).toHaveBeenCalledWith(device,expect.any(Number))
})
it('Nordic unavailable image readback is not successful return', async () => {
  vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue('nordic')
  window.NordicDfu = { resumeApplication: resume } as any
  vi.mocked(inktagClient.getRuntimeImageInfo).mockReturnValue(null)
  const result = expect(exitNordicRecovery()).rejects.toThrow('原应用读回尚未核验')
  await vi.runAllTimersAsync();await result
  expect(resume).toHaveBeenCalledOnce()
})

it.each([
  ['nordic', 'nrf52832_priceetag', 135204, true],
  ['nordic', 'nrf52832_priceetag', 163840, true],
  ['nordic', 'nrf52832_priceetag', 163841, false],
  ['nordic', 'nrf52832_priceetag', 0, false],
  ['nordic', 'aesl22_nrf', 131072, true],
  ['nordic', 'aesl22_nrf', 131073, false],
  ['nordic', 'nrf52811_aesl', 135204, false],
  ['nordic', '', 135204, false],
  ['nordic', 'nrf52832_unknown', 135204, false],
  ['nordic', 'nrf52832_priceetag_extra', 135204, false],
  ['dialog', 'nrf52832_priceetag', 95648, true],
  ['dialog', 'nrf52832_priceetag', 95649, false],
] as const)('keeps the exact %s/%s readback window at %i', async (type, family, appBytes, allowed) => {
  useDeviceStore.setState({boardId:'nrf52832_priceetag'})
  vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue(type)
  window.NordicDfu = { resumeApplication: resume } as any
  vi.mocked(inktagClient.getRecoveryOriginalImage).mockReturnValue(null)
  vi.mocked(inktagClient.getRuntimeImageInfo).mockReturnValue({version:1,appBytes,checksum:42})
  // Only the newly read App identity after reconnect may select its window.
  reconnect.mockImplementation(async () => {
    vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue(null)
    useDeviceStore.setState({boardId:family})
  })
  const operation = type === 'nordic' ? exitNordicRecovery() : exitDialogRecovery()
  const result = allowed ? expect(operation).resolves.toBeUndefined()
    : expect(operation).rejects.toThrow('原应用读回尚未核验')
  await vi.runAllTimersAsync();await result
  expect(resume).toHaveBeenCalledOnce();expect(disconnect).toHaveBeenCalledOnce()
  expect(reconnect).toHaveBeenCalledWith(device,expect.any(Number))
})

it.each(['none', 'size', 'crc'] as const)('retains original Nordic large-image comparison: %s', async mismatch => {
  vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue('nordic')
  window.NordicDfu = { resumeApplication: resume } as any
  vi.mocked(inktagClient.getRecoveryOriginalImage).mockReturnValue({version:1,appBytes:135204,checksum:42})
  vi.mocked(inktagClient.getRuntimeImageInfo).mockReturnValue({version:1,
    appBytes:135204 + Number(mismatch === 'size'),checksum:42 + Number(mismatch === 'crc')})
  reconnect.mockImplementation(async () => {
    vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue(null)
    useDeviceStore.setState({boardId:'nrf52832_priceetag'})
  })
  const operation=exitNordicRecovery()
  const result=mismatch === 'none' ? expect(operation).resolves.toBeUndefined()
    : expect(operation).rejects.toThrow('原应用读回尚未核验')
  await vi.runAllTimersAsync();await result
  expect(resume).toHaveBeenCalledOnce()
})
