import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { renderToStaticMarkup } from 'react-dom/server'
import { inspectTelinkRecovery, inspectNordicRecovery } from './ota'
import { inktagClient } from './client'
import { useDeviceStore } from '../store/device'
import { RecoveryReadButton } from '../components/RecoveryReadButton'

const device = { id: 'exact-grant', name: 'IKT_TL_80FA' } as BluetoothDevice
let read: ReturnType<typeof vi.fn>, release: ReturnType<typeof vi.spyOn>, enter: ReturnType<typeof vi.spyOn>
beforeEach(() => {
  useDeviceStore.getState().resetDevice()
  read = vi.fn().mockResolvedValue({ status: 0, raw: new Uint8Array([0, 1, 2]) })
  vi.stubGlobal('window', { TelinkOta: { queryRecoveryDevice: read } })
  vi.stubGlobal('document', { createElement: () => ({ remove() {} }), head: { appendChild() {} } })
  vi.spyOn(inktagClient, 'getExpectedRecoveryIdentity').mockReturnValue({ deviceId: device.id, recoveryName: 'IKT_TL_80FA_OTA' })
  vi.spyOn(inktagClient, 'getDevice').mockReturnValue(device)
  release = vi.spyOn(inktagClient, 'releaseForRecovery').mockImplementation(async () => {
    vi.mocked(inktagClient.getDevice).mockReturnValue(null)
  })
  enter = vi.spyOn(inktagClient, 'enterOtaRecovery')
  vi.spyOn(inktagClient, 'getOtaRecoveryType').mockReturnValue(null)
})
afterEach(() => { vi.restoreAllMocks(); vi.unstubAllGlobals() })
it('keeps the explicit read in closed details without starting a probe', () => {
  const html = renderToStaticMarkup(<RecoveryReadButton collapsed onRunningChange={() => {}} />)
  expect(html).toContain('<details class="recovery-read-details"><summary>详情</summary>')
  expect(html).not.toContain('<details open')
  expect(html).toContain('读取 Recovery 状态')
  expect(read).not.toHaveBeenCalled()
  expect(release).not.toHaveBeenCalled()
})
it('exposes an ordinary visible read-only button and probes only the exact handoff grant', async () => {
  expect(renderToStaticMarkup(<RecoveryReadButton onRunningChange={() => {}} />)).toContain('读取 Recovery 状态')
  await expect(inspectTelinkRecovery(() => {})).resolves.toContain('Recovery 状态 0')
  expect(release).toHaveBeenCalledExactlyOnceWith({ deviceId: device.id, recoveryName: 'IKT_TL_80FA_OTA' })
  expect(read).toHaveBeenCalledExactlyOnceWith(device, { onPhase: expect.any(Function) })
  expect(enter).not.toHaveBeenCalled()
  expect(useDeviceStore.getState().action).toBe('idle')
})
it('Nordic read requires the verified connected Recovery, not an App or advertised label alone', async () => {
  await expect(inspectNordicRecovery(() => {})).rejects.toThrow('正常蓝牙入口')
  expect(enter).not.toHaveBeenCalled(); expect(release).not.toHaveBeenCalled()
  vi.spyOn(inktagClient, 'getGattServer').mockReturnValue({ connected: true } as BluetoothRemoteGATTServer)
  vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue('nordic')
  vi.spyOn(inktagClient, 'getVerifiedRecoveryName').mockReturnValue('IKT_NRF_F988_OTA')
  const query = vi.fn().mockResolvedValue({ command: { offset: 128, crc: 1, maxSize: 256 }, data: { offset: 0, crc: 0, maxSize: 4096 } })
  window.NordicDfu = { queryRecoveryState: query }
  await expect(inspectNordicRecovery(() => {})).resolves.toContain('DATA offset=0 CRC=00000000 max=4096')
  expect(query).toHaveBeenCalledOnce(); expect(enter).not.toHaveBeenCalled(); expect(release).not.toHaveBeenCalled()
})
it.each([null, { deviceId: device.id, recoveryName: 'IKT_NRF_F988_OTA' }])('refuses absent or foreign handoff %j without any device action', async handoff => {
  vi.mocked(inktagClient.getExpectedRecoveryIdentity).mockReturnValue(handoff)
  await expect(inspectTelinkRecovery(() => {})).rejects.toThrow('交接身份')
  expect(read).not.toHaveBeenCalled(); expect(release).not.toHaveBeenCalled(); expect(enter).not.toHaveBeenCalled()
})
it('never substitutes another cached grant and keeps a failed read terminal', async () => {
  vi.mocked(inktagClient.getDevice).mockReturnValue(null)
  vi.stubGlobal('navigator', { bluetooth: { getDevices: async () => [{ ...device, id: 'different-grant' }] } })
  await expect(inspectTelinkRecovery(() => {})).rejects.toThrow('原设备授权不可用')
  expect(read).not.toHaveBeenCalled()
  vi.mocked(inktagClient.getDevice).mockReturnValue(device)
  read.mockRejectedValueOnce(new Error('native discovery timed out'))
  await expect(inspectTelinkRecovery(() => {})).rejects.toThrow('native discovery timed out')
  expect(read).toHaveBeenCalledOnce(); expect(enter).not.toHaveBeenCalled()
  expect(useDeviceStore.getState().action).toBe('idle')
})
