import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { transferOta, type ParsedOtaPackage } from './ota'
import { inktagClient } from './client'
import { useDeviceStore } from '../store/device'

const device = { id: 'exact-da', name: 'IKT_DA_C54F_OTA' } as BluetoothDevice
const pkg = { type: 'dialog', payload: { codeSize: 81164, crc32: 0xc611e7c4 } } as ParsedOtaPackage
let transfer: ReturnType<typeof vi.fn>
let sync: ReturnType<typeof vi.spyOn>
beforeEach(() => {
  vi.useFakeTimers()
  useDeviceStore.getState().resetDevice()
  transfer = vi.fn().mockResolvedValue({ finalizationWriteConfirmed: false, applicationVerified: false })
  vi.stubGlobal('window', { DialogSuota: { transferServer: transfer } })
  vi.stubGlobal('document', { createElement: () => ({ remove() {} }), head: { appendChild() {} } })
  vi.spyOn(inktagClient, 'getDevice').mockReturnValue(device)
  vi.spyOn(inktagClient, 'getGattServer').mockReturnValue({ connected: true, device } as unknown as BluetoothRemoteGATTServer)
  vi.spyOn(inktagClient, 'getOtaRecoveryType').mockReturnValue('dialog')
  vi.spyOn(inktagClient, 'enterOtaRecovery').mockResolvedValue(undefined)
  vi.spyOn(inktagClient, 'reconnectDevice').mockImplementation(async (actual) => {
    expect(actual).toBe(device)
    vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue(null)
  })
  vi.spyOn(inktagClient, 'waitForPasswordAuthorization').mockResolvedValue(undefined)
  vi.spyOn(inktagClient, 'getRuntimeImageInfo').mockReturnValue({ version: 1, appBytes: 81164, checksum: 0xc611e7c4 })
  sync = vi.spyOn(inktagClient, 'syncTime').mockResolvedValue(undefined)
})
afterEach(() => { vi.restoreAllMocks(); vi.unstubAllGlobals(); vi.useRealTimers() })

it('DA App OTA writes only through the verified Recovery server', async () => {
  const handoff = { deviceId: device.id, recoveryName: device.name! }
  const recovery = { connected: true, device } as unknown as BluetoothRemoteGATTServer
  vi.spyOn(inktagClient, 'getExpectedRecoveryIdentity').mockReturnValue(handoff)
  vi.spyOn(inktagClient, 'getVerifiedRecoveryName').mockReturnValue(handoff.recoveryName)
  const release = vi.spyOn(inktagClient, 'releaseForRecovery').mockResolvedValue(undefined)
  vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue(null)
  vi.mocked(inktagClient.reconnectDevice).mockImplementationOnce(async (_actual, _timeout, requireRecovery) => {
    expect(requireRecovery).toBe(true)
    vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue('dialog')
    vi.mocked(inktagClient.getGattServer).mockReturnValue(recovery)
  })
  const pending = transferOta(pkg, vi.fn())
  await vi.runAllTimersAsync(); await pending
  expect(release).toHaveBeenCalledExactlyOnceWith(handoff)
  expect(transfer).toHaveBeenCalledExactlyOnceWith(recovery, pkg.payload, expect.any(Function), expect.anything())
  expect(sync).toHaveBeenCalledOnce()
})

it('unknown FE ATT reaches same-grant App attestation before one clock sync', async () => {
  const progress = vi.fn()
  const pending = transferOta(pkg, progress)
  expect(sync).not.toHaveBeenCalled()
  await vi.runAllTimersAsync(); await pending
  expect(transfer).toHaveBeenCalledOnce()
  expect(inktagClient.enterOtaRecovery).not.toHaveBeenCalled()
  expect(inktagClient.reconnectDevice).toHaveBeenCalledWith(device, expect.any(Number))
  expect(inktagClient.waitForPasswordAuthorization).toHaveBeenCalledTimes(2)
  expect(sync).toHaveBeenCalledOnce()
  const messages = progress.mock.calls.map(([value]) => value.message)
  expect(messages).toContain('数据传输结束，正在等待应用验身；断连不代表升级成功')
  expect(messages.indexOf('应用长度与 CRC32 已核对，试运行已确认')).toBeLessThan(
    messages.indexOf('固件验身完成，正在校准设备时间'))
})

it.each([null, { version: 1, appBytes: 81163, checksum: 0xc611e7c4 },
  { version: 1, appBytes: 81164, checksum: 0xc611e7c5 }])('missing/mismatched App is not success or a write retry: %j', async runtime => {
  vi.mocked(inktagClient.getRuntimeImageInfo).mockReturnValue(runtime)
  const progress = vi.fn()
  const failure = expect(transferOta(pkg, progress)).rejects.toThrow('应用与升级包不一致')
  await vi.runAllTimersAsync(); await failure
  expect(transfer).toHaveBeenCalledOnce()
  expect(sync).not.toHaveBeenCalled()
  expect(progress.mock.calls.map(([value]) => value.message)).not.toContain('OTA 完成，应用已验身、重连并校时')
})

it('explicit receiver refusal does not reconnect, synchronize or replay', async () => {
  transfer.mockRejectedValueOnce(new Error('image finalize failed with SUOTA status 0x06'))
  await expect(transferOta(pkg, vi.fn())).rejects.toThrow('0x06')
  expect(transfer).toHaveBeenCalledOnce()
  expect(inktagClient.reconnectDevice).not.toHaveBeenCalled()
  expect(sync).not.toHaveBeenCalled()
})

it('Recovery reconnect alone cannot count as returned App', async () => {
  vi.spyOn(inktagClient, 'disconnectAndWait').mockResolvedValue(undefined)
  vi.mocked(inktagClient.reconnectDevice).mockResolvedValue(undefined)
  const failure = expect(transferOta(pkg, vi.fn())).rejects.toThrow('应用未重新上线')
  await vi.runAllTimersAsync(); await failure
  expect(transfer).toHaveBeenCalledOnce()
  expect(inktagClient.reconnectDevice).toHaveBeenCalledTimes(9)
  expect(sync).not.toHaveBeenCalled()
})
