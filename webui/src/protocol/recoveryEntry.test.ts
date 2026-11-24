import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { inktagClient, ProtocolV2Error } from './client'
import { PROTOCOL_V2_VERSION, StatusV2 } from './generatedCommandsV2'
import { enterFixedRecovery } from './ota'
import { useDeviceStore } from '../store/device'

let server: BluetoothRemoteGATTServer, entry: ReturnType<typeof vi.spyOn>, reconnect: ReturnType<typeof vi.spyOn>
const device = { id: 'exact-grant', name: 'IKT_NRF_6775' } as BluetoothDevice
beforeEach(() => {
  vi.useFakeTimers(); useDeviceStore.getState().resetDevice()
  useDeviceStore.setState({ connection: 'connected', deviceId: device.id, protocolVersion: PROTOCOL_V2_VERSION, firmwareVersion: '0.2.0',
    capabilities: { ...useDeviceStore.getState().capabilities, ota: 'nordic' } })
  server = { connected: true } as BluetoothRemoteGATTServer
  vi.spyOn(inktagClient, 'getGattServer').mockReturnValue(server)
  vi.spyOn(inktagClient, 'getDevice').mockReturnValue(device)
  vi.spyOn(inktagClient, 'getExpectedRecoveryIdentity').mockReturnValue(null)
  vi.spyOn(inktagClient, 'getOtaRecoveryType').mockReturnValue(null)
  vi.spyOn(inktagClient, 'getVerifiedRecoveryName').mockReturnValue('IKT_NRF_6775_OTA')
  vi.stubGlobal('window', { GoodixOta: { enterRecovery: vi.fn().mockResolvedValue(undefined) } })
  vi.stubGlobal('document', { createElement: () => ({ remove() {} }), head: { appendChild() {} } })
  entry = vi.spyOn(inktagClient, 'enterOtaRecovery').mockImplementation(async () => {
    vi.mocked(inktagClient.getExpectedRecoveryIdentity).mockReturnValue({ deviceId: device.id, recoveryName: 'IKT_NRF_6775_OTA' })
    Object.assign(server, { connected: false })
  })
  vi.spyOn(inktagClient, 'releaseForRecovery').mockImplementation(async () => {
    Object.assign(server, { connected: false })
    await new Promise(resolve => setTimeout(resolve, 900))
  })
  reconnect = vi.spyOn(inktagClient, 'reconnectDevice').mockImplementation(async () => {
    vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue('nordic')
  })
})
afterEach(() => { vi.restoreAllMocks(); vi.useRealTimers(); vi.unstubAllGlobals() })
it.each(['goodix', 'onmicro', 'telink', 'dialog', 'nordic'] as const)('uses the shared entry and same-grant live role proof for %s', async type => {
  useDeviceStore.getState().setCapabilities({ ota: type })
  reconnect.mockImplementation(async () => vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue(type))
  const run = enterFixedRecovery(device.id)
  await vi.runAllTimersAsync(); await run
  expect(entry).toHaveBeenCalledOnce()
  if (type === 'goodix') expect(window.GoodixOta!.enterRecovery).toHaveBeenCalledExactlyOnceWith(server, expect.objectContaining({ isCurrent: expect.any(Function) }))
  expect(reconnect).toHaveBeenCalledExactlyOnceWith(device, expect.any(Number))
  expect(useDeviceStore.getState().action).toBe('idle')
})
it.each(['another-grant', ''])('rejects a changed target %s before issuing entry', async id => {
  await expect(enterFixedRecovery(id)).rejects.toThrow('同一台')
  expect(entry).not.toHaveBeenCalled(); expect(reconnect).not.toHaveBeenCalled()
})
it('preserves ordinary AUTH refusal with no reconnection or fallback', async () => {
  entry.mockRejectedValue(new Error('请先验证设备密码'))
  await expect(enterFixedRecovery(device.id)).rejects.toThrow('设备密码')
  expect(entry).toHaveBeenCalledOnce(); expect(reconnect).not.toHaveBeenCalled()
})
it.each([StatusV2.DENIED, StatusV2.LOW_POWER, StatusV2.BUSY])('allows a new explicitly confirmed attempt after matching refusal %s', async status => {
  entry.mockImplementationOnce(async () => {
    vi.mocked(inktagClient.getExpectedRecoveryIdentity).mockReturnValue({ deviceId: device.id, recoveryName: 'IKT_NRF_6775_OTA' })
    throw new ProtocolV2Error(status)
  })
  await expect(enterFixedRecovery(device.id)).rejects.toBeInstanceOf(ProtocolV2Error)
  expect(reconnect).not.toHaveBeenCalled()
  const run = enterFixedRecovery(device.id)
  await vi.runAllTimersAsync(); await run
  expect(entry).toHaveBeenCalledTimes(2); expect(reconnect).toHaveBeenCalledOnce()
})
it('keeps a response timeout sealed instead of treating it as a firmware refusal', async () => {
  entry.mockImplementation(async () => {
    vi.mocked(inktagClient.getExpectedRecoveryIdentity).mockReturnValue({ deviceId: device.id, recoveryName: 'IKT_NRF_6775_OTA' })
    throw new ProtocolV2Error(StatusV2.TIMEOUT)
  })
  await expect(enterFixedRecovery(device.id)).rejects.toBeInstanceOf(ProtocolV2Error)
  await expect(enterFixedRecovery(device.id)).rejects.toThrow('不要重复进入')
  expect(entry).toHaveBeenCalledOnce(); expect(reconnect).not.toHaveBeenCalled()
})
it('does not replay an unknown entry after a route/modal remount on the same connection', async () => {
  entry.mockImplementation(async () => {
    vi.mocked(inktagClient.getExpectedRecoveryIdentity).mockReturnValue({ deviceId: device.id, recoveryName: 'IKT_NRF_6775_OTA' })
    throw new Error('unknown write ACK')
  })
  await expect(enterFixedRecovery(device.id)).rejects.toThrow('unknown write ACK')
  await expect(enterFixedRecovery(device.id)).rejects.toThrow('不要重复进入')
  expect(entry).toHaveBeenCalledOnce(); expect(reconnect).not.toHaveBeenCalled()
})
it('never treats an App/wrong Recovery name as confirmed entry or replays the write', async () => {
  vi.mocked(inktagClient.getVerifiedRecoveryName).mockReturnValue('IKT_NRF_9999_OTA')
  const run = enterFixedRecovery(device.id), rejected = expect(run).rejects.toThrow('Recovery 回连尚未确认')
  await vi.runAllTimersAsync(); await rejected
  expect(entry).toHaveBeenCalledOnce(); expect(reconnect).toHaveBeenCalledTimes(9)
})
it('requires idle V2 App capability before entry', async () => {
  for (const state of [{ action: 'ota' as const }, { protocolVersion: 1 }, { protocolVersion: 2 }, { firmwareVersion: 'Recovery' },
    { capabilities: { ...useDeviceStore.getState().capabilities, ota: 'none' as const } }]) {
    useDeviceStore.setState({ action: 'idle', protocolVersion: PROTOCOL_V2_VERSION, firmwareVersion: '0.2.0',
      capabilities: { ...useDeviceStore.getState().capabilities, ota: 'nordic' } })
    useDeviceStore.setState(state)
    await expect(enterFixedRecovery(device.id)).rejects.toThrow()
  }
  expect(entry).not.toHaveBeenCalled()
})
it('releases only the accepted exact handoff instead of waiting20s for peripheral disconnect', async () => {
  entry.mockImplementation(async () => {
    vi.mocked(inktagClient.getExpectedRecoveryIdentity).mockReturnValue({ deviceId: device.id, recoveryName: 'IKT_NRF_6775_OTA' })
  })
  const reset = vi.spyOn(inktagClient, 'softReset')
  const run = enterFixedRecovery(device.id)
  await vi.runAllTimersAsync(); await run
  expect(entry).toHaveBeenCalledOnce()
  expect(inktagClient.releaseForRecovery).toHaveBeenCalledExactlyOnceWith({ deviceId: device.id, recoveryName: 'IKT_NRF_6775_OTA' })
  expect(reset).not.toHaveBeenCalled(); expect(reconnect).toHaveBeenCalledOnce()
})
it('stops read-side reconnect when the user cancels the handoff during the wait', async () => {
  entry.mockImplementation(async () => {
    vi.mocked(inktagClient.getExpectedRecoveryIdentity).mockReturnValue({ deviceId: device.id, recoveryName: 'IKT_NRF_6775_OTA' })
    Object.assign(server, { connected: false })
    setTimeout(() => vi.mocked(inktagClient.getExpectedRecoveryIdentity).mockReturnValue(null), 100)
  })
  const run = enterFixedRecovery(device.id), rejected = expect(run).rejects.toThrow('交接已取消')
  await vi.runAllTimersAsync(); await rejected
  expect(entry).toHaveBeenCalledOnce(); expect(reconnect).not.toHaveBeenCalled()
})
